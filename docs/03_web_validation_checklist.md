# Web/API 功能验证清单

本清单用于新 2 Flow / 6 Zone 重构完成后验证业务页面和 `/api/v1/*` 业务 API。验证前先确认设备已完成 WiFi/NTP 基础初始化。

## 首页与导航

- 打开 `/`：应进入 Esp32Base 或业务入口，并可到达 `/irrigation`。
- 打开 `/irrigation`：应显示 Flow 状态、enabled Zone 操作卡、运行中任务、计划队列和当前故障。
- 首页应显示所有 enabled Zone 的启动/停止入口，不只显示 Zone 1/2。
- disabled Zone 不应出现在首页操作卡中，但应能在 `/irrigation/zones` 查看并重新启用。
- 业务导航应包含：总览、Flows、Zones、Plans、Settings、Calibration、Records、Events。
- 底部或系统导航应保留 Esp32Base 基础入口。

## UI 和安全

- 业务页面输出必须做 HTML escape。
- JSON API 输出必须做 JSON escape。
- 所有状态改变页面表单都必须使用 POST、鉴权、浏览器二次确认和防重复提交。
- 页面禁用按钮不能代替服务端校验；所有 POST handler 必须重新校验。
- 跨站 `Origin` / `Referer` 不应触发副作用。

## 状态 API

- `GET /api/v1/status` 返回合法 JSON。
- `flows[]` 至少包含：`flowId`、`enabled`、`pulsePin`、`activeZoneId`、`frequencyMilliHz`、`flowMlPerMin`、`belowMeteringRange`、`sampleInvalid`、`totalPulses`。
- `zones[]` 至少包含：`zoneId`、`name`、`enabled`、`valvePin`、`flowId`、`state`、`canStart`、`blockedReason`、`running`、`elapsedSec`、`targetSec`、`estimatedMl`、`fault`。
- `queue[]` 至少包含：`planId`、`zoneId`、`flowId`、`scheduledEpoch`、`queuedEpoch`、`expiresEpoch`。
- `faults` 至少包含：`zoneFaults[]`、`flowLeakFaults[]`、`leakProtectionActive`。

## 手动控制

- `POST /api/v1/zones/start` 可启动 enabled Zone。
- disabled Zone、ZoneFault、Flow disabled、Flow busy、leak protection、calibration/learning active 时应拒绝，并返回稳定 reason。
- 手动启动不进入计划队列。
- `POST /api/v1/zones/stop` 只停止目标 Zone。
- `POST /api/v1/zones/stop-all` 停止所有运行 Zone，并中止校准/学习。
- 页面手动启动、单路停止和 Stop All 都必须有二次确认。

## Flow 和 Zone 配置

- `/irrigation/flows` 显示 Flow 1/2、输入 GPIO、active/pending/rollback 校准参数和安装提示。
- 禁用 Flow 时，如果仍有 enabled Zone 归属该 Flow，应拒绝保存。
- Flow 参数手工编辑只保存 pendingCalibration，不直接覆盖 activeCalibration。
- `/irrigation/zones` 显示 Zone 1..6、启用状态、名称、阀门 GPIO、归属 Flow、基线和安全阈值。
- enabled Zone 必须归属 enabled Flow。
- running Zone 不允许改名、禁用、改 Flow、保存基线或应用/回退基线。
- Zone 基线手工编辑只保存 pendingBaseline。

## 计划

- `/irrigation/plans` 显示 Zone 1..6 的计划，每个 Zone 最多 6 条，总计 36 个计划槽。
- disabled Zone 可以保留 disabled 计划，但不能启用计划。
- Flow disabled 时，不允许启用该 Flow 下 Zone 的计划。
- 同一 Flow 下计划冲突时进入队列，页面显示 queued。
- 超过 `queuedPlanMaxDelaySec` 时任务过期，页面显示 expired，并记录业务事件。

## 校准和学习

- `/irrigation/calibration` 分为 Flow K+Offset 校准和 Zone 正常流量学习。
- 同一时间只能有一个 Flow 校准或 Zone 学习任务。
- Flow 校准采样必须选择目标 Flow 和一个归属该 Flow 的 enabled Zone。
- 单点采样生成 `offsetMilliHz = 0` 的 pendingCalibration。
- 多点拟合生成 K+Offset 的 pendingCalibration，并显示误差和可信度。
- Flow 参数 apply/rollback 只允许目标 Flow 空闲且没有校准/学习进行中。
- Zone 学习只生成 pendingBaseline，不直接覆盖 activeBaseline。
- 本地屏幕不提供校准、学习或实际水量输入。

## 记录与事件

- `/irrigation/records` 分页显示 `/irr/records_v1.bin` 中的浇水记录。
- 每条记录应包含 `zoneId`、`flowId`、Flow/Zone 参数快照、目标时长、实际时长、估算水量、结果和停止来源。
- `/irrigation/events` 分页显示灌溉业务事件，不另建第二套事件存储。
- `GET /api/v1/records` 返回浇水记录 JSON。
- `GET /api/v1/events` 返回灌溉业务事件 JSON。
- 基础库系统事件仍由 Esp32Base App Events 页面/API 查看。

## 本地按钮和 I2C 屏幕

- 本地选择列表只包含 enabled Zone。
- Button1/2 可选择上一个/下一个 enabled Zone。
- Button3 第一次按显示启动或停止确认；5 秒内再次按同一键才执行。
- Button4 第一次按显示 Stop All 确认；5 秒内再次按同一键才执行。
- 切换 Zone、切换信息页或确认超时应取消 pending confirmation。
- I2C 屏幕显示 selected enabled Zone、Flow、blockedReason、运行水量、队列数量和故障摘要。
- I2C 屏幕不提供配置、校准、学习或数字输入。

## 存储和维护

- `FlowConfigStore` 按 `f1/f2` 拆 key 保存。
- `ZoneConfigStore` 按 `z1..z6` 拆 key 保存。
- `PlanStore` 按 `z<zone>_<slot>` 拆 key 保存，不使用一个大 blob。
- `RecordStore` 使用 `Esp32BaseFs::readBytesAt()` / `writeBytesAt()`，不直接使用 Arduino `File` 或 include `LittleFS.h`。
- Factory reset / clear irrigation data 只清除灌溉 namespace 和 `/irr/records_v1.bin`，不清除 Esp32Base 自有 namespace。

## 未实机验证项

- 6 路阀门实际电平、PWM 保持和驱动温升。
- 2 路 Flow 输入在真实线缆、外部上拉和喷头组合下的稳定性。
- 真实水路 `pressurizeSec` 和 `noPulseTimeoutSec` 是否匹配。
- GPIO5 作为 Valve6 输出是否影响启动采样。
- 增压泵缺水和 72 小时连续运行稳定性。
