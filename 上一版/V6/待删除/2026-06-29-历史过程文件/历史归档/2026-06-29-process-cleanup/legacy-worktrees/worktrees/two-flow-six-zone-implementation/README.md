# ESP32 Irrigation

ESP32 灌溉控制器应用项目，目标模型为最多 2 个 Flow 流量计、最多 6 个 Zone 电磁阀水路。每个 Zone 可任意归属 Flow 1 或 Flow 2；同一 Flow 下 Zone 互斥运行，不同 Flow 下 Zone 可并行运行。

当前重构不考虑旧配置、旧 API、旧记录或旧页面兼容。设备会格式化后部署，应用层只实现灌溉业务；WiFi、NTP、mDNS、Web、OTA、日志、文件系统、App Config 和 App Events 等基础能力继续使用 `/Users/tyg/dir/claude_dir/Esp32Base`，默认按 `ESP32BASE_PROFILE_FULL` 规划。

## Current Design

- Flow：最多 2 个流量计，使用固定点 K+Offset 线性校准。
- Zone：最多 6 路电磁阀水路，每路固定一个阀门 PWM 输出。
- Flow 归属：每个 Zone 配置 `flowId = 1 or 2`，支持任意组合，例如 `1/3/5 -> Flow 1`、`2/4/6 -> Flow 2`。
- 运行互斥：同一 Flow 下只允许一个 Zone 运行；不同 Flow 下允许并行。
- 默认配置：Flow 1 启用，Flow 2 禁用；Zone 1/2 启用，Zone 3..6 禁用；所有 Zone 默认归属 Flow 1。
- 本地控制：只选择 enabled Zone 手动启动/停止和 Stop All；启动、单路停止、Stop All 都需要同键二次确认。
- Web 首页：展示并操作所有 enabled Zone，隐藏 disabled Zone 的主操作入口；Zone 设置页仍可重新启用。
- 校准：Flow 参数使用 K+Offset；Zone 学习保存正常流量基线和高低流量阈值。
- 记录：浇水记录使用 `/irr/records_v1.bin` 固定环形文件；业务事件使用 `Esp32BaseAppEventLog`。

## Key Routes

- `/irrigation`：业务总览。
- `/irrigation/flows`：Flow 设置和校准参数。
- `/irrigation/zones`：Zone 设置、Flow 归属和安全阈值。
- `/irrigation/plans`：Zone 计划。
- `/irrigation/settings`：业务系统设置。
- `/irrigation/calibration`：Flow K+Offset 校准和 Zone 学习。
- `/irrigation/records`：浇水记录。
- `/irrigation/events`：灌溉业务事件。
- `GET /api/v1/status`：Zone 状态、Zone 配置、Flow 配置、Flow 告警。
- `GET /api/v1/plans`、`POST /api/v1/plans/save`、`POST /api/v1/plans/delete`：计划 API。
- `GET /api/v1/records`、`GET /api/v1/events`：记录和业务事件 API。
- `POST /api/v1/manual/start`、`POST /api/v1/manual/stop`、`POST /api/v1/manual/stop-all`：手动浇水 API。
- `POST /api/v1/zones/config`、`POST /api/v1/zones/baseline/*`、`POST /api/v1/flows/config`、`POST /api/v1/calibration/*`、`GET/POST /api/v1/config`：配置和校准 API。

## Build

```bash
pio run
```

项目通过 symlink 引用本地 `Esp32Base`：

```ini
lib_deps =
  symlink:///Users/tyg/dir/claude_dir/Esp32Base
```

## Validation

```bash
node scripts/check-web-structure.mjs
pio run
```

构建通过不等于硬件验证通过。阀门吸合、PWM 保持、GPIO5 启动采样、流量计上拉、真实喷头流量、增压泵缺水和 72 小时稳定性必须单独标注实机验证结果。

## Documentation

- [项目协作规范](AGENTS.md)
- [总体计划](PROJECT_PLAN.md)
- [当前需求摘要](docs/01_requirements_v1.md)
- [下一阶段实施入口](docs/02_next_implementation_plan.md)
- [Web/API 验证清单](docs/03_web_validation_checklist.md)
- [事件字段语义](docs/04_event_fields.md)
- [详细设计](docs/superpowers/specs/2026-06-05-two-flow-six-zone-irrigation-design.md)
- [实施计划](docs/superpowers/plans/2026-06-05-two-flow-six-zone-irrigation-redesign.md)
