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
const all = (selector) => [...document.querySelectorAll(selector)];

function setText(id, value) {
  byId(id).textContent = value ?? "—";
}

function zoneName(id) {
  return snapshot.meta?.zones?.find((zone) => Number(zone.id) === Number(id))?.name
    || `水路 ${id}`;
}

function fixedTime(epoch) {
  if (!epoch) return "—";
  return new Intl.DateTimeFormat("zh-CN", {
    timeZone: "Asia/Shanghai",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  }).format(new Date(Number(epoch) * 1000));
}

function automaticLabel(mode) {
  return {
    enabled: "自动计划已启用",
    paused_indefinitely: "自动计划已暂停",
    paused_until: "自动计划定时暂停",
  }[mode] || "状态未知";
}

function resultLabel(result) {
  if (!result) return "暂无控制命令";
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

function canControl() {
  return snapshot.brokerConnected
    && snapshot.availability === "online"
    && snapshot.state?.ready === true;
}

function renderConnection() {
  const broker = byId("brokerBadge");
  const device = byId("deviceBadge");
  broker.textContent = snapshot.brokerConnected ? "MQTT 已连接" : "MQTT 未连接";
  broker.className = `badge ${snapshot.brokerConnected ? "" : "muted"}`;
  device.textContent = snapshot.availability === "online" ? "设备在线" : "设备离线";
  device.className = `badge ${snapshot.availability === "online" ? "" : "muted"}`;

  const notice = byId("blockingNotice");
  if (!snapshot.brokerConnected) {
    notice.textContent = "本机尚未连接 MQTT 服务器，请检查网络或连接配置。";
    notice.className = "notice danger";
  } else if (snapshot.availability !== "online") {
    notice.textContent = "浇水设备当前离线，控制操作已锁定。";
    notice.className = "notice danger";
  } else if (!snapshot.state) {
    notice.textContent = "设备已在线，正在读取业务状态……";
    notice.className = "notice warn";
  } else if (!snapshot.state.ready) {
    notice.textContent = "设备已联网，但灌溉业务尚未就绪；请先处理设备本地配置或故障。";
    notice.className = "notice warn";
  } else {
    notice.textContent = snapshot.state.watering?.active
      ? "设备正在浇水，可以随时停止。"
      : "设备在线且就绪，可以远程控制。";
    notice.className = "notice";
  }
}

function renderWatering() {
  const watering = snapshot.state?.watering || {};
  setText("wateringTitle", watering.active ? "正在浇水" : "当前空闲");
  setText("wateringDetail", watering.active
    ? watering.source === "automatic_plan" ? `自动计划 ${watering.plan_id} 正在执行`
      : watering.source === "single_output" ? "单次出水正在执行"
        : "手动浇水正在执行"
    : "没有水路正在运行");
  setText("activeZone", watering.active ? zoneName(watering.zone_id) : "—");
  setText("remaining", watering.active ? `${Math.ceil(Number(watering.remaining_s || 0) / 60)} 分钟` : "—");
  setText("flow", watering.active ? `${Number(watering.flow_ml_min || 0)} mL/min` : "—");
  setText("water", watering.active ? `${(Number(watering.water_ml || 0) / 1000).toFixed(1)} L` : "—");
}

function renderAutomatic() {
  const automatic = snapshot.state?.automatic || {};
  let detail = "下一计划 —";
  if (automatic.mode === "paused_until") {
    detail = `恢复 ${fixedTime(automatic.resume_at)}`;
  } else if (automatic.next_plan_id) {
    detail = `计划 ${automatic.next_plan_id} · ${fixedTime(automatic.next_at)}`;
  }
  setText("automaticMode", automaticLabel(automatic.mode));
  setText("nextPlan", detail);
}

function renderFaults() {
  const container = byId("faults");
  container.replaceChildren();
  const faults = snapshot.state?.faults || {};
  const faultLabels = {
    unexpected_flow: "意外水流",
    watering_records: "浇水记录存储",
    events: "事件存储",
    scheduler: "计划调度",
    checkpoint: "运行检查点",
  };
  const active = Object.entries(faults).filter(([, value]) => value);
  if (!active.length) {
    const item = document.createElement("span");
    item.className = "fault";
    item.textContent = "当前没有设备故障";
    container.append(item);
  } else {
    for (const [key] of active) {
      const item = document.createElement("span");
      item.className = "fault bad";
      item.textContent = faultLabels[key] || key;
      container.append(item);
    }
  }
  setText("lastResult", resultLabel(snapshot.lastResult));
}

function planSummary(plan) {
  if (!plan?.configured) return "尚未配置";
  const starts = (plan.starts || [])
    .map((minute) => `${String(Math.floor(minute / 60)).padStart(2, "0")}:${String(minute % 60).padStart(2, "0")}`)
    .join("、") || "没有开始时间";
  const durations = (plan.durations || [])
    .map((minutes, index) => minutes ? `${zoneName(index + 1)} ${minutes} 分钟` : null)
    .filter(Boolean)
    .join(" · ");
  return `${plan.enabled ? "已启用" : "已停用"} · ${starts}${durations ? ` · ${durations}` : ""}`;
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
  for (let id = 1; id <= 8; id += 1) {
    const plan = snapshot.plans[id] || { id, configured: false };
    const card = document.createElement("article");
    card.className = "plan-card";
    const head = document.createElement("div");
    head.className = "plan-card-head";
    const title = document.createElement("h2");
    title.textContent = plan.name || `计划 ${id}`;
    const status = document.createElement("span");
    status.className = `badge ${plan.configured && plan.enabled ? "" : "muted"}`;
    status.textContent = plan.configured ? (plan.enabled ? "已启用" : "已停用") : "未配置";
    head.append(title, status);
    const summary = document.createElement("p");
    summary.className = "plan-summary";
    summary.textContent = planSummary(plan);
    const actions = document.createElement("div");
    actions.className = "plan-actions";
    if (plan.configured) {
      const start = actionButton("立即执行", "primary", () => startPlan(id));
      start.dataset.start = "true";
      actions.append(start);
    }
    actions.append(actionButton(plan.configured ? "编辑" : "配置", "secondary", () => openPlan(plan)));
    card.append(head, summary, actions);
    grid.append(card);
  }
}

function renderPlanSelect() {
  const select = byId("startPlanSelect");
  const selected = select.value;
  select.replaceChildren();
  const plans = Object.values(snapshot.plans)
    .filter((plan) => plan?.configured)
    .sort((a, b) => Number(a.id) - Number(b.id));
  for (const plan of plans) {
    const option = document.createElement("option");
    option.value = plan.id;
    option.textContent = plan.name || `计划 ${plan.id}`;
    select.append(option);
  }
  if (plans.some((plan) => String(plan.id) === selected)) select.value = selected;
}

function renderControls() {
  const ready = canControl();
  const active = snapshot.state?.watering?.active === true;
  byId("stopButton").disabled = !ready || !active;
  byId("pauseButton").disabled = !ready;
  byId("timedPauseButton").disabled = !ready;
  byId("resumeButton").disabled = !ready;
  byId("startManualButton").disabled = !ready || active;
  byId("startPlanButton").disabled = !ready || active || !byId("startPlanSelect").options.length;
  all('[data-start="true"]').forEach((button) => button.disabled = !ready || active);
  byId("savePlanButton").disabled = !ready;
  byId("deletePlanButton").disabled = !ready;
}

function render() {
  renderConnection();
  renderWatering();
  renderAutomatic();
  renderFaults();
  renderPlanSelect();
  renderPlans();
  renderControls();
}

function toast(message, error = false) {
  const box = byId("toast");
  box.textContent = message;
  box.className = `toast show ${error ? "error" : ""}`;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => box.className = "toast", 3200);
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
  try {
    const value = await api(path, { method: "POST", body: JSON.stringify(body) });
    snapshot.lastResult = value.result;
    toast(message);
    render();
    return true;
  } catch (error) {
    toast(error.message, true);
    return false;
  }
}

async function startPlan(id) {
  const plan = snapshot.plans[id];
  if (!window.confirm(`确定立即执行“${plan?.name || `计划 ${id}`}”吗？`)) return;
  await mutate("/api/watering/start-plan", { planId: Number(id) }, "计划已开始");
}

function openPlan(plan) {
  byId("planId").value = plan.id;
  byId("planRevision").value = snapshot.state?.revision || 0;
  setText("planDialogTitle", plan.configured ? `编辑计划 ${plan.id}` : `配置计划 ${plan.id}`);
  byId("planName").value = plan.name || `计划 ${plan.id}`;
  byId("planEnabled").checked = plan.enabled ?? true;
  all(".plan-start").forEach((input, index) => {
    const minute = plan.starts?.[index];
    input.value = minute == null ? "" : `${String(Math.floor(minute / 60)).padStart(2, "0")}:${String(minute % 60).padStart(2, "0")}`;
  });
  all(".plan-duration").forEach((input, index) => input.value = plan.durations?.[index] || 0);
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

function initializeInputs() {
  for (let index = 1; index <= 6; index += 1) {
    const manualLabel = document.createElement("label");
    manualLabel.textContent = zoneName(index);
    const manualInput = document.createElement("input");
    manualInput.type = "number";
    manualInput.min = "0";
    manualInput.max = "720";
    manualInput.value = "0";
    manualInput.className = "manual-duration";
    manualLabel.append(manualInput);
    byId("manualDurations").append(manualLabel);

    const planLabel = document.createElement("label");
    planLabel.textContent = zoneName(index);
    const planInput = document.createElement("input");
    planInput.type = "number";
    planInput.min = "0";
    planInput.max = "720";
    planInput.value = "0";
    planInput.className = "plan-duration";
    planLabel.append(planInput);
    byId("planDurations").append(planLabel);
  }
  for (let index = 0; index < 4; index += 1) {
    const label = document.createElement("label");
    label.textContent = `时间 ${index + 1}`;
    const input = document.createElement("input");
    input.type = "time";
    input.className = "plan-start";
    label.append(input);
    byId("planStarts").append(label);
  }
}

function bindEvents() {
  all(".tab").forEach((tab) => tab.addEventListener("click", () => {
    all(".tab").forEach((item) => item.classList.toggle("active", item === tab));
    byId("overviewView").classList.toggle("hidden", tab.dataset.view !== "overview");
    byId("plansView").classList.toggle("hidden", tab.dataset.view !== "plans");
  }));
  byId("stopButton").addEventListener("click", () => mutate("/api/watering/stop", {}, "浇水已停止"));
  byId("startPlanButton").addEventListener("click", () => startPlan(byId("startPlanSelect").value));
  byId("pauseButton").addEventListener("click", () => mutate("/api/automatic/pause", { resumeAt: 0 }, "自动计划已暂停"));
  byId("resumeButton").addEventListener("click", () => mutate("/api/automatic/resume", {}, "自动计划已恢复"));
  byId("timedPauseButton").addEventListener("click", () => byId("pauseDialog").showModal());
  byId("closePauseDialog").addEventListener("click", () => byId("pauseDialog").close());
  byId("cancelPauseDialog").addEventListener("click", () => byId("pauseDialog").close());
  byId("closePlanDialog").addEventListener("click", () => byId("planDialog").close());
  byId("cancelPlanDialog").addEventListener("click", () => byId("planDialog").close());
  byId("startManualButton").addEventListener("click", async () => {
    const durations = all(".manual-duration").map((input) => Number(input.value || 0));
    if (!durations.some(Boolean)) return toast("请至少填写一条水路的浇水时长", true);
    const detail = durations.map((minutes, index) => minutes ? `${zoneName(index + 1)} ${minutes} 分钟` : null).filter(Boolean).join("、");
    if (!window.confirm(`确定开始手动浇水吗？\n${detail}`)) return;
    await mutate("/api/watering/start-manual", { durations }, "手动浇水已开始");
  });
  byId("pauseForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    try {
      const value = resumeEpoch(byId("pauseUntil").value);
      if (value <= Date.now() / 1000) throw new Error("恢复时间必须晚于现在");
      if (await mutate("/api/automatic/pause", { resumeAt: value }, "定时暂停已设置")) {
        byId("pauseDialog").close();
      }
    } catch (error) {
      toast(error.message, true);
    }
  });
  byId("planForm").addEventListener("submit", async (event) => {
    event.preventDefault();
    const id = Number(byId("planId").value);
    try {
      const value = await api(`/api/plans/${id}`, {
        method: "PUT",
        body: JSON.stringify({
          expectedRevision: Number(byId("planRevision").value),
          name: byId("planName").value.trim(),
          enabled: byId("planEnabled").checked,
          starts: all(".plan-start").map((input) => input.value).filter(Boolean).map(timeToMinute),
          durations: all(".plan-duration").map((input) => Number(input.value || 0)),
        }),
      });
      snapshot.lastResult = value.result;
      byId("planDialog").close();
      toast("计划已保存");
    } catch (error) {
      toast(error.message, true);
    }
  });
  byId("deletePlanButton").addEventListener("click", async () => {
    const id = Number(byId("planId").value);
    if (!window.confirm(`确定删除计划 ${id} 吗？`)) return;
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

initializeInputs();
bindEvents();
render();
connect();
