# ESP32 Irrigation

ESP32 1-2 路智能灌溉控制器应用项目。当前实现基于 `/Users/tyg/dir/claude_dir/Esp32Base`，默认使用 `ESP32BASE_PROFILE_FULL`，应用层只负责灌溉业务：阀门控制、流量计量、定时计划、Web/API、记录和告警。

旧版需求文档保留在 `old-docs/`，只作为背景材料；当前实现不继承旧 4 路硬件、EC11 编码器、旧 API 或旧数据格式。

## 当前能力

- 1-2 路阀门控制，默认启用第 1 路。
- 手动浇水：同时/顺序模式，按路独立计时和停止。
- 流量计数：2 路 YF-S201 脉冲输入，按路估算水量和流速。
- 水流异常：浇水中无脉冲超时自动关阀并记录。
- 漏水监控：待机状态检测异常脉冲并告警。
- 计划浇水：最多 8 条计划，统一循环模型，支持跳过单次或某天剩余计划。
- Web/API：首页、近期计划、计划配置、记录、灌溉设置和状态 API；手动浇水合并在首页。
- 记录与事件：浇水记录用于业务页面和 JSON 查询；事件作为内部事实流和 API 诊断数据保留。
- 恢复出厂：GPIO0/BOOT 长按 3 秒直接执行“仅恢复配置”并重启；系统维护能力优先使用 `Esp32Base`。

## 构建

```bash
pio run
```

固件目标环境在 `platformio.ini` 的 `esp32dev` 中定义。项目通过 symlink 引用本地 `Esp32Base`：

```ini
lib_deps =
  symlink:///Users/tyg/dir/claude_dir/Esp32Base
```

默认 hostname 通过 `ESP32BASE_DEFAULT_HOSTNAME` 编译期宏设置为 `esp32-irrigation`；基础库 Web/API 保存的 hostname 会在重启后覆盖该默认值。文件日志路径通过 `ESP32BASE_EB_FILELOG_PATH` 设置为 `/logs/irrigation.log`。

`Esp32BaseAppConfig` 暂不启用；业务参数仍由本项目现有设置页和 `SettingsStore` 管理，后续统一确认参数边界后再接入。

## 运行入口

- 业务首页：`/irrigation`
- 系统工具：`/esp32base`
- 状态 API：`/api/v1/status`
- 设置表单：`/irrigation/settings/config`
- 设置 JSON：`/api/v1/config`
- 记录 JSON：`/api/v1/records`
- 事件 JSON：`/api/v1/events`

当前默认 Web 账号密码和 OTA 调试配置保留在源码/`platformio.ini` 中，目的是方便本地调试；这不是缺陷项。实际部署前按现场需要调整。

## 硬件假设

- 阀门：GPIO13 / GPIO14，当前代码假设 `HIGH=开阀`。
- 流量计：GPIO34 / GPIO35。两者是 ESP32 输入专用脚，没有内部上拉；YF-S201 等开漏输出传感器必须使用外部上拉。
- GPIO0/BOOT：恢复出厂长按键，同时也是 ESP32 启动模式脚。按住上电或复位会进入下载模式；固件运行后长按 3 秒才触发仅配置恢复。
- 默认硬件目标是 1-2 路控制器，不是 `old-docs/02-硬件设计说明书.md` 中的旧 4 路 EC11 方案。

## 文档索引

- [项目协作规范](AGENTS.md)
- [总体计划](PROJECT_PLAN.md)
- [当前需求](docs/01_requirements_v1.md)
- [实现状态与下一阶段](docs/02_next_implementation_plan.md)
- [Web 验证清单](docs/03_web_validation_checklist.md)
- [事件字段语义](docs/04_event_fields.md)
- [Web 静态原型说明](prototypes/web/README.md)

## 验证状态

当前自动验证以 `pio run` 编译为主。阀门极性、流量计稳定性、GPIO0 误触、功耗、OTA、真实水路启动延迟等仍需区分实机验证结果，不能只按编译通过视为完成。
