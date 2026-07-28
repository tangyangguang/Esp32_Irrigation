import crypto from "node:crypto";

export class ProtocolError extends Error {}

function integer(value, name, minimum, maximum) {
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new ProtocolError(`${name}超出允许范围`);
  }
  return value;
}

function commandId(action) {
  return `mac-${action}-${Date.now().toString(36)}-${crypto.randomBytes(5).toString("hex")}`;
}

export function createCommand(action, args = undefined, revision = undefined) {
  const command = { v: 1, id: commandId(action), action };
  if (revision !== undefined) command.revision = revision;
  if (args !== undefined) command.args = args;
  return command;
}

export function validatePlan(id, value) {
  integer(id, "计划编号", 1, 8);
  integer(value.expectedRevision, "配置版本", 1, 0xffffffff);
  if (typeof value.name !== "string" || value.name.trim().length < 1) {
    throw new ProtocolError("计划名称不能为空");
  }
  if (new TextEncoder().encode(value.name.trim()).length > 48) {
    throw new ProtocolError("计划名称过长");
  }
  if (typeof value.enabled !== "boolean") {
    throw new ProtocolError("自动执行状态无效");
  }
  if (!Array.isArray(value.starts) || value.starts.length > 4) {
    throw new ProtocolError("每天最多设置4个启动时间");
  }
  const starts = [...new Set(value.starts.map((item) =>
    integer(item, "启动时间", 0, 1439),
  ))].sort((a, b) => a - b);
  if (!Array.isArray(value.durations) || value.durations.length !== 6) {
    throw new ProtocolError("必须提供六路浇水时长");
  }
  const durations = value.durations.map((item) =>
    integer(item, "水路时长", 0, 720),
  );
  return {
    expectedRevision: value.expectedRevision,
    args: {
      id,
      name: value.name.trim(),
      enabled: value.enabled,
      starts,
      durations,
    },
  };
}

export function validateDurations(value) {
  if (!Array.isArray(value) || value.length !== 6) {
    throw new ProtocolError("必须提供六路浇水时长");
  }
  return value.map((item) => integer(item, "水路时长", 0, 720));
}

export function validateDeviceDurations(value, meta) {
  const durations = validateDurations(value);
  const maximum = meta?.maximum_zone_duration_minutes;
  if (!Number.isInteger(maximum) || maximum < 1 || maximum > 720) {
    throw new ProtocolError("设备时长限制尚未就绪，请稍后重试");
  }
  const zones = Array.isArray(meta?.zones) ? meta.zones : [];
  for (let index = 0; index < durations.length; index += 1) {
    if (durations[index] > maximum) {
      throw new ProtocolError(`水路时长不能超过 ${maximum} 分钟`);
    }
    const zone = zones.find((item) => Number(item.id) === index + 1);
    if (!zone?.enabled && durations[index] !== 0) {
      throw new ProtocolError(`水路 ${index + 1} 已禁用，不能设置浇水时长`);
    }
  }
  return durations;
}

export function validatePlanId(value) {
  return integer(value, "计划编号", 1, 8);
}

export function validateResumeAt(value) {
  return integer(value, "恢复时间", 0, 0xffffffff);
}

export function parseJsonPayload(payload, topic) {
  try {
    const value = JSON.parse(payload.toString("utf8"));
    if (!value || typeof value !== "object" || value.v !== 1) {
      throw new Error("unsupported payload");
    }
    return value;
  } catch {
    throw new ProtocolError(`设备主题返回了无效数据：${topic}`);
  }
}
