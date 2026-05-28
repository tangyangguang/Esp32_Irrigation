# Web 原型说明

本目录存放 ESP32 灌溉项目的 Web 页面原型。原型用于确认页面结构、操作流程、字段和中文文案，不承载真实业务逻辑，不接入 PlatformIO 构建。

真实运行页面以 `src/web/IrrigationWeb.cpp` 生成的服务端 HTML 为准；本目录是静态设计稿，不代表当前固件一定逐字一致。

## 当前版本

本版按当前固件页面组织导航：

`首页 / 近期计划 / 历史记录 / 计划配置 / 灌溉设置`

## 页面清单

- `index.html`：原型入口和评审状态。
- `dashboard.html`：首页，展示设备状态、4 路浇水状态、手动启动、停止操作和异常提示。
- `manual.html`：历史占位页，说明手动浇水已合并到首页。
- `plans.html`：近期计划，展示今天、明天、后天的计划执行状态和计划触发结果。
- `plan-config.html`：计划配置，展示计划列表和修改入口。
- `plan-edit.html`：编辑单一路计划的执行时间、循环规则和目标时长。
- `records.html`：浇水任务历史记录。
- `settings.html`：水路启用、名称、固定引脚、流量和安全阈值。

## 原型到真实路由

| 原型文件 | 当前真实路由 | 说明 |
|---|---|---|
| `dashboard.html` | `/irrigation` | 首页和手动浇水。 |
| `manual.html` | 无真实路由 | 手动浇水已合并到首页。 |
| `plans.html` | `/irrigation/plans` | 近期计划和跳过操作。 |
| `plan-config.html` | `/irrigation/plan-config` | 计划列表。 |
| `plan-edit.html` | `/irrigation/plan?edit=<index>` | 单条计划编辑。 |
| `records.html` | `/irrigation/data` | 历史浇水记录。 |
| `settings.html` | `/irrigation/settings` | 灌溉业务设置。 |
| `index.html` | 无直接运行路由 | 原型入口。 |

## 设计原则

- 中文界面，简洁大方，采用紧凑卡片式布局，电脑宽屏不拉太宽，手机单列展示。
- 卡片只用于功能分区，不做大面积装饰；模块标题在卡片内上方，内容自然占满有效宽度。
- 所有改变状态的操作都必须有确认提示；计划编辑只使用循环规则表达重复执行，临时跳过只在近期计划页处理。
- 顶部导航负责页面跳转，总览页不重复放快捷入口。
- `/esp32base` 已提供 WiFi、认证账号密码、OTA、日志、重启等系统工具，业务页面不重复提供系统工具入口。
- 原型保持轻量静态 HTML/CSS，不引入前端框架，后续应能映射到 ESP32 服务端 HTML。
- 页面不追求装饰感，优先清晰、稳定、好用。
