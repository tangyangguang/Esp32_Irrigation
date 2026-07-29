import assert from "node:assert/strict";
import test from "node:test";
import { StateStore } from "../src/state-store.js";

test("汇总设备在线、状态、计划和结果", () => {
  const store = new StateStore("irrigation/device");
  store.setBrokerConnected(true);
  store.update("availability", "online");
  store.update("state", { v: 1, ready: true, revision: 3 });
  store.update("run", { v: 1, active: true, step: 1 });
  store.update("latest", { v: 1, found: true, record_id: 9 });
  store.update("plan/2", { v: 1, id: 2, configured: true });
  store.update("result", { v: 1, id: "one", ok: true });
  const value = store.snapshot;
  assert.equal(value.brokerConnected, true);
  assert.equal(value.availability, "online");
  assert.equal(value.state.revision, 3);
  assert.equal(value.run.step, 1);
  assert.equal(value.latest.record_id, 9);
  assert.equal(value.plans["2"].configured, true);
  assert.equal(value.lastResult.id, "one");
});

test("对外快照不能修改内部状态", () => {
  const store = new StateStore("irrigation/device");
  const value = store.snapshot;
  value.availability = "tampered";
  assert.equal(store.snapshot.availability, "unknown");
});
