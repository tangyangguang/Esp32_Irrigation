import assert from "node:assert/strict";
import test from "node:test";
import {
  ProtocolError,
  createCommand,
  parseJsonPayload,
  validateDurations,
  validatePlan,
} from "../src/protocol.js";

test("生成 MQTT v1 命令并为每条命令分配唯一 ID", () => {
  const first = createCommand("stop");
  const second = createCommand("stop");
  assert.equal(first.v, 1);
  assert.equal(first.action, "stop");
  assert.notEqual(first.id, second.id);
});

test("计划输入会排序去重并保持六路分钟时长", () => {
  const value = validatePlan(2, {
    expectedRevision: 7,
    name: " 早晨浇水 ",
    enabled: true,
    starts: [480, 360, 480],
    durations: [10, 8, 0, 5, 0, 0],
  });
  assert.deepEqual(value, {
    expectedRevision: 7,
    args: {
      id: 2,
      name: "早晨浇水",
      enabled: true,
      starts: [360, 480],
      durations: [10, 8, 0, 5, 0, 0],
    },
  });
});

test("拒绝越界时长和不完整的六路输入", () => {
  assert.throws(
    () => validateDurations([1, 2, 3]),
    ProtocolError,
  );
  assert.throws(
    () => validateDurations([1, 2, 3, 4, 5, 721]),
    /水路时长超出允许范围/,
  );
});

test("只接受 MQTT 协议 v1 JSON 状态", () => {
  assert.deepEqual(
    parseJsonPayload(Buffer.from('{"v":1,"ready":true}'), "state"),
    { v: 1, ready: true },
  );
  assert.throws(
    () => parseJsonPayload(Buffer.from('{"v":2}'), "state"),
    ProtocolError,
  );
});
