# Web/API 功能验证清单

本清单用于烧录固件后验证 `/irrigation` 业务页面和 `/api/v1/*` 业务 API。验证前先确认设备已完成 WiFi/NTP 基础初始化。

## 首页与导航

- 打开 `/`：应进入灌溉业务首页。
- 打开 `/irrigation`：应显示首页，包含设备状态、4 路浇水状态、手动浇水和当前告警。
- 业务导航顺序应为：首页、近期计划、历史记录、计划配置、灌溉设置。
- 业务导航不应显示单独的手动页或调试页。
- 底部应保留 Esp32Base 默认页脚导航：`Status`、`Logs`、`App Config`、`System`。

## UI baseline

- 业务页面应由 `Esp32BaseWeb::sendHeader()` 自动加载 `/esp32base/ui.css`，不应注入整套业务自定义 CSS。
- 页面标题、面板、提示、指标、配置行、表格、表单网格和分页应优先使用 Esp32Base UI helper 或 baseline class。
- 业务页不应使用旧 `shell`、`grid/span-*`、`badge`、`modal`、`table-wrap`、`field-grid` 等自定义页面结构。
- 所有改变状态的页面表单都必须使用 POST，并带浏览器二次确认和 `once()` 防重复提交。
- 修改前可运行 `node scripts/check-web-structure.mjs`，确认结构红线仍被自动检查覆盖。

## 状态 API

- `GET /api/v1/status` 返回合法 JSON。
- JSON 包含 `wifi`、`time`、`settings`、`watering`、`records`、`events`。
- `watering.roads` 应包含 `r1`、`r2`、`r3`、`r4`。
- 每路包含启用状态、运行状态、目标秒数、估算水量、当前流速和阀门状态。

## 手动控制

- 默认第 1、2 路启用，第 3、4 路停用。
- 首页能看到 4 路手动控制卡片。
- 停用水路可见但不能启动。
- 页面表单提交后历史记录中的启动/停止来源应为 `web_page`。
- 直接调用 `POST /api/v1/water/start` 且不带 `source=web_page` 时，历史记录启动来源应为 `http_api`。
- 外部 API 启动返回逐路结果数组，包含 `started`、`busy`、`disabled`、`invalid_duration`、`not_requested` 或 `rejected`。
- `POST /api/v1/water/stop` 页面触发后返回首页；直接 API 调用返回 JSON。

## 近期计划

- 打开 `/irrigation/plans`：应以一个表格显示今天、明天、后天。
- 每行对应一路的一个计划槽。
- 已手动跳过、已启动、水路停用跳过、水路忙跳过等结果应显示为计划触发结果。
- 每个未执行的未来计划可单独跳过，已跳过计划可取消跳过。
- `GET /api/v1/plans/recent` 返回今天、明天、后天展开后的计划结果 JSON。

## 计划配置

- 打开 `/irrigation/plan-config`：应显示 24 个计划槽。
- 每个计划显示为“第 N 路 / 计划 M”。
- 编辑页只能修改该计划槽的启用状态、时间、目标时长、循环天数、循环开始日期和循环执行日。
- 不应出现同时/顺序模式。
- `GET /api/v1/plans` 返回 24 个计划的原始配置字段。

## 灌溉设置

- 打开 `/irrigation/settings`：应显示 4 路固定引脚。
- 每路可修改启用状态、名称、默认手动时长、每升脉冲和校准系数。
- PWM 参数应展示为固定策略，不开放页面修改。
- 修改设置后刷新和重启应保持。
- 无效范围必须拒绝，例如水流异常窗口为 0 或 61。

## 历史记录与事件

- 打开 `/irrigation/data`：只显示浇水业务记录，不显示系统事件。
- 每条历史记录对应一路一次浇水任务。
- 记录应显示任务类型、启动来源、停止来源、结果、目标时长、实际时长和估算水量。
- `GET /api/v1/records?limit=10` 返回合法 JSON。
- `GET /api/v1/events?limit=20` 作为内部事实流诊断 API 保留。

## 异常与维护

- 清除当前告警表单提交后返回首页，并写入 `alert_clear` 事件。
- 业务页面不提供恢复出厂入口。
- 恢复出厂待处理后，配置保存、手动启动、清除告警、计划保存和计划跳过应返回 `factory_reset_pending`。
- 恢复出厂待处理时，停止本路和停止全部仍应允许。

## 未实机验证项

- 阀门实际电平、PWM 占空比和驱动温升。
- 流量计脉冲稳定性和外部上拉。
- 默认 10 秒无脉冲超时是否匹配真实水流启动延迟。
- GPIO0 长按 3 秒恢复出厂是否容易误触。
- 72 小时连续运行稳定性。
