const token = document.querySelector('meta[name="irrigation-token"]').content;
const snapshot = {
  brokerConnected: false,
  availability: "unknown",
  meta: null,
  state: null,
  run: null,
  latest: null,
  plans: {},
  lastResult: null,
  receivedAt: {},
};

const byId = (id) => document.getElementById(id);
const all = (selector, root = document) => [...root.querySelectorAll(selector)];
const manualDraft = new Map();
let manualZoneSignature = "";
let selectedTemplateId = 0;
let busy = false;
let clockAnchor = null;

function setText(id, value) {
  const element = byId(id);
  if (element) element.textContent = value ?? "—";
}

function setTag(id, text, tone) {
  setText(id, text);
  byId(id).className = `tag ${tone || "info"}`;
}

function enabledZones() {
  return (snapshot.meta?.zones || [])
    .filter((zone) => zone.enabled)
    .sort((a, b) => Number(a.id) - Number(b.id));
}

function maximumMinutes() {
  const value = Number(snapshot.meta?.maximum_zone_duration_minutes);
  return Number.isInteger(value) && value >= 1 ? value : 0;
}

function zoneName(id) {
  return snapshot.meta?.zones?.find((zone) => Number(zone.id) === Number(id))?.name
    || `水路 ${id}`;
}

function configuredPlans() {
  return Object.values(snapshot.plans)
    .filter((plan) => plan?.configured)
    .sort((a, b) => Number(a.id) - Number(b.id));
}

function planName(id, showDeleted = false) {
  const plan = snapshot.plans?.[id];
  if (plan?.configured) return plan.name || `计划 ${id}`;
  return id ? `计划 ${id}${showDeleted ? "（已删除）" : ""}` : "";
}

function shanghaiParts(epoch) {
  const date = new Date((Number(epoch) + 8 * 3600) * 1000);
  return {
    year: date.getUTCFullYear(),
    month: date.getUTCMonth() + 1,
    day: date.getUTCDate(),
    hour: date.getUTCHours(),
    minute: date.getUTCMinutes(),
    second: date.getUTCSeconds(),
  };
}

function pad(value) {
  return String(value).padStart(2, "0");
}

function formatFullDateTime(epoch) {
  if (!Number(epoch)) return "—";
  const value = shanghaiParts(epoch);
  return `${value.year}年${pad(value.month)}月${pad(value.day)}日 ${pad(value.hour)}:${pad(value.minute)}`;
}

function formatRecordTime(epoch) {
  if (!Number(epoch)) return "—";
  const value = shanghaiParts(epoch);
  return `${pad(value.month)}月${pad(value.day)}日 ${pad(value.hour)}:${pad(value.minute)}:${pad(value.second)}`;
}

function currentDeviceEpoch() {
  if (!clockAnchor) return 0;
  return clockAnchor.epoch + Math.floor((performance.now() - clockAnchor.started) / 1000);
}

function updateClockAnchor() {
  const epoch = Number(snapshot.state?.time?.epoch);
  if (snapshot.state?.time?.trusted !== true || !epoch) {
    clockAnchor = null;
    return;
  }
  const stateReceived = snapshot.receivedAt?.state || 0;
  if (!clockAnchor || clockAnchor.receivedAt !== stateReceived) {
    clockAnchor = { epoch, receivedAt: stateReceived, started: performance.now() };
  }
}

function renderClock() {
  const time = snapshot.state?.time || {};
  const epoch = currentDeviceEpoch();
  if (!time.trusted || !epoch) {
    setText("deviceClock", "尚未就绪");
    setText("deviceDate", "等待 RTC 或 NTP 提供可信时间");
  } else {
    const value = shanghaiParts(epoch);
    setText("deviceClock", `${pad(value.hour)}:${pad(value.minute)}:${pad(value.second)}`);
    setText(
      "deviceDate",
      `${value.year}年${value.month}月${value.day}日 · ${time.source === "ntp" ? "NTP 校时" : "RTC 时间"}`,
    );
  }
  byId("rtcWarning").classList.toggle("hidden", time.rtc_unavailable !== true);
}

function friendlyDateTime(epoch) {
  if (!Number(epoch)) return "—";
  const target = shanghaiParts(epoch);
  const now = shanghaiParts(currentDeviceEpoch() || Date.now() / 1000);
  const targetDay = Date.UTC(target.year, target.month - 1, target.day) / 86400000;
  const nowDay = Date.UTC(now.year, now.month - 1, now.day) / 86400000;
  const offset = targetDay - nowDay;
  const prefix = offset === 0 ? "今天" : offset === 1 ? "明天" : offset === 2 ? "后天" : `${target.month}月${target.day}日`;
  return `${prefix} ${pad(target.hour)}:${pad(target.minute)}`;
}

function formatElapsed(seconds) {
  const value = Math.max(0, Math.floor(Number(seconds) || 0));
  if (value < 60) return `${value} 秒`;
  if (value < 3600) return `${Math.floor(value / 60)} 分 ${value % 60} 秒`;
  return `${Math.floor(value / 3600)} 小时 ${Math.floor((value % 3600) / 60)} 分`;
}

function formatWater(ml) {
  return `${((Number(ml) || 0) / 1000).toFixed(3)} L`;
}

function formatCompactWater(ml) {
  const value = Math.max(0, Math.floor(Number(ml) || 0));
  if (value < 1000) return `${value} mL`;
  return `${(Math.round(value / 100) / 10).toFixed(1)} L`;
}

function formatFlow(mlPerMinute) {
  return `${((Number(mlPerMinute) || 0) / 1000).toFixed(3)} L/min`;
}

function minuteLabel(minute) {
  return `${pad(Math.floor(Number(minute) / 60))}:${pad(Number(minute) % 60)}`;
}

function sourceLabel(source, planId) {
  if (source === "automatic_plan") {
    return planId ? `自动计划 · ${planName(planId, true)}` : "自动计划";
  }
  if (source === "single_output") return "单次出水";
  return "手动浇水";
}

function stageLabel(state) {
  return {
    idle: "空闲",
    starting_zone: "区域启动中",
    waiting_for_flow: "等待水流",
    watering_zone: "正在浇水",
    stopping_zone: "区域停止中",
    switching_zone: "水路切换中",
  }[state] || "正在执行";
}

function readinessLabel(reason) {
  return {
    base_not_ready: "基础服务启动失败",
    filesystem_unavailable: "设备存储不可用",
    no_valid_config_copy: "灌溉配置需要重新建立",
    default_config_write_failed: "灌溉配置无法保存",
    config_recovery_write_failed: "灌溉配置无法保存",
    config_not_ready: "灌溉配置尚未就绪",
    config_write_failed: "灌溉配置无法保存",
    startup_check_failed: "灌溉功能未就绪",
    not_loaded: "灌溉配置尚未加载",
  }[reason] || "灌溉功能未就绪";
}

function commandResultLabel(result) {
  const labels = {
    ok: "操作成功", success: "操作成功", saved: "计划已保存", deleted: "计划已删除",
    paused: "自动浇水已暂停", resumed: "自动浇水已恢复", started: "浇水已开始",
    stopping: "正在停止浇水", already_idle: "设备已经空闲",
    duplicate_command: "重复命令已忽略", not_ready: "设备业务尚未就绪",
    busy: "设备正在执行其他任务", previous_result_pending: "上一任务结果正在处理",
    plan_not_found: "计划不存在", revision_conflict: "计划已被其他客户端修改",
    invalid_arguments: "命令参数无效", configuration_unavailable: "设备配置不可用",
    config_invalid: "计划配置校验失败", config_save_failed: "计划保存失败",
    hardware_failure: "硬件执行失败", maintenance_active: "设备正在校准或学习",
    pause_rejected: "暂停自动浇水失败", resume_rejected: "恢复自动浇水失败",
    stop_rejected: "停止浇水失败",
  };
  return labels[result?.code] || result?.code || "设备未返回明确结果";
}

function canControl() {
  return snapshot.brokerConnected
    && snapshot.availability === "online"
    && snapshot.state?.ready === true;
}

function blockingMessage() {
  if (!snapshot.brokerConnected) {
    return ["本机尚未连接 MQTT 服务器，请检查网络或连接配置。", "danger"];
  }
  if (snapshot.availability !== "online") {
    return ["浇水设备当前离线。页面保留最后一次收到的状态，但所有操作已锁定。", "danger"];
  }
  if (!snapshot.state) return ["设备已在线，正在读取业务状态……", "warn"];
  if (!snapshot.state.ready) {
    return [`${readinessLabel(snapshot.state.ready_reason)}。全部输出保持关闭，远程操作已锁定。`, "danger"];
  }
  return null;
}

function renderConnection() {
  const brokerOnline = snapshot.brokerConnected;
  const deviceOnline = snapshot.availability === "online";
  setText("brokerBadge", brokerOnline ? "MQTT 已连接" : "MQTT 未连接");
  setText("deviceBadge", deviceOnline ? "设备在线" : "设备离线");
  byId("brokerBadge").className = `status-pill ${brokerOnline ? "" : "muted"}`.trim();
  byId("deviceBadge").className = `status-pill ${deviceOnline ? "" : "muted"}`.trim();
  setText("footerBroker", `MQTT：${brokerOnline ? "已连接" : "未连接"}`);
  setText("footerDevice", `设备：${deviceOnline ? "在线" : "离线"}`);

  const blocking = blockingMessage();
  for (const id of ["blockingNotice", "plansBlockingNotice"]) {
    const notice = byId(id);
    notice.classList.toggle("hidden", !blocking);
    if (blocking) {
      notice.textContent = blocking[0];
      notice.className = `notice ${blocking[1]}`;
    }
  }
}

function heroState() {
  const state = snapshot.state || {};
  const watering = state.watering || {};
  const monitor = state.unexpected_flow || {};
  const faults = state.faults || {};
  const time = state.time || {};
  if (!state.ready) {
    return ["danger", readinessLabel(state.ready_reason), "全部输出已保持关闭。请先在设备本地页面处理存储、配置或启动故障。"];
  }
  if (monitor.alarm) {
    return ["danger", "关阀后水流异常", "水泵和全部阀门均已关闭，但仍检测到水流。请检查阀门、管路或流量计。"];
  }
  if (watering.active) {
    return ["active", sourceLabel(watering.source, watering.plan_id), `${zoneName(watering.zone_id)} · ${stageLabel(watering.state)}${watering.flow_established ? " · 水流正常" : " · 正在等待水流"}`];
  }
  if (faults.scheduler) {
    return ["danger", "自动浇水暂不可用", "调度状态无法可靠保存；手动浇水仍可使用。"];
  }
  if (state.automatic?.next_status === "rtc_rollback") {
    return ["warn", "设备时间异常，自动浇水已停止", "检测到 RTC 时间明显倒退，等待 NTP 校时后自动恢复判断。"];
  }
  if (!time.trusted) {
    return ["warn", "设备时间尚未就绪", "自动计划暂时不会运行，手动浇水仍可使用。"];
  }
  if (faults.watering_records || faults.events || faults.checkpoint) {
    return ["warn", "设备可以浇水，但部分数据存储异常", "请在设备本地查看系统状态；自动计划或历史记录可能受到影响。"];
  }
  return ["", "当前没有浇水", "自动计划会按设定时间运行，也可以随时手动开始。"];
}

function renderHero() {
  const [tone, title, detail] = heroState();
  const monitor = snapshot.state?.unexpected_flow || {};
  const hero = byId("statusHero");
  hero.className = `home-hero panel ${tone}`.trim();
  setText("wateringTitle", title);
  setText("wateringDetail", detail);
  if (monitor.alarm) {
    setText(
      "flowMonitor",
      `近 ${Math.max(1, Number(monitor.observed_s) || 1)} 秒检测到 ${Number(monitor.pulse_count) || 0} 个水流脉冲 · 估算平均流量 ${formatFlow(monitor.estimated_ml_min)}`,
    );
  } else {
    setText("flowMonitor", monitor.observation_ready ? "关阀后水流监测已开启" : "关阀后水流监测中");
  }
}

function renderRun() {
  const stateRun = snapshot.state?.watering || {};
  const run = snapshot.run?.active ? snapshot.run : stateRun;
  const active = stateRun.active === true;
  byId("runPanel").classList.toggle("hidden", !active);
  if (!active) return;

  setText("runTaskTitle", sourceLabel(run.source, run.plan_id));
  setText("runState", `${zoneName(run.zone_id)} · ${stageLabel(run.state)}`);
  setText("runElapsed", formatElapsed(run.elapsed_s));
  setText("runRemaining", formatElapsed(run.planned_remaining_s));
  setText("runWater", formatWater(run.water_ml));
  setText("runStepCount", `第 ${Number(run.step) || 1} / ${Number(run.step_count) || 1} 条水路`);
  setText("runCurrentZone", zoneName(run.zone_id));
  setText("runCurrentRemaining", `剩余 ${formatElapsed(run.zone_remaining_s ?? run.remaining_s)}`);

  const steps = Array.isArray(run.steps) ? run.steps : [];
  const currentIndex = Math.max(0, (Number(run.step) || 1) - 1);
  const current = steps[currentIndex] || {};
  const target = Number(current.target_ml) || Number(current.planned_s) || 0;
  const progressValue = Number(current.target_ml) ? Number(current.water_ml) : Number(run.zone_elapsed_s);
  const progress = target ? Math.min(100, Math.round(progressValue * 100 / target)) : 0;
  byId("runProgressBar").style.width = `${progress}%`;
  setText(
    "runCurrentElapsed",
    `实际浇水 ${formatElapsed(run.zone_elapsed_s)} / ${Number(current.target_ml) ? `目标 ${formatWater(current.target_ml)}` : formatElapsed(current.planned_s)}`,
  );
  setText("runFlow", formatFlow(run.flow_ml_min));
  setText("runExpectedFlow", Number(run.expected_flow_ml_min) ? formatFlow(run.expected_flow_ml_min) : "未设置");
  setText("runPulses", String(Number(run.pulse_count) || 0));
  setText("runZoneWater", formatWater(current.water_ml));

  let flowTone = "warn";
  let flowText = "等待水流";
  if (run.state === "switching_zone") {
    flowTone = "info";
    flowText = "水路切换中";
  } else if (run.flow_established && ["low", "both"].includes(current.flow_alert)) {
    flowText = "低流量";
  } else if (run.flow_established && ["high", "both"].includes(current.flow_alert)) {
    flowTone = "danger";
    flowText = "高流量";
  } else if (run.flow_established && !Number(run.expected_flow_ml_min)) {
    flowTone = "info";
    flowText = "水流已建立";
  } else if (run.flow_established) {
    flowTone = "info";
    flowText = "流量监测中";
  }
  setTag("runFlowState", flowText, flowTone);

  const container = byId("runSteps");
  container.replaceChildren();
  if (!steps.length) {
    const loading = document.createElement("p");
    loading.className = "muted";
    loading.textContent = "正在读取完整水路执行顺序……";
    container.append(loading);
    return;
  }
  steps.forEach((step, index) => {
    const complete = index < currentIndex;
    const isCurrent = index === currentIndex;
    const row = document.createElement("div");
    row.className = `run-step${complete ? " complete" : ""}${isCurrent ? " current" : ""}`;
    const icon = document.createElement("span");
    icon.className = "run-step-icon";
    icon.textContent = complete ? "✓" : String(index + 1);
    const main = document.createElement("div");
    const name = document.createElement("b");
    name.textContent = zoneName(step.zone_id);
    const goal = document.createElement("small");
    goal.textContent = Number(step.target_ml) ? `目标 ${formatWater(step.target_ml)}` : `计划 ${formatElapsed(step.planned_s)}`;
    main.append(name, goal);
    const detail = document.createElement("span");
    detail.className = "run-step-detail";
    if (complete) detail.textContent = `实际 ${formatElapsed(step.actual_s)} · ${formatWater(step.water_ml)}`;
    else if (isCurrent && run.state === "switching_zone") detail.textContent = "等待开阀";
    else if (isCurrent && Number(step.target_ml)) detail.textContent = `正在执行 · 已出 ${formatWater(step.water_ml)}`;
    else if (isCurrent) detail.textContent = `正在执行 · 剩余 ${formatElapsed(run.zone_remaining_s ?? run.remaining_s)}`;
    else detail.textContent = "等待执行";
    row.append(icon, main, detail);
    container.append(row);
  });
}

function planDurations(plan) {
  return enabledZones()
    .map((zone) => {
      const minutes = Number(plan?.durations?.[Number(zone.id) - 1]) || 0;
      return minutes ? { zone, minutes } : null;
    })
    .filter(Boolean);
}

function renderNextAutomatic() {
  const automatic = snapshot.state?.automatic || {};
  const paused = automatic.mode === "paused_indefinitely" || automatic.mode === "paused_until";
  setTag("nextStatusTag", paused ? "自动浇水已暂停" : "自动浇水正常", paused ? "warn" : "ok");
  const content = byId("nextContent");
  const empty = byId("nextEmpty");
  content.classList.add("hidden");
  empty.classList.add("hidden");

  if (automatic.mode === "paused_indefinitely") {
    empty.textContent = "等待你手动恢复。暂停期间到点的计划不会执行，也不会补执行。";
    empty.classList.remove("hidden");
    return;
  }

  const nextAvailable = automatic.next_status === "available" && Number(automatic.next_plan_id);
  if (!nextAvailable && automatic.mode !== "paused_until") {
    const emptyMessages = {
      no_enabled_plans: "还没有开启自动执行的计划。设置计划和启动时间后，下一次浇水会显示在这里。",
      rtc_rollback: "设备时间发生倒退，暂时无法计算下一次浇水。",
      time_unavailable: "设备时间尚未就绪，暂时无法计算下一次浇水。",
    };
    empty.textContent = emptyMessages[automatic.next_status] || "正在计算下一次自动浇水。";
    empty.classList.remove("hidden");
    return;
  }

  content.classList.remove("hidden");
  if (automatic.mode === "paused_until") {
    setText("nextPlanTitle", `将在${friendlyDateTime(automatic.resume_at)}自动恢复`);
    setText("nextPlanDate", `${formatFullDateTime(automatic.resume_at)}${snapshot.state?.time?.trusted ? "" : "；设备时间恢复可信后才会判断是否到期"}`);
  } else {
    setText("nextPlanTitle", friendlyDateTime(automatic.next_at));
    setText("nextPlanDate", formatFullDateTime(automatic.next_at));
  }
  setText("nextPlanLabel", paused ? "恢复后的计划" : "执行计划");
  setText("nextPlanName", planName(automatic.next_plan_id));
  const plan = snapshot.plans?.[automatic.next_plan_id];
  const zones = planDurations(plan);
  const total = zones.reduce((sum, item) => sum + item.minutes, 0);
  setText("nextPlanSummary", `${zones.length} 个水路 · 预计 ${total} 分钟`);
  const first = zones.slice(0, 3).map(({ zone, minutes }) => `${zone.name} ${minutes} 分`);
  if (zones.length > 3) first.push(`另有 ${zones.length - 3} 个水路`);
  setText("nextPlanZones", first.join(" · ") || "没有可执行水路");
}

function latestOutcome(record) {
  const completed = Number(record.completed_zones) || 0;
  const planned = Number(record.planned_zones) || 0;
  const started = Number(record.started_zones) || 0;
  const affected = Number(record.affected_zone_id) || 0;
  let text = "";
  if (record.result === "completed") {
    text = planned > 1
      ? `${completed}/${planned} 条水路完成${record.flow_alert ? "，执行期间检测到流量报警" : "，均按计划结束"}`
      : record.flow_alert ? "浇水已完成，但执行期间检测到流量报警" : "按计划完成";
  } else {
    const reason = {
      user_stopped: "由用户停止",
      flow_start_timeout: affected ? `${zoneName(affected)}启动后未检测到水流` : "启动后未检测到水流",
      no_flow_timeout: affected ? `${zoneName(affected)}浇水过程中水流中断` : "浇水过程中水流中断",
      low_flow: affected ? `${zoneName(affected)}检测到水流过低` : "检测到水流过低",
      high_flow: affected ? `${zoneName(affected)}检测到水流过高` : "检测到水流过高",
      learning_timeout: "流量学习超过最长时间",
      hardware_failure: "硬件执行失败",
      maintenance_interrupted: "维护操作中断了浇水",
      target_volume_timeout: affected ? `${zoneName(affected)}达到最长运行时间仍未完成目标水量` : "达到最长运行时间仍未完成目标水量",
    };
    text = reason[record.stop_reason] || "浇水任务未正常完成";
    const remaining = Math.max(0, planned - started);
    if (remaining) text += `，后续 ${remaining} 个水路未执行`;
    else if (record.result === "failed") text += "，整次任务已安全停止";
  }
  return text;
}

function renderLatest() {
  const record = snapshot.latest;
  const card = byId("latestCard");
  card.className = "home-card panel";
  byId("latestContent").classList.add("hidden");
  byId("latestEmpty").classList.remove("hidden");
  if (!record || record.available === false) {
    setTag("latestTag", "读取异常", "danger");
    setText("latestEmpty", "浇水记录存储异常，暂时无法读取最近记录。");
    card.classList.add("danger");
    return;
  }
  if (!record.found) {
    setTag("latestTag", "暂无记录", "info");
    setText("latestEmpty", "还没有浇水记录。第一次浇水执行结束后，无论完成、停止或失败，结果都会显示在这里。");
    return;
  }
  byId("latestContent").classList.remove("hidden");
  byId("latestEmpty").classList.add("hidden");
  const outcome = record.result === "failed"
    ? ["失败", "danger"]
    : record.result === "stopped"
      ? ["已停止", "warn"]
      : record.flow_alert ? ["完成但有报警", "warn"] : ["已完成", "ok"];
  setTag("latestTag", outcome[0], outcome[1]);
  card.classList.add(outcome[1]);
  setText("latestSource", sourceLabel(record.source, record.plan_id));
  setText("latestTime", `${formatRecordTime(record.started_at)}–${formatRecordTime(record.completed_at).split(" ").at(-1)}`);
  setText("latestOutcome", latestOutcome(record));
  setText("latestTarget", Number(record.target_ml) ? formatCompactWater(record.target_ml) : formatElapsed(record.planned_s));
  setText("latestActual", formatElapsed(record.actual_s));
  setText("latestWater", formatCompactWater(record.water_ml));
}

function renderAutomaticControl() {
  const automatic = snapshot.state?.automatic || {};
  const paused = automatic.mode === "paused_indefinitely" || automatic.mode === "paused_until";
  const card = byId("automaticStateCard");
  card.className = `automatic-state ${paused ? "warn" : "ok"}`;
  setTag("automaticTag", paused ? "自动浇水已暂停" : "自动浇水正常运行", paused ? "warn" : "ok");
  if (automatic.mode === "paused_indefinitely") {
    setText("automaticDetail", "已暂停，等待手动恢复；手动浇水不受影响。");
  } else if (automatic.mode === "paused_until") {
    setText("automaticDetail", `已暂停，将在 ${formatFullDateTime(automatic.resume_at)} 自动恢复；手动浇水不受影响。`);
  } else {
    setText("automaticDetail", "已启用的计划会在设定时间自动执行。");
  }
  byId("openPauseButton").hidden = paused;
  byId("resumeButton").hidden = !paused;
}

function actionButton(label, listener) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "button secondary compact";
  button.textContent = label;
  button.addEventListener("click", listener);
  return button;
}

function renderPlans() {
  const grid = byId("planGrid");
  grid.replaceChildren();
  const plans = configuredPlans();
  if (!plans.length) {
    const empty = document.createElement("section");
    empty.className = "empty-state";
    empty.innerHTML = "<h3>还没有计划</h3><p>新增一个计划后，可按时间自动浇水，也可作为手动浇水模板。</p>";
    grid.append(empty);
    return;
  }
  for (const plan of plans) {
    const card = document.createElement("article");
    card.className = `plan-card${plan.enabled ? "" : " disabled"}`;
    const head = document.createElement("div");
    head.className = "plan-card-head";
    const titleArea = document.createElement("div");
    const titleLine = document.createElement("div");
    titleLine.className = "plan-title-line";
    const title = document.createElement("h3");
    title.textContent = `计划 ${plan.id}`;
    const status = document.createElement("span");
    status.className = `tag ${plan.enabled ? "ok" : "info"}`;
    status.textContent = plan.enabled ? "自动执行已开启" : "自动执行已关闭";
    titleLine.append(title, status);
    const name = document.createElement("p");
    name.className = "plan-name";
    name.textContent = plan.name || `计划 ${plan.id}`;
    titleArea.append(titleLine, name);
    head.append(titleArea, actionButton("编辑", () => openPlan(plan)));

    const details = document.createElement("div");
    details.className = "plan-details";
    const schedule = document.createElement("div");
    const scheduleLabel = document.createElement("span");
    scheduleLabel.className = "plan-detail-label";
    scheduleLabel.textContent = "每日启动时间";
    const times = document.createElement("div");
    times.className = "time-pills";
    const starts = Array.isArray(plan.starts) ? plan.starts : [];
    if (starts.length) {
      starts.forEach((start) => {
        const pill = document.createElement("span");
        pill.className = "time-pill";
        pill.textContent = minuteLabel(start);
        times.append(pill);
      });
    } else {
      times.textContent = "没有设置开始时间";
      times.classList.add("muted");
    }
    schedule.append(scheduleLabel, times);

    const zoneArea = document.createElement("div");
    const zoneLabel = document.createElement("span");
    zoneLabel.className = "plan-detail-label";
    zoneLabel.textContent = "各水路浇水时长";
    const zoneList = document.createElement("div");
    zoneList.className = "zone-duration-list";
    const durations = planDurations(plan);
    if (durations.length) {
      durations.forEach(({ zone, minutes }) => {
        const item = document.createElement("div");
        item.className = "zone-duration";
        const zoneText = document.createElement("span");
        zoneText.textContent = zone.name;
        const minutesText = document.createElement("b");
        minutesText.textContent = `${minutes} 分钟`;
        item.append(zoneText, minutesText);
        zoneList.append(item);
      });
    } else {
      zoneList.textContent = "没有可执行水路";
      zoneList.classList.add("muted");
    }
    zoneArea.append(zoneLabel, zoneList);
    details.append(schedule, zoneArea);
    card.append(head, details);
    grid.append(card);
  }
}

function updateManualSummary() {
  const selected = enabledZones()
    .map((zone) => [zone, Number(manualDraft.get(Number(zone.id))) || 0])
    .filter(([, minutes]) => minutes > 0);
  const total = selected.reduce((sum, [, minutes]) => sum + minutes, 0);
  setText("manualSummary", selected.length ? `已选择 ${selected.length} 条水路 · 合计 ${total} 分钟` : "尚未选择水路");
  setText(
    "manualNote",
    selectedTemplateId
      ? `已从“${planName(selectedTemplateId)}”填入，可继续修改；本次修改不会保存到计划。`
      : `每条水路范围 0～${maximumMinutes() || "—"} 分钟，0 表示本次不执行。`,
  );
  byId("startManualButton").disabled =
    !canControl() || busy || snapshot.state?.watering?.active || !selected.length;
}

function setManualValue(zoneId, value, manuallyAdjusted = false) {
  manualDraft.set(Number(zoneId), Number(value) || 0);
  const input = byId(`manualZone${zoneId}`);
  if (input && String(input.value) !== String(value)) input.value = String(value);
  if (manuallyAdjusted) selectedTemplateId = 0;
  all("[data-template-id]").forEach((button) => {
    button.classList.toggle("selected", Number(button.dataset.templateId) === selectedTemplateId);
  });
  updateManualSummary();
}

function applyTemplate(plan) {
  selectedTemplateId = Number(plan.id);
  for (const zone of enabledZones()) {
    setManualValue(zone.id, plan.durations?.[Number(zone.id) - 1] || 0);
  }
  all("[data-template-id]").forEach((button) => {
    button.classList.toggle("selected", Number(button.dataset.templateId) === selectedTemplateId);
  });
  updateManualSummary();
}

function renderManualTemplates() {
  const container = byId("manualTemplates");
  container.replaceChildren();
  const plans = configuredPlans();
  if (!plans.length) {
    const empty = document.createElement("p");
    empty.className = "home-empty";
    empty.textContent = "还没有可用计划，可以直接设置下方各水路时长。";
    container.append(empty);
    return;
  }
  for (const plan of plans) {
    const durations = planDurations(plan);
    const total = durations.reduce((sum, item) => sum + item.minutes, 0);
    const button = document.createElement("button");
    button.type = "button";
    button.className = `template-card${Number(plan.id) === selectedTemplateId ? " selected" : ""}`;
    button.dataset.templateId = String(plan.id);
    button.disabled = !durations.length;
    const title = document.createElement("b");
    title.textContent = plan.name || `计划 ${plan.id}`;
    const detail = document.createElement("span");
    detail.textContent = durations.map(({ zone, minutes }) => `${zone.name} ${minutes}分`).join(" · ") || "当前无可执行水路";
    const summary = document.createElement("small");
    summary.textContent = `${durations.length} 路 · 共 ${total} 分钟`;
    button.append(title, detail, summary);
    button.addEventListener("click", () => applyTemplate(plan));
    container.append(button);
  }
}

function renderManualInputs() {
  const signature = `${maximumMinutes()}:${enabledZones().map((zone) => `${zone.id}:${zone.name}`).join("|")}`;
  if (signature === manualZoneSignature) return;
  manualZoneSignature = signature;
  const container = byId("manualDurations");
  container.replaceChildren();
  for (const zone of enabledZones()) {
    if (!manualDraft.has(Number(zone.id))) manualDraft.set(Number(zone.id), 0);
    const label = document.createElement("label");
    const name = document.createElement("span");
    name.textContent = zone.name;
    const input = document.createElement("input");
    input.id = `manualZone${zone.id}`;
    input.type = "number";
    input.inputMode = "numeric";
    input.min = "0";
    input.max = String(maximumMinutes());
    input.value = String(manualDraft.get(Number(zone.id)) || 0);
    input.addEventListener("input", () => setManualValue(zone.id, input.value, true));
    const unit = document.createElement("span");
    unit.className = "unit";
    unit.textContent = "分钟";
    label.append(name, input, unit);
    container.append(label);
  }
}

function renderControls() {
  const ready = canControl();
  const active = snapshot.state?.watering?.active === true;
  const automatic = snapshot.state?.automatic || {};
  const paused = automatic.mode === "paused_indefinitely" || automatic.mode === "paused_until";
  byId("stopButton").hidden = !active;
  byId("manualOpenButton").hidden = active;
  byId("stopButton").disabled = !ready || !active || busy;
  byId("manualOpenButton").disabled = !ready || active || busy || !enabledZones().length || !maximumMinutes();
  byId("openPauseButton").disabled = !ready || paused || busy;
  byId("resumeButton").disabled = !ready || !paused || busy;
  byId("savePlanButton").disabled = !ready || busy;
  byId("deletePlanButton").disabled = !ready || busy;
  const firstEmpty = Array.from({ length: 8 }, (_, index) => index + 1)
    .find((id) => !snapshot.plans[id]?.configured);
  byId("addPlanButton").disabled = !ready || busy || !firstEmpty;
  byId("addPlanButton").textContent = firstEmpty ? "新增计划" : "计划已满";
  updateManualSummary();
}

function render() {
  updateClockAnchor();
  renderConnection();
  renderClock();
  renderHero();
  renderRun();
  renderNextAutomatic();
  renderLatest();
  renderAutomaticControl();
  renderPlans();
  renderManualInputs();
  renderManualTemplates();
  renderControls();
}

function toast(message, error = false) {
  const box = byId("toast");
  box.textContent = message;
  box.className = `toast show ${error ? "error" : ""}`;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => box.className = "toast", 3600);
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: {
      "content-type": "application/json",
      "x-irrigation-token": token,
      ...(options.headers || {}),
    },
  });
  const value = await response.json().catch(() => ({}));
  if (!response.ok || value.ok === false) {
    throw new Error(value.error || commandResultLabel(value.result));
  }
  return value;
}

async function mutate(path, body, message) {
  busy = true;
  renderControls();
  try {
    const value = await api(path, { method: "POST", body: JSON.stringify(body) });
    snapshot.lastResult = value.result;
    toast(message || commandResultLabel(value.result));
    return true;
  } catch (error) {
    toast(error.message, true);
    return false;
  } finally {
    busy = false;
    render();
  }
}

function switchView(name) {
  all(".nav-item").forEach((item) => item.classList.toggle("active", item.dataset.view === name));
  byId("overviewView").classList.toggle("hidden", name !== "overview");
  byId("plansView").classList.toggle("hidden", name !== "plans");
}

function openManual() {
  selectedTemplateId = 0;
  for (const zone of enabledZones()) setManualValue(zone.id, 0);
  renderManualTemplates();
  updateManualSummary();
  byId("manualDialog").showModal();
}

function buildPlanDurationInputs(plan) {
  const container = byId("planDurations");
  container.replaceChildren();
  for (const zone of enabledZones()) {
    const label = document.createElement("label");
    const name = document.createElement("span");
    name.textContent = zone.name;
    const input = document.createElement("input");
    input.type = "number";
    input.inputMode = "numeric";
    input.min = "0";
    input.max = String(maximumMinutes());
    input.value = String(plan.durations?.[Number(zone.id) - 1] || 0);
    input.className = "plan-duration";
    input.dataset.zoneId = String(zone.id);
    const unit = document.createElement("span");
    unit.className = "unit";
    unit.textContent = "分钟";
    label.append(name, input, unit);
    container.append(label);
  }
  setText("planLimitNote", `每条水路范围 0～${maximumMinutes()} 分钟，0 表示该计划不执行此水路。`);
}

function openPlan(plan) {
  byId("planId").value = plan.id;
  byId("planRevision").value = snapshot.state?.revision || 0;
  setText("planDialogTitle", plan.configured ? `编辑“${plan.name || `计划 ${plan.id}`}”` : "新增计划");
  byId("planName").value = plan.name || `计划 ${plan.id}`;
  byId("planEnabled").checked = plan.enabled ?? true;
  all(".plan-start").forEach((input, index) => {
    const minute = plan.starts?.[index];
    input.value = minute == null ? "" : minuteLabel(minute);
  });
  buildPlanDurationInputs(plan);
  byId("deletePlanButton").hidden = !plan.configured;
  byId("planDialog").showModal();
}

function timeToMinute(value) {
  const [hour, minute] = value.split(":").map(Number);
  return hour * 60 + minute;
}

function resumeEpoch(value) {
  const match = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$/.exec(value);
  if (!match) throw new Error("请选择恢复日期和时间");
  const [, year, month, day, hour, minute] = match.map(Number);
  return Math.floor(Date.UTC(year, month - 1, day, hour - 8, minute) / 1000);
}

function shanghaiDateTime(dayOffset) {
  const now = shanghaiParts(currentDeviceEpoch() || Date.now() / 1000);
  const date = new Date(Date.UTC(now.year, now.month - 1, now.day + dayOffset));
  return `${date.getUTCFullYear()}-${pad(date.getUTCMonth() + 1)}-${pad(date.getUTCDate())}T06:00`;
}

function openPauseDialog() {
  byId("pauseHours").value = "24";
  byId("pauseUntil").value = "";
  const trusted = snapshot.state?.time?.trusted === true;
  byId("pauseHours").disabled = !trusted;
  byId("pauseUntil").disabled = !trusted;
  byId("confirmTimedPauseButton").disabled = !trusted;
  all("[data-pause-hours], [data-resume-day]").forEach((button) => button.disabled = !trusted);
  byId("pauseTimeWarning").classList.toggle("hidden", trusted);
  byId("pauseTimeWarning").textContent = "设备时间尚未就绪，不能可靠设置定时恢复；仍可选择无限期暂停。";
  byId("pauseDialog").showModal();
}

function initializeTimeInputs() {
  for (let index = 0; index < 4; index += 1) {
    const wrapper = document.createElement("div");
    wrapper.className = "time-entry";
    const label = document.createElement("label");
    label.textContent = `时间 ${index + 1}`;
    const input = document.createElement("input");
    input.type = "time";
    input.className = "plan-start";
    const clear = document.createElement("button");
    clear.type = "button";
    clear.className = "clear-time";
    clear.textContent = "清除";
    clear.addEventListener("click", () => input.value = "");
    label.append(input);
    wrapper.append(label, clear);
    byId("planStarts").append(wrapper);
  }
}

function bindEvents() {
  all(".nav-item").forEach((item) => item.addEventListener("click", () => switchView(item.dataset.view)));
  all("[data-go-view]").forEach((button) => button.addEventListener("click", () => {
    const dialogId = button.dataset.closeDialog;
    if (dialogId) byId(dialogId).close();
    switchView(button.dataset.goView);
  }));

  byId("stopButton").addEventListener("click", async () => {
    if (!window.confirm("确认停止当前整次浇水任务？")) return;
    await mutate("/api/watering/stop", {}, "正在停止浇水");
  });
  byId("manualOpenButton").addEventListener("click", openManual);
  byId("cancelManualDialog").addEventListener("click", () => byId("manualDialog").close());
  byId("clearManualButton").addEventListener("click", () => {
    selectedTemplateId = 0;
    for (const zone of enabledZones()) setManualValue(zone.id, 0);
    setText("manualNote", "已全部清零。");
    renderManualTemplates();
  });
  byId("manualForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    const durations = Array(6).fill(0);
    const selected = [];
    for (const zone of enabledZones()) {
      const minutes = Number(manualDraft.get(Number(zone.id))) || 0;
      durations[Number(zone.id) - 1] = minutes;
      if (minutes) selected.push([zone, minutes]);
    }
    if (!selected.length) return toast("请至少为一条水路设置大于 0 的时长。", true);
    const total = selected.reduce((sum, [, minutes]) => sum + minutes, 0);
    if (!window.confirm(`确认手动浇水 ${selected.length} 条水路，合计 ${total} 分钟？`)) return;
    if (await mutate("/api/watering/start-manual", { durations }, "手动浇水已开始")) {
      byId("manualDialog").close();
    }
  });

  byId("openPauseButton").addEventListener("click", openPauseDialog);
  byId("resumeButton").addEventListener("click", async () => {
    if (!window.confirm("确认恢复自动浇水？已启用的计划将重新按设定时间执行。")) return;
    await mutate("/api/automatic/resume", {}, "自动浇水已恢复");
  });
  byId("closePauseDialog").addEventListener("click", () => byId("pauseDialog").close());
  byId("pauseHours").addEventListener("input", () => byId("pauseUntil").value = "");
  byId("pauseUntil").addEventListener("input", () => byId("pauseHours").value = "");
  all("[data-pause-hours]").forEach((button) => button.addEventListener("click", () => {
    byId("pauseHours").value = button.dataset.pauseHours;
    byId("pauseUntil").value = "";
  }));
  all("[data-resume-day]").forEach((button) => button.addEventListener("click", () => {
    byId("pauseUntil").value = shanghaiDateTime(Number(button.dataset.resumeDay));
    byId("pauseHours").value = "";
  }));
  byId("pauseForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    try {
      const resumeAt = byId("pauseUntil").value
        ? resumeEpoch(byId("pauseUntil").value)
        : Math.floor(currentDeviceEpoch() || Date.now() / 1000) + Number(byId("pauseHours").value) * 3600;
      if (!Number.isFinite(resumeAt) || resumeAt <= (currentDeviceEpoch() || Date.now() / 1000)) {
        throw new Error("恢复时间必须晚于设备当前时间");
      }
      if (!window.confirm(`确认暂停自动浇水，并在 ${formatFullDateTime(resumeAt)} 自动恢复？`)) return;
      if (await mutate("/api/automatic/pause", { resumeAt }, "定时暂停已设置")) byId("pauseDialog").close();
    } catch (error) {
      toast(error.message, true);
    }
  });
  byId("indefinitePauseButton").addEventListener("click", async () => {
    if (!window.confirm("确认无限期暂停自动浇水？暂停期间计划不会执行，直到你手动恢复。")) return;
    if (await mutate("/api/automatic/pause", { resumeAt: 0 }, "自动浇水已无限期暂停")) byId("pauseDialog").close();
  });

  byId("addPlanButton").addEventListener("click", () => {
    const id = Array.from({ length: 8 }, (_, index) => index + 1)
      .find((value) => !snapshot.plans[value]?.configured);
    if (id) openPlan({ id, configured: false, enabled: true, starts: [], durations: Array(6).fill(0) });
  });
  byId("closePlanDialog").addEventListener("click", () => byId("planDialog").close());
  byId("cancelPlanDialog").addEventListener("click", () => byId("planDialog").close());
  byId("planForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    const id = Number(byId("planId").value);
    const name = byId("planName").value.trim();
    if (!name) return toast("请输入计划名称", true);
    if (new TextEncoder().encode(name).length > 48) return toast("计划名称最多 48 个 UTF-8 字节", true);
    const durations = Array(6).fill(0);
    all(".plan-duration").forEach((input) => {
      durations[Number(input.dataset.zoneId) - 1] = Number(input.value || 0);
    });
    busy = true;
    renderControls();
    try {
      const value = await api(`/api/plans/${id}`, {
        method: "PUT",
        body: JSON.stringify({
          expectedRevision: Number(byId("planRevision").value),
          name,
          enabled: byId("planEnabled").checked,
          starts: all(".plan-start").map((input) => input.value).filter(Boolean).map(timeToMinute),
          durations,
        }),
      });
      snapshot.lastResult = value.result;
      byId("planDialog").close();
      toast("计划已保存");
    } catch (error) {
      toast(error.message, true);
    } finally {
      busy = false;
      render();
    }
  });
  byId("deletePlanButton").addEventListener("click", async () => {
    const id = Number(byId("planId").value);
    if (!window.confirm(`确认删除“${planName(id)}”？删除后不能恢复。`)) return;
    busy = true;
    renderControls();
    try {
      const value = await api(`/api/plans/${id}`, {
        method: "DELETE",
        body: JSON.stringify({ expectedRevision: Number(byId("planRevision").value) }),
      });
      snapshot.lastResult = value.result;
      byId("planDialog").close();
      toast("计划已删除");
    } catch (error) {
      toast(error.message, true);
    } finally {
      busy = false;
      render();
    }
  });
}

async function connect() {
  try {
    const initial = await api("/api/status");
    Object.assign(snapshot, initial.snapshot);
    render();
  } catch (error) {
    toast(error.message, true);
  }
  const events = new EventSource(`/api/events?token=${encodeURIComponent(token)}`);
  events.addEventListener("snapshot", (event) => {
    Object.assign(snapshot, JSON.parse(event.data));
    render();
  });
  events.onerror = () => {
    setText("brokerBadge", "连接本机服务中");
    byId("brokerBadge").className = "status-pill muted";
  };
}

initializeTimeInputs();
bindEvents();
render();
connect();
setInterval(renderClock, 1000);
