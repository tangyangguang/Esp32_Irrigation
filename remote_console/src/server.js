import crypto from "node:crypto";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { loadConfig, consoleRoot } from "./config.js";
import { MqttService } from "./mqtt-service.js";
import {
  ProtocolError,
  validateDeviceDurations,
  validatePlan,
  validatePlanId,
  validateResumeAt,
} from "./protocol.js";
import { StateStore } from "./state-store.js";

const config = loadConfig();
const rootTopic = `${config.topicPrefix}/${config.deviceId}`;
const store = new StateStore(rootTopic);
const mqttService = new MqttService(config, store);
const sessionToken = crypto.randomBytes(24).toString("base64url");
const webRoot = path.join(consoleRoot, "web");
const allowedHosts = new Set([
  `${config.listenHost}:${config.listenPort}`,
  `localhost:${config.listenPort}`,
]);
const allowedOrigins = new Set(
  [...allowedHosts].map((host) => `http://${host}`),
);

function json(response, status, value) {
  const body = JSON.stringify(value);
  response.writeHead(status, {
    "content-type": "application/json; charset=utf-8",
    "content-length": Buffer.byteLength(body),
    "cache-control": "no-store",
    "x-content-type-options": "nosniff",
  });
  response.end(body);
}

function errorResponse(response, error) {
  const status =
    error instanceof ProtocolError ? 400 :
    /版本|冲突/.test(error.message) ? 409 :
    /离线|未连接|未就绪/.test(error.message) ? 503 : 502;
  json(response, status, { ok: false, error: error.message });
}

async function readJson(request) {
  if (!request.headers["content-type"]?.startsWith("application/json")) {
    throw new ProtocolError("请求必须使用 JSON");
  }
  let size = 0;
  const chunks = [];
  for await (const chunk of request) {
    size += chunk.length;
    if (size > 16 * 1024) throw new ProtocolError("请求内容过长");
    chunks.push(chunk);
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}");
  } catch {
    throw new ProtocolError("请求 JSON 无效");
  }
}

function assertMutationAllowed(request) {
  const origin = request.headers.origin;
  if (!origin || !allowedOrigins.has(origin)) {
    throw new ProtocolError("请求来源无效");
  }
  if (request.headers["x-irrigation-token"] !== sessionToken) {
    throw new ProtocolError("控制台会话已失效，请刷新页面");
  }
}

function assertReady() {
  const snapshot = store.snapshot;
  if (!snapshot.brokerConnected) throw new Error("控制台未连接 Broker");
  if (snapshot.availability !== "online") throw new Error("设备当前离线");
  if (!snapshot.state?.ready) throw new Error("设备业务未就绪");
}

async function handleApi(request, response, url) {
  if (request.method === "GET" && url.pathname === "/api/status") {
    json(response, 200, { ok: true, snapshot: store.snapshot });
    return true;
  }
  if (request.method === "GET" && url.pathname === "/api/events") {
    if (url.searchParams.get("token") !== sessionToken) {
      json(response, 403, { ok: false, error: "控制台会话无效" });
      return true;
    }
    response.writeHead(200, {
      "content-type": "text/event-stream; charset=utf-8",
      "cache-control": "no-store",
      connection: "keep-alive",
      "x-accel-buffering": "no",
    });
    const send = (snapshot) =>
      response.write(`event: snapshot\ndata: ${JSON.stringify(snapshot)}\n\n`);
    send(store.snapshot);
    const unsubscribe = store.subscribe(send);
    const heartbeat = setInterval(() => response.write(": keepalive\n\n"), 15000);
    request.on("close", () => {
      clearInterval(heartbeat);
      unsubscribe();
    });
    return true;
  }
  if (!url.pathname.startsWith("/api/") || request.method === "GET") return false;

  assertMutationAllowed(request);
  const body = await readJson(request);
  let result;
  if (request.method === "POST" && url.pathname === "/api/watering/stop") {
    assertReady();
    result = await mqttService.execute("stop");
  } else if (
    request.method === "POST" &&
    url.pathname === "/api/watering/start-plan"
  ) {
    assertReady();
    result = await mqttService.execute("start_plan", {
      id: validatePlanId(body.planId),
    });
  } else if (
    request.method === "POST" &&
    url.pathname === "/api/watering/start-manual"
  ) {
    assertReady();
    result = await mqttService.execute("start_manual", {
      durations: validateDeviceDurations(body.durations, store.snapshot.meta),
    });
  } else if (
    request.method === "POST" &&
    url.pathname === "/api/automatic/pause"
  ) {
    assertReady();
    result = await mqttService.execute("pause_automatic", {
      resume_at: validateResumeAt(body.resumeAt),
    });
  } else if (
    request.method === "POST" &&
    url.pathname === "/api/automatic/resume"
  ) {
    assertReady();
    result = await mqttService.execute("resume_automatic");
  } else {
    const planMatch = /^\/api\/plans\/([1-8])$/.exec(url.pathname);
    if (!planMatch) return false;
    assertReady();
    const planId = Number(planMatch[1]);
    const currentRevision = store.snapshot.state?.revision;
    if (body.expectedRevision !== currentRevision) {
      throw new Error("计划版本已经变化，请重新打开编辑窗口");
    }
    if (request.method === "PUT") {
      const plan = validatePlan(planId, body);
      plan.args.durations = validateDeviceDurations(
        plan.args.durations,
        store.snapshot.meta,
      );
      result = await mqttService.execute(
        "set_plan",
        plan.args,
        plan.expectedRevision,
      );
    } else if (request.method === "DELETE") {
      result = await mqttService.execute(
        "delete_plan",
        { id: planId },
        body.expectedRevision,
      );
    } else {
      return false;
    }
  }
  json(response, 200, { ok: result.ok, result });
  return true;
}

function serveStatic(response, pathname) {
  const files = {
    "/": ["index.html", "text/html; charset=utf-8"],
    "/app.js": ["app.js", "text/javascript; charset=utf-8"],
    "/styles.css": ["styles.css", "text/css; charset=utf-8"],
  };
  const selected = files[pathname];
  if (!selected) return false;
  let body = fs.readFileSync(path.join(webRoot, selected[0]));
  if (pathname === "/") {
    body = Buffer.from(
      body.toString("utf8").replaceAll("__SESSION_TOKEN__", sessionToken),
    );
  }
  response.writeHead(200, {
    "content-type": selected[1],
    "content-length": body.length,
    "cache-control": pathname === "/" ? "no-store" : "no-cache",
    "x-content-type-options": "nosniff",
    "content-security-policy":
      "default-src 'self'; connect-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; base-uri 'none'; frame-ancestors 'none'",
  });
  response.end(body);
  return true;
}

const server = http.createServer(async (request, response) => {
  try {
    if (!allowedHosts.has(request.headers.host || "")) {
      json(response, 400, { ok: false, error: "请求主机无效" });
      return;
    }
    const url = new URL(request.url, `http://${request.headers.host}`);
    if (await handleApi(request, response, url)) return;
    if (request.method === "GET" && serveStatic(response, url.pathname)) return;
    json(response, 404, { ok: false, error: "页面不存在" });
  } catch (error) {
    errorResponse(response, error);
  }
});

server.listen(config.listenPort, config.listenHost, () => {
  console.info(
    `[console] 灌溉控制台：http://${config.listenHost}:${config.listenPort}`,
  );
  if (config.usingDeviceCredentials) {
    console.warn("[console] 当前临时复用设备 MQTT 凭据，正式使用前请配置控制台专用账号");
  }
  mqttService.start();
});

function shutdown() {
  mqttService.stop();
  server.close(() => process.exit(0));
}
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
