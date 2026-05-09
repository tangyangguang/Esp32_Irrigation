# Web 功能验证清单

本清单用于烧录固件后验证 `/irrigation` 业务首页和 `/esp32base` 系统工具入口。验证前先确认设备已进入 Web ready 状态。

## 首页与导航

- 打开 `/`：应进入灌溉业务首页。
- 打开 `/irrigation`：应显示中文业务首页，包含网络信息、阀门状态和异常提示。
- 打开 `/irrigation/manual`：应显示手动浇水页面，每一路可独立选择是否参与本次浇水。
- 打开 `/irrigation/plans`：应显示昨日、今日、明日、后天的近期计划状态和跳过操作。
- 打开 `/irrigation/plan-config`：应显示 8 个计划配置入口。
- 打开 `/irrigation/settings`：应显示业务配置页面，按每一路聚合配置。
- 打开 `/irrigation/data`：应显示最近记录和事件。
- 打开 `/esp32base`：应显示基础库融合/系统工具入口。
- 业务页底部应能进入 Logs、OTA、Reboot 等基础库工具。

## 状态区

- 能看到 WiFi 名称、RSSI、IP 地址、当前时间、阀门状态和记录/事件容量。
- `GET /api/v1/status` 返回合法 JSON。
- 状态 JSON 中包含 `wifi.ssid`、`wifi.rssi`、`wifi.ip`、`time.current`、每路 `flow_l_min`、`records.count` 和 `events.count`。
- 禁用 `ESP32BASE_ENABLE_WIFI` 构建时，状态 JSON 仍必须是合法 JSON，`wifi.connected=false` 且 `ssid/ip` 为空字符串。

## 手动控制

- 默认 1 路启用时，只选择第 1 路并填写合法分钟数应成功。
- 启用 2 路后，应能只浇第 1 路、只浇第 2 路、两路同时浇水。
- 停止 R1、停止 R2、Stop All 表单提交后返回 `/irrigation/manual` 或当前业务页面。
- 操作后 `/api/v1/events` 应出现 `water_start` 或 `water_stop`。
- 操作后 `/api/v1/records` 应出现浇水会话记录。

## 配置

- 修改启用路数、默认模式、每路名称、每路默认时长、水流异常窗口、漏水窗口、漏水脉冲阈值后保存。
- 刷新页面后配置值应保持。
- 重启后配置值应保持。
- 无效范围必须拒绝，例如 `flow_no_pulse_timeout_s=0` 或 `61`。
- 基础库系统导航应提供认证设置入口；业务项目不保存基础库鉴权密码。
- 通过 `/esp32base/auth` 修改账号密码后，新账号密码立即生效，旧凭据后续请求应失效。

## 浇花计划

- 8 个计划都应显示。
- 每个计划可单独保存启用状态、时间、R1/R2 时长、模式、循环天数和周期内执行日。
- Skip 应设置当天跳过；Unskip 应清除跳过。
- 保存或跳过操作后 `/api/v1/events` 应出现 `plan_changed`。
- `GET /api/v1/plans` 返回 8 个计划的原始配置字段。

## 记录与导出

- 页面应显示最近浇水记录。
- 首次启动时记录文件应能自动初始化，启动过程不应触发 task watchdog。
- `GET /api/v1/records?limit=10` 返回合法 JSON。
- `GET /api/v1/records.csv` 下载 CSV，字段包含原始脉冲、校准快照和估算水量。
- 页面应显示最近系统事件。
- `GET /api/v1/events?limit=20` 返回合法 JSON。
- `GET /api/v1/events.csv` 下载 CSV。

## 异常与维护

- 清除已处理异常表单提交后返回当前业务页面，并写入 `alert_clear` 事件。
- 恢复出厂必须填写 `RESET` 才能执行。
- 恢复出厂不清记录时：配置和计划恢复默认，记录保留。
- 恢复出厂选择清记录时：配置、计划、记录、事件都清空后重启，并在重启前尽量记录执行事件。

## 未实机验证项

- 阀门实际电平是否与 `HIGH=开阀` 一致。
- YF-S201 脉冲计数是否稳定。
- 默认 10 秒无脉冲关阀是否符合真实水流启动延迟。
- GPIO0 长按 3 秒应直接执行仅配置恢复并重启；实机验证是否容易误触，以及是否保留记录和事件。
