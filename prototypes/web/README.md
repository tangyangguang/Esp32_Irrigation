# Web 原型说明

本目录存放 ESP32 灌溉项目的 Web 页面原型。原型用于确认页面结构、操作流程、字段和中文文案，不承载真实业务逻辑，不接入 PlatformIO 构建。

真实运行页面以 `src/web/IrrigationWeb.cpp` 生成的服务端 HTML 为准；本目录是静态设计稿，不代表当前固件一定逐字一致。

## 当前版本

本版按实际使用频率组织导航：

`总览 / 手动 / 近期计划 / 计划配置 / 记录 / 设置 / 调试`

## 页面清单

- `index.html`：原型入口和评审状态。
- `dashboard.html`：总览，只展示设备状态、浇水状态、停止操作和异常提示。
- `manual.html`：查看浇水状态、按路选择时长并开始手动浇水。
- `plans.html`：近期计划，展示昨日、今日、明日、后天的计划执行状态，并处理跳过某次或某天计划。
- `plan-config.html`：计划配置，展示计划列表和修改入口。
- `plan-edit.html`：编辑执行时间、循环规则、执行模式和每路时长。
- `records.html`：浇水记录、系统事件和导出入口。
- `settings.html`：灌溉业务参数，每次只修改一个参数。
- `status-api.html`：调试页，展示状态 API 结构示意。

## 原型到真实路由

| 原型文件 | 当前真实路由 | 说明 |
|---|---|---|
| `dashboard.html` | `/irrigation` | 业务总览。 |
| `manual.html` | `/irrigation/manual` | 手动浇水。 |
| `plans.html` | `/irrigation/plans` | 近期计划和跳过操作。 |
| `plan-config.html` | `/irrigation/plan-config` | 计划列表。 |
| `plan-edit.html` | `/irrigation/plan?edit=<index>` | 单条计划编辑。 |
| `records.html` | `/irrigation/data` | 记录、事件、CSV 导出。 |
| `settings.html` | `/irrigation/settings` | 灌溉业务设置和恢复出厂入口。 |
| `status-api.html` | `/irrigation/debug`、`/api/v1/status` | 调试页和实时状态 API。 |
| `index.html` | 无直接运行路由 | 原型入口。 |

## 设计原则

- 中文界面，简洁大方，采用紧凑卡片式布局，电脑宽屏不拉太宽，手机单列展示。
- 卡片只用于功能分区，不做大面积装饰；模块标题在卡片内上方，内容自然占满有效宽度。
- 所有改变状态的操作都必须有确认提示；计划编辑只使用循环规则表达重复执行，临时跳过只在近期计划页处理。
- 顶部导航负责页面跳转，总览页不重复放快捷入口。
- `/esp32base` 已提供 WiFi、认证账号密码、OTA、日志、重启等系统工具，业务页面不重复提供系统工具入口。
- 原型保持轻量静态 HTML/CSS，不引入前端框架，后续应能映射到 ESP32 服务端 HTML。
- 页面不追求装饰感，优先清晰、稳定、好用。
