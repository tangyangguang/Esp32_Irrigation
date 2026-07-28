import assert from "node:assert/strict";
import test from "node:test";
import {
  ProtocolError,
  createCommand,
  parseJsonPayload,
  validateDeviceDurations,
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

test("按设备当前上限校验时长并拒绝已禁用水路", () => {
  const meta = {
    maximum_zone_duration_minutes: 120,
    zones: [
      { id: 1, enabled: true },
      { id: 2, enabled: false },
      { id: 3, enabled: true },
      { id: 4, enabled: true },
      { id: 5, enabled: true },
      { id: 6, enabled: true },
    ],
  };
  assert.deepEqual(
    validateDeviceDurations([120, 0, 1, 0, 0, 0], meta),
    [120, 0, 1, 0, 0, 0],
  );
  assert.throws(
    () => validateDeviceDurations([121, 0, 0, 0, 0, 0], meta),
    /不能超过 120 分钟/,
  );
  assert.throws(
    () => validateDeviceDurations([0, 1, 0, 0, 0, 0], meta),
    /水路 2 已禁用/,
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
