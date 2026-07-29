import fs from "node:fs";
import crypto from "node:crypto";
import mqtt from "mqtt";
import { createCommand, parseJsonPayload } from "./protocol.js";

export class MqttService {
  #client;
  #pending = new Map();

  constructor(config, store, logger = console) {
    this.config = config;
    this.store = store;
    this.logger = logger;
    this.rootTopic = `${config.topicPrefix}/${config.deviceId}`;
  }

  start() {
    this.#client = mqtt.connect({
      protocol: "mqtts",
      host: this.config.brokerHost,
      port: this.config.brokerPort,
      username: this.config.username,
      password: this.config.password,
      ca: fs.readFileSync(this.config.caPath),
      rejectUnauthorized: true,
      servername: this.config.brokerHost,
      clientId: `irrigation-console-${crypto.randomUUID()}`,
      protocolVersion: 4,
      clean: true,
      keepalive: 60,
      reconnectPeriod: 2000,
      connectTimeout: 10000,
      resubscribe: true,
    });

    this.#client.on("connect", () => {
      this.logger.info("[mqtt] 已连接公网 Broker");
      this.store.setBrokerConnected(true);
      this.#client.subscribe(
        [
          `${this.rootTopic}/availability`,
          `${this.rootTopic}/meta`,
          `${this.rootTopic}/state`,
          `${this.rootTopic}/run`,
          `${this.rootTopic}/latest`,
          `${this.rootTopic}/plan/+`,
          `${this.rootTopic}/result`,
        ],
        { qos: 1 },
        (error) => {
          if (error) this.logger.error(`[mqtt] 订阅失败：${error.message}`);
        },
      );
    });
    this.#client.on("reconnect", () => this.logger.warn("[mqtt] 正在重新连接"));
    this.#client.on("close", () => this.store.setBrokerConnected(false));
    this.#client.on("error", (error) =>
      this.logger.error(`[mqtt] ${error.message}`),
    );
    this.#client.on("message", (topic, payload) => this.#onMessage(topic, payload));
  }

  stop() {
    for (const pending of this.#pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(new Error("控制台正在停止"));
    }
    this.#pending.clear();
    this.#client?.end(true);
  }

  async execute(action, args, revision) {
    if (!this.store.snapshot.brokerConnected) throw new Error("控制台未连接 Broker");
    if (this.store.snapshot.availability !== "online") throw new Error("设备当前离线");
    const command = createCommand(action, args, revision);
    const payload = JSON.stringify(command);
    if (Buffer.byteLength(payload) > 1024) throw new Error("命令内容过长");

    const resultPromise = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.#pending.delete(command.id);
        reject(new Error("设备在10秒内没有确认命令"));
      }, this.config.commandTimeoutMs);
      this.#pending.set(command.id, { resolve, reject, timer });
    });

    this.#client.publish(
      `${this.rootTopic}/command`,
      payload,
      { qos: 1, retain: false },
      (error) => {
        if (!error) return;
        const pending = this.#pending.get(command.id);
        if (!pending) return;
        clearTimeout(pending.timer);
        this.#pending.delete(command.id);
        pending.reject(error);
      },
    );
    return resultPromise;
  }

  #onMessage(topic, payload) {
    const prefix = `${this.rootTopic}/`;
    if (!topic.startsWith(prefix)) return;
    const relativeTopic = topic.slice(prefix.length);
    if (relativeTopic === "availability") {
      this.store.update(relativeTopic, payload.toString("utf8"));
      return;
    }
    let value;
    try {
      value = parseJsonPayload(payload, topic);
    } catch (error) {
      this.logger.error(`[mqtt] ${error.message}`);
      return;
    }
    this.store.update(relativeTopic, value);
    if (relativeTopic !== "result" || !value.id) return;
    const pending = this.#pending.get(value.id);
    if (!pending) return;
    clearTimeout(pending.timer);
    this.#pending.delete(value.id);
    pending.resolve(value);
  }
}
