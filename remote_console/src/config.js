import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const directory = path.dirname(fileURLToPath(import.meta.url));
export const consoleRoot = path.resolve(directory, "..");
export const projectRoot = path.resolve(consoleRoot, "..");

function parseIniSection(text, sectionName) {
  const values = {};
  let active = false;
  for (const originalLine of text.split(/\r?\n/)) {
    const line = originalLine.trim();
    if (!line || line.startsWith(";") || line.startsWith("#")) continue;
    if (line.startsWith("[") && line.endsWith("]")) {
      active = line.slice(1, -1).trim() === sectionName;
      continue;
    }
    if (!active) continue;
    const separator = line.indexOf("=");
    if (separator < 1) continue;
    values[line.slice(0, separator).trim()] = line.slice(separator + 1).trim();
  }
  return values;
}

function requireText(value, name) {
  if (typeof value !== "string" || value.trim() === "") {
    throw new Error(`缺少配置：${name}`);
  }
  return value.trim();
}

export function loadConfig() {
  const publicConfig = JSON.parse(
    fs.readFileSync(path.join(consoleRoot, "config.json"), "utf8"),
  );
  const privatePath = path.join(projectRoot, "firmware", "platformio.local.ini");
  const privateValues = parseIniSection(
    fs.readFileSync(privatePath, "utf8"),
    "env:esp32_irrigation",
  );

  const consoleUsername =
    privateValues.custom_irrigation_console_mqtt_username?.trim();
  const consolePassword =
    privateValues.custom_irrigation_console_mqtt_password?.trim();
  if (Boolean(consoleUsername) !== Boolean(consolePassword)) {
    throw new Error("控制台 MQTT 用户名和密码必须同时配置");
  }
  const usingDeviceCredentials = !consoleUsername;
  const username =
    consoleUsername || privateValues.custom_irrigation_mqtt_username;
  const password =
    consolePassword || privateValues.custom_irrigation_mqtt_password;

  const listenPort = Number(publicConfig.listenPort);
  const commandTimeoutMs = Number(publicConfig.commandTimeoutMs);
  const brokerPort = Number(
    requireText(
      privateValues.custom_irrigation_mqtt_port,
      "custom_irrigation_mqtt_port",
    ),
  );
  if (!Number.isInteger(brokerPort) || brokerPort < 1 || brokerPort > 65535) {
    throw new Error("MQTT 端口无效");
  }
  if (!Number.isInteger(listenPort) || listenPort < 1024 || listenPort > 65535) {
    throw new Error("控制台监听端口无效");
  }
  if (
    !Number.isInteger(commandTimeoutMs) ||
    commandTimeoutMs < 1000 ||
    commandTimeoutMs > 60000
  ) {
    throw new Error("命令超时时间无效");
  }

  return Object.freeze({
    brokerHost: requireText(
      privateValues.custom_irrigation_mqtt_host,
      "custom_irrigation_mqtt_host",
    ),
    brokerPort,
    username: requireText(username, "控制台 MQTT 用户名"),
    password: requireText(password, "控制台 MQTT 密码"),
    topicPrefix: requireText(
      privateValues.custom_irrigation_mqtt_topic_prefix,
      "custom_irrigation_mqtt_topic_prefix",
    ).replace(/\/+$/, ""),
    caPath: path.join(
      projectRoot,
      "firmware",
      requireText(
        privateValues.custom_irrigation_mqtt_ca_file,
        "custom_irrigation_mqtt_ca_file",
      ),
    ),
    deviceId: requireText(publicConfig.deviceId, "deviceId"),
    listenHost: publicConfig.listenHost === "127.0.0.1" ? "127.0.0.1" : "127.0.0.1",
    listenPort,
    commandTimeoutMs,
    usingDeviceCredentials,
  });
}
