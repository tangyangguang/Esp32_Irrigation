const token = document.querySelector('meta[name="irrigation-token"]').content;
const snapshot = {
  brokerConnected: false,
  availability: "unknown",
  meta: null,
  state: null,
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

function setText(id, value) {
  byId(id).textContent = value ?? "—";
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

function fixedTime(epoch, includeYear = false) {
  if (!epoch) return "—";
  return new Intl.DateTimeFormat("zh-CN", {
    timeZone: "Asia/Shanghai",
    ...(includeYear ? { year: "numeric" } : {}),
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  }).format(new Date(Number(epoch) * 1000));
}

function clock(seconds) {
  const value = Math.max(0, Number(seconds) || 0);
  const hours = Math.floor(value / 3600);
  const minutes = Math.floor((value % 3600) / 60);
  const rest = Math.floor(value % 60);
  return hours > 0
    ? `${hours}:${String(minutes).padStart(2, "0")}:${String(rest).padStart(2, "0")}`
    : `${minutes}:${String(rest).padStart(2, "0")}`;
}

function minuteLabel(minute) {
  return `${String(Math.floor(minute / 60)).padStart(2, "0")}:${String(minute % 60).padStart(2, "0")}`;
}

function wateringSourceLabel(source, planId) {
  if (source === "manual") {
    return planId ? `手动执行计划 ${planId}` : "手动浇水";
  }
  return {
    automatic_plan: `自动计划 ${planId || ""}`.trim(),
    single_output: "单次出水",
  }[source] || "浇水任务";
}

function wateringStageLabel(state) {
  return {
    idle: "空闲",
    starting_zone: "正在开启水路",
    waiting_for_flow: "正在等待水流",
    watering_zone: "正在浇水",
    stopping_zone: "正在关闭水路",
    switching_zone: "正在切换水路",
    completed: "任务已完成",
  }[state] || "正在执行";
}

function resultLabel(result) {
  if (!result) return "暂无远程控制命令";
  const labels = {
    ok: "操作成功",
    success: "操作成功",
    saved: "计划已保存",
    deleted: "计划已删除",
    paused: "自动计划已暂停",
    resumed: "自动计划已恢复",
    started: "浇水已开始",
    stopping: "正在停止浇水",
    already_idle: "设备已经空闲",
    duplicate_command: "重复命令已忽略",
    not_ready: "设备业务尚未就绪",
    busy: "设备正在执行其他任务",
    previous_result_pending: "上一任务结果正在处理",
    plan_not_found: "计划不存在",
    revision_conflict: "计划已被其他客户端修改",
    invalid_arguments: "命令参数无效",
    configuration_unavailable: "设备配置不可用",
    config_invalid: "计划配置校验失败",
    config_save_failed: "计划保存失败",
    hardware_failure: "硬件执行失败",
    maintenance_active: "设备正在校准或学习，不能远程停止",
    pause_rejected: "暂停自动计划失败",
    resume_rejected: "恢复自动计划失败",
    stop_rejected: "停止浇水失败",
  };
  return `${result.ok ? "成功" : "失败"}：${labels[result.code] || result.code || "设备已响应"}`;
}

function readinessLabel(reason) {
  return {
    base_not_ready: "基础服务启动失败",
    filesystem_unavailable: "设备文件系统不可用",
    no_valid_config_copy: "灌溉配置版本不兼容或配置副本损坏",
    default_config_write_failed: "默认灌溉配置创建失败",
    config_recovery_write_failed: "灌溉配置恢复写入失败",
    config_not_ready: "灌溉配置尚未就绪",
    config_write_failed: "灌溉配置保存失败",
    startup_check_failed: "设备启动检查失败",
    not_loaded: "灌溉配置尚未加载",
  }[reason] || (reason && reason !== "none" ? `设备启动错误：${reason}` : "设备启动检查未完成");
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
    return ["浇水设备当前离线，控制操作已锁定。", "danger"];
  }
  if (!snapshot.state) {
    return ["设备已在线，正在读取业务状态……", "warn"];
  }
  if (!snapshot.state.ready) {
    return [
      `${readinessLabel(snapshot.state.ready_reason)}。设备仍保持安全关阀，远程控制已锁定。`,
      "warn",
    ];
  }
  if (snapshot.state.faults?.unexpected_flow) {
    return [
      "检测到关阀后仍有水流。请检查阀门、管路或流量计；远程页面会持续显示监测信息。",
      "danger",
    ];
  }
  if (snapshot.state.watering?.active) {
    return ["设备正在浇水，可以随时停止整次任务。", ""];
  }
  return ["设备在线且业务已就绪，可以远程控制。", ""];
}

function renderConnection() {
  const broker = byId("brokerBadge");
  const device = byId("deviceBadge");
  broker.textContent = snapshot.brokerConnected ? "MQTT 已连接" : "MQTT 未连接";
  broker.className = `badge ${snapshot.brokerConnected ? "" : "muted"}`;
  device.textContent = snapshot.availability === "online" ? "设备在线" : "设备离线";
  device.className = `badge ${snapshot.availability === "online" ? "" : "muted"}`;

  const [message, tone] = blockingMessage();
  for (const id of ["blockingNotice", "plansBlockingNotice"]) {
    const notice = byId(id);
    notice.textContent = message;
    notice.className = `notice ${tone}`.trim();
  }
}

function renderWatering() {
  const watering = snapshot.state?.watering || {};
  const monitor = snapshot.state?.unexpected_flow || {};
  const hero = byId("statusHero");
  hero.classList.toggle("danger-panel", monitor.alarm === true);
  hero.classList.toggle("active-panel", watering.active === true && !monitor.alarm);

  if (monitor.alarm) {
    setText("wateringTitle", "关阀后水流异常");
    setText("wateringDetail", "水泵和全部阀门均已关闭，但仍检测到水流。请检查阀门、管路或流量计。");
  } else if (watering.active) {
    setText("wateringTitle", wateringStageLabel(watering.state));
    setText(
      "wateringDetail",
      `${wateringSourceLabel(watering.source, watering.plan_id)} · ${zoneName(watering.zone_id)}`,
    );
  } else {
    setText("wateringTitle", "当前空闲");
    setText("wateringDetail", "没有水路正在运行");
  }

  if (monitor.alarm) {
    setText(
      "flowMonitor",
      `近 ${Math.max(1, Number(monitor.observed_s) || 1)} 秒检测到 ${Number(monitor.pulse_count) || 0} 个水流脉冲 · 估算平均流量 ${((Number(monitor.estimated_ml_min) || 0) / 1000).toFixed(3)} L/min`,
    );
  } else {
    setText(
      "flowMonitor",
      monitor.observation_ready ? "关阀后水流监测已开启" : "关阀后水流监测中",
    );
  }

  setText("activeZone", watering.active ? zoneName(watering.zone_id) : "—");
  setText(
    "stepProgress",
    watering.active && watering.step_count
      ? `第 ${watering.step || 1} / ${watering.step_count} 条水路`
      : "没有运行任务",
  );
  setText("remaining", watering.active ? clock(watering.remaining_s) : "—");
  setText("zoneElapsed", watering.active ? `已运行 ${clock(watering.zone_elapsed_s)}` : "—");
  setText("flow", watering.active ? `${Number(watering.flow_ml_min || 0)} mL/min` : "—");
  setText(
    "expectedFlow",
    watering.active
      ? watering.flow_established
        ? `水流已建立 · 预期 ${Number(watering.expected_flow_ml_min || 0)} mL/min`
        : "正在等待水流建立"
      : "—",
  );
  setText("water", watering.active ? `${(Number(watering.water_ml || 0) / 1000).toFixed(2)} L` : "—");
  setText(
    "taskElapsed",
    watering.active
      ? `任务已运行 ${clock(watering.elapsed_s)} · 计划剩余 ${clock(watering.planned_remaining_s)}`
      : "—",
  );

  const resultLabels = {
    none: ["暂无结果", "设备启动后完成的浇水任务会显示在这里"],
    completed: ["上次浇水已完成", "任务正常完成"],
    stopped: ["上次浇水已停止", "任务由用户或系统停止"],
    failed: ["上次浇水未完成", "请结合停止原因检查设备"],
  };
  const [title, fallback] = resultLabels[watering.last_result] || ["上次任务已有结果", watering.last_result || "—"];
  const stopReasons = {
    none: "",
    completed: "全部计划水路已完成",
    user_stopped: "由用户停止",
    flow_start_timeout: "水流未在规定时间内建立",
    no_flow_timeout: "运行中持续未检测到水流",
    low_flow: "检测到水流过低",
    high_flow: "检测到水流过高",
    learning_timeout: "流量学习超过最长时间",
    hardware_failure: "硬件执行失败",
    maintenance_interrupted: "维护操作被中断",
    target_volume_timeout: "达到最长运行时间仍未完成目标水量",
  };
  setText("lastWateringTitle", title);
  setText("lastWateringDetail", stopReasons[watering.stop_reason] || fallback);
}

function nextAutomaticText(automatic) {
  if (automatic.mode === "paused_indefinitely") {
    return ["自动浇水已暂停", "当前为无限期暂停，需要手动恢复后计划才会自动启动。"];
  }
  if (automatic.mode === "paused_until") {
    return ["自动浇水定时暂停", `将在 ${fixedTime(automatic.resume_at, true)} 自动恢复。`];
  }
  const labels = {
    no_enabled_plans: ["没有可执行的自动计划", "请在计划管理中启用计划，并设置开始时间和至少一条水路。"],
    time_unavailable: ["设备时间尚未就绪", "自动计划暂时不会运行，手动浇水仍可使用。"],
    rtc_rollback: ["设备时间异常", "检测到 RTC 时间明显倒退，等待网络校时后自动恢复判断。"],
  };
  if (automatic.next_status === "available" && automatic.next_plan_id) {
    const plan = snapshot.plans[automatic.next_plan_id];
    return [
      plan?.name || `计划 ${automatic.next_plan_id}`,
      `${fixedTime(automatic.next_at, true)} 自动开始`,
    ];
  }
  return labels[automatic.next_status] || ["正在计算下一计划", "设备尚未给出下一次自动浇水时间。"];
}

function renderAutomatic() {
  const automatic = snapshot.state?.automatic || {};
  const [nextTitle, nextDetail] = nextAutomaticText(automatic);
  setText("nextPlanTitle", nextTitle);
  setText("nextPlanDetail", nextDetail);

  const paused = automatic.mode === "paused_indefinitely" || automatic.mode === "paused_until";
  setText(
    "automaticMode",
    paused ? "自动浇水已暂停" : "自动浇水已启用",
  );
  setText(
    "automaticDetail",
    automatic.mode === "paused_indefinitely"
      ? "无限期暂停中"
      : automatic.mode === "paused_until"
        ? `将在 ${fixedTime(automatic.resume_at, true)} 自动恢复`
        : `${nextTitle} · ${nextDetail}`,
  );
  byId("openPauseButton").hidden = paused;
  byId("resumeButton").hidden = !paused;
}

function addFault(container, text, tone = "") {
  const item = document.createElement("span");
  item.className = `fault ${tone}`.trim();
  item.textContent = text;
  container.append(item);
}

function renderHealth() {
  const container = byId("faults");
  container.replaceChildren();
  const faults = snapshot.state?.faults || {};
  const time = snapshot.state?.time || {};
  const active = [];
  if (faults.unexpected_flow) active.push(["关阀后仍检测到水流", "bad"]);
  if (faults.watering_records) active.push(["浇水记录存储异常", "warn"]);
  if (faults.events) active.push(["事件存储异常", "warn"]);
  if (faults.scheduler) active.push(["自动计划状态存储异常", "bad"]);
  if (faults.checkpoint) active.push(["运行检查点存储异常", "warn"]);
  if (!time.trusted) active.push(["设备时间尚未就绪，自动计划不会运行", "warn"]);
  if (time.rtc_unavailable) active.push(["硬件时钟不可用，断网后计划可能暂停", "warn"]);

  if (active.length) {
    for (const [text, tone] of active) addFault(container, text, tone);
    setText("healthTitle", "有需要处理的状态");
  } else {
    addFault(container, "当前没有设备故障");
    setText("healthTitle", snapshot.state?.ready ? "设备运行正常" : "设备状态尚未就绪");
  }

  const sources = { ntp: "网络校时", rtc: "硬件时钟", uptime: "尚未校时" };
  setText("timeBadge", `设备时间：${sources[time.source] || "未知"}`);
  byId("timeBadge").className = `badge ${time.trusted ? "soft" : "warn"}`;
  setText("lastResult", resultLabel(snapshot.lastResult));
}

function planDurations(plan) {
  return enabledZones()
    .map((zone) => {
      const minutes = Number(plan?.durations?.[Number(zone.id) - 1]) || 0;
      return minutes ? `${zone.name} ${minutes} 分钟` : null;
    })
    .filter(Boolean);
}

function planSummary(plan) {
  const starts = (plan.starts || []).map(minuteLabel).join("、") || "没有开始时间";
  const durations = planDurations(plan);
  return {
    schedule: plan.enabled ? `每天 ${starts}` : "自动执行已关闭",
    zones: durations.join(" · ") || "没有可执行水路",
  };
}

function actionButton(label, className, listener) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = `button ${className}`;
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
    empty.className = "panel empty-state";
    empty.innerHTML = "<h2>还没有计划</h2><p>新增一个计划后，可按时间自动浇水，也可作为手动浇水模板。</p>";
    grid.append(empty);
  }
  for (const plan of plans) {
    const card = document.createElement("article");
    card.className = "plan-card";
    const head = document.createElement("div");
    head.className = "plan-card-head";
    const title = document.createElement("h2");
    title.textContent = plan.name || `计划 ${plan.id}`;
    const status = document.createElement("span");
    status.className = `badge ${plan.enabled ? "" : "muted"}`;
    status.textContent = plan.enabled ? "自动执行" : "仅作模板";
    head.append(title, status);
    const summaryValue = planSummary(plan);
    const summary = document.createElement("div");
    summary.className = "plan-summary";
    const schedule = document.createElement("p");
    schedule.textContent = summaryValue.schedule;
    const zones = document.createElement("p");
    zones.textContent = summaryValue.zones;
    summary.append(schedule, zones);
    const actions = document.createElement("div");
    actions.className = "plan-actions";
    actions.append(actionButton("编辑计划", "secondary", () => openPlan(plan)));
    card.append(head, summary, actions);
    grid.append(card);
  }
}

function updateManualSummary() {
  const selected = enabledZones()
    .map((zone) => [zone, Number(manualDraft.get(Number(zone.id))) || 0])
    .filter(([, minutes]) => minutes > 0);
  const total = selected.reduce((sum, [, minutes]) => sum + minutes, 0);
  setText(
    "manualSummary",
    selected.length ? `已选择 ${selected.length} 条水路，合计 ${total} 分钟` : "尚未选择水路",
  );
  setText(
    "manualNote",
    selectedTemplateId
      ? `已套用“${snapshot.plans[selectedTemplateId]?.name || `计划 ${selectedTemplateId}`}”，可以继续临时调整`
      : `每条水路范围 0～${maximumMinutes() || "—"} 分钟，0 表示本次不执行`,
  );
  byId("startManualButton").disabled =
    !canControl() || busy || snapshot.state?.watering?.active || !selected.length;
}

function setManualValue(zoneId, value, fromInput = false) {
  manualDraft.set(Number(zoneId), Number(value) || 0);
  const input = byId(`manualZone${zoneId}`);
  if (input && String(input.value) !== String(value)) input.value = String(value);
  if (fromInput) selectedTemplateId = 0;
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
    empty.className = "secondary";
    empty.textContent = "暂无已保存计划，可以直接填写水路时长。";
    container.append(empty);
    return;
  }
  for (const plan of plans) {
    const summary = planSummary(plan);
    const button = document.createElement("button");
    button.type = "button";
    button.className = `template-card ${Number(plan.id) === selectedTemplateId ? "selected" : ""}`;
    button.dataset.templateId = String(plan.id);
    button.disabled = !planDurations(plan).length;
    const title = document.createElement("b");
    title.textContent = plan.name || `计划 ${plan.id}`;
    const detail = document.createElement("span");
    detail.textContent = summary.zones;
    const tag = document.createElement("small");
    tag.textContent = plan.enabled ? "自动计划" : "仅作手动模板";
    button.append(title, detail, tag);
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
    label.append(name, input);
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
  renderConnection();
  renderWatering();
  renderAutomatic();
  renderHealth();
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
    throw new Error(value.error || resultLabel(value.result));
  }
  return value;
}

async function mutate(path, body, message) {
  busy = true;
  renderControls();
  try {
    const value = await api(path, { method: "POST", body: JSON.stringify(body) });
    snapshot.lastResult = value.result;
    toast(message);
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
  all(".tab").forEach((item) => item.classList.toggle("active", item.dataset.view === name));
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
    label.append(name, input);
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
  const now = new Date();
  const shanghai = new Date(now.toLocaleString("en-US", { timeZone: "Asia/Shanghai" }));
  shanghai.setDate(shanghai.getDate() + dayOffset);
  const year = shanghai.getFullYear();
  const month = String(shanghai.getMonth() + 1).padStart(2, "0");
  const day = String(shanghai.getDate()).padStart(2, "0");
  return `${year}-${month}-${day}T06:00`;
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
  byId("pauseTimeWarning").textContent =
    "设备时间尚未就绪，不能可靠设置定时恢复；仍可选择无限期暂停。";
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
  all(".tab").forEach((tab) => tab.addEventListener("click", () => switchView(tab.dataset.view)));
  all("[data-go-view]").forEach((button) => button.addEventListener("click", () => switchView(button.dataset.goView)));

  byId("stopButton").addEventListener("click", async () => {
    if (!window.confirm("确认停止当前整次浇水任务？")) return;
    await mutate("/api/watering/stop", {}, "正在停止浇水");
  });
  byId("manualOpenButton").addEventListener("click", openManual);
  byId("closeManualDialog").addEventListener("click", () => byId("manualDialog").close());
  byId("cancelManualDialog").addEventListener("click", () => byId("manualDialog").close());
  byId("clearManualButton").addEventListener("click", () => {
    selectedTemplateId = 0;
    for (const zone of enabledZones()) setManualValue(zone.id, 0);
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
    if (!selected.length) return toast("请至少填写一条水路的浇水时长", true);
    const total = selected.reduce((sum, [, minutes]) => sum + minutes, 0);
    if (!window.confirm(`确认手动浇水 ${selected.length} 条水路，合计 ${total} 分钟？`)) return;
    if (await mutate("/api/watering/start-manual", { durations }, "手动浇水已开始")) {
      byId("manualDialog").close();
    }
  });

  byId("openPauseButton").addEventListener("click", openPauseDialog);
  byId("resumeButton").addEventListener("click", () => mutate("/api/automatic/resume", {}, "自动计划已恢复"));
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
        : Math.floor(Date.now() / 1000) + Number(byId("pauseHours").value) * 3600;
      if (!Number.isFinite(resumeAt) || resumeAt <= Date.now() / 1000) {
        throw new Error("恢复时间必须晚于现在");
      }
      if (!window.confirm(`确认暂停自动浇水，并在 ${fixedTime(resumeAt, true)} 自动恢复？`)) return;
      if (await mutate("/api/automatic/pause", { resumeAt }, "定时暂停已设置")) {
        byId("pauseDialog").close();
      }
    } catch (error) {
      toast(error.message, true);
    }
  });
  byId("indefinitePauseButton").addEventListener("click", async () => {
    if (!window.confirm("确认无限期暂停自动浇水？")) return;
    if (await mutate("/api/automatic/pause", { resumeAt: 0 }, "自动计划已无限期暂停")) {
      byId("pauseDialog").close();
    }
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
    if (new TextEncoder().encode(name).length > 48) {
      return toast("计划名称最多 48 个 UTF-8 字节", true);
    }
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
    const plan = snapshot.plans[id];
    if (!window.confirm(`确认删除“${plan?.name || `计划 ${id}`}”？`)) return;
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
    byId("brokerBadge").textContent = "连接本机服务中";
    byId("brokerBadge").className = "badge muted";
  };
}

initializeTimeInputs();
bindEvents();
render();
connect();
