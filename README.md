# ESP32 Irrigation

ESP32 固定 4 路智能灌溉控制器应用项目，默认启用第 1、2 路。当前实现基于 `/Users/tyg/dir/claude_dir/Esp32Base`，默认使用 `ESP32BASE_PROFILE_FULL`，应用层只负责灌溉业务：阀门控制、流量计量、定时计划、Web/API、记录和告警。

旧版需求文档保留在 `old-docs/`，只作为背景材料；当前实现不继承旧 EC11 编码器、旧 API 或旧数据格式。

## 当前能力

- 固定 4 路阀门控制，默认启用第 1 路和第 2 路；实际焊接几路就在灌溉设置中启用对应水路。
- 手动浇水：按路独立启动、计时和停止；同一路互斥，不同路可并行。
- 流量计数：4 路脉冲输入，按路估算水量和流速。
- 水流异常：浇水中无脉冲超时自动关阀并记录。
- 漏水监控：待机状态检测异常脉冲并告警。
- 计划浇水：每路最多 6 条计划，总计 24 个计划槽；计划执行结果按 `planId + 日期 + 分钟` 持久化，避免重启后重复执行或漏记已处理状态；重启/NTP 同步后仍处理宽限期内计划。
- 单次跳过：API 已支持按 `planId + 日期` 跳过/取消跳过；当前 Web 页面仍以计划配置为主，近期计划视图尚未作为页面能力承诺。
- Web/API：首页、计划配置、历史记录、水路设置、流量校准和状态 API；手动浇水合并在首页，业务页面使用 Esp32Base UI baseline，不复制基础库页面 CSS。
- 记录与事件：浇水记录用于展示实际浇水任务，启动时会从定长记录文件重建元数据；事件记录只保存灌溉业务重要事件，并使用 `Esp32BaseAppEventLog` 的通用应用事件日志能力。
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

`Esp32BaseAppConfig` 已启用，以保留基础库默认底部导航中的 App Config 入口；部分系统级业务参数通过 App Config 注册，水路和计划仍由本项目业务页面管理。`Esp32BaseAppEventLog` 已启用，容量为 256 条，基础库负责应用事件的固定容量环形存储、分页、CSV 导出和清空。

## 运行入口

- 业务首页：`/index`
- 系统工具：`/esp32base`
- 状态 API：`/api/v1/status`
- 水路设置：`/irrigation/zones`
- 设置 JSON：`/api/v1/config`
- 记录 JSON：`/api/v1/records`
- 事件 JSON：`/api/v1/events`
- 基础库应用事件页：`/esp32base/app-events`
- 基础库应用事件 JSON：`/esp32base/api/app-events`
- 基础库应用事件 CSV：`/esp32base/app-events.csv`

当前默认 Web 账号密码和 OTA 调试配置保留在源码/`platformio.ini` 中，目的是方便本地调试；这不是缺陷项。实际部署前按现场需要调整。

## 硬件假设

- 阀门 PWM 输出：第 1 路 GPIO16，第 2 路 GPIO14，第 3 路 GPIO13，第 4 路 GPIO27。当前代码假设输出有效即开阀。
- 流量计输入：第 1 路 GPIO32，第 2 路 GPIO35，第 3 路 GPIO36，第 4 路 GPIO39。GPIO35/36/39 没有内部上拉；GPIO32 在本设计中也按板级上拉处理，YF-S201 等开漏输出传感器必须使用外部或板载上拉。
- GPIO0/BOOT：恢复出厂长按键，同时也是 ESP32 启动模式脚。按住上电或复位会进入下载模式；固件运行后长按 3 秒才触发仅配置恢复。
- 默认软件启用第 1、2 路，硬件按实际焊接启用对应水路；本地控制仍使用独立按键，不采用 `old-docs/02-硬件设计说明书.md` 中的旧 EC11 方案。

## 文档索引

- [项目协作规范](AGENTS.md)
- [总体计划](PROJECT_PLAN.md)
- [当前需求](docs/01_requirements_v1.md)
- [实现状态与下一阶段](docs/02_next_implementation_plan.md)
- [Web 验证清单](docs/03_web_validation_checklist.md)
- [事件字段语义](docs/04_event_fields.md)
- [Web 静态原型说明](prototypes/web/README.md)

## 验证状态

当前自动验证以 `node scripts/check-web-structure.mjs` 和 `pio run` 为主。阀门极性、流量计稳定性、GPIO0 误触、功耗、OTA、真实水路启动延迟等仍需区分实机验证结果，不能只按编译通过视为完成。
