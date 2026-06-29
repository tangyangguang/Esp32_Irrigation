# ESP32 灌溉控制器当前需求 V3

本文是当前需求摘要。完整细节以 [Two Flow Six Zone Irrigation Redesign](superpowers/specs/2026-06-05-two-flow-six-zone-irrigation-design.md) 和对应实施计划为准。`old-docs/` 和旧版 road-management 文档只保留为背景材料，不作为兼容依据。

## 1. 产品目标

1. 安全可靠：启动、异常、停止、恢复出厂和重启路径默认关阀。
2. 清晰可用：Web/API 覆盖完整业务，本地按键只做停水和手动浇水。
3. 边界明确：应用层只实现灌溉业务，基础能力使用 `Esp32Base`。
4. 不过度设计：不内置天气联动、云端访问或复杂本地菜单。

## 2. 当前范围

- 最多 2 个 Flow 流量计。
- 最多 6 个 Zone 电磁阀水路。
- 每个 Zone 任意归属 Flow 1 或 Flow 2。
- 同一 Flow 下 Zone 互斥运行，不同 Flow 下 Zone 可并行运行。
- Flow 计量使用固定点 K+Offset 校准。
- Zone 保存正常流量基线、高低流量阈值、无脉冲超时和异常动作。
- 默认 Flow 1 启用、Flow 2 禁用；默认 Zone 1/2 启用、Zone 3..6 禁用；所有 Zone 默认归属 Flow 1。
- 新部署会格式化设备，不迁移旧配置、旧记录、旧 API 或旧页面。

## 3. 手动和本地控制

- Web 首页展示所有 enabled Zone 的手动启动/停止入口，隐藏 disabled Zone 的主操作入口。
- Zone 设置页显示全部 Zone，可重新启用 disabled Zone。
- 本地屏幕和本地按键选择列表只展示 enabled Zone。
- 本地按键通过上一个/下一个选择当前 enabled Zone。
- 本地启动和单路停止只作用于当前选中 Zone，使用 `manualDefaultDurationSec`。
- 本地启动、单路停止和 Stop All 都必须二次确认；第一版采用同一个按键 5 秒内再次按下确认。
- 本地不做 Flow 校准、Zone 学习、容量输入、计划编辑或参数编辑。

## 4. 计划和运行

- 每个 Zone 最多 6 条计划，总计 36 个计划槽。
- 计划触发按 Zone 执行，仍受 Zone 启用状态、Flow 启用状态、Flow 互斥、漏水保护和配置有效性约束。
- 同一 Flow 下多个计划冲突时进入内存队列，按计划时间和 zoneId 排队。
- 手动启动不进入队列，Flow 忙时直接拒绝。
- 队列不持久化；重启后按计划宽限期重新评估。

## 5. 流量、异常和记录

- 无脉冲保护只看原始脉冲是否持续为 0。
- 低于计量下限但仍有脉冲时，不判定无水，也不累计低流量停机确认。
- 无效高频样本不累计高流量停机确认。
- 未学习 Zone 正常流量时，不启用高/低流量比例判断。
- 低/高流量异常可配置为停机或只记录。
- 待机漏水定位到 Flow，不伪装成某个 Zone 错误。
- 浇水记录保存 Flow/Zone 参数快照，使用 `/irr/records_v1.bin` 固定环形文件。
- 灌溉业务事件使用 `Esp32BaseAppEventLog`。

## 6. 页面与 API

- 页面入口：`/irrigation`、`/irrigation/flows`、`/irrigation/zones`、`/irrigation/plans`、`/irrigation/settings`、`/irrigation/calibration`、`/irrigation/records`、`/irrigation/events`。
- 只读 API：`/api/v1/status`、`/api/v1/flows`、`/api/v1/zones`、`/api/v1/plans`、`/api/v1/records`、`/api/v1/events`。
- 状态改变 API 使用 POST、鉴权和稳定 reason。
- 不保留旧 `/api/v1/zone/*`、`/api/v1/flow/*`、`/api/v1/calibration/*` 路径语义。

## 7. 验收标准

- `node scripts/check-web-structure.mjs` 通过。
- `pio run` 通过。
- 2 路 Flow 输入能计数。
- 6 路 Valve PWM 输出正常。
- 同 Flow 互斥、不同 Flow 并行符合预期。
- K+Offset 校准、Zone 学习、无脉冲停机、低/高流量动作、待机漏水、本地同键二次确认和记录恢复通过台架验证。
- 真实水路、增压泵缺水、阀门温升、GPIO5 启动采样和 72 小时稳定性必须单独做实机验证。
