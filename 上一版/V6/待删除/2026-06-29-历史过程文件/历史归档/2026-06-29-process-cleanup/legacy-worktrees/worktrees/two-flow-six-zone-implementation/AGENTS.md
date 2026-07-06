# ESP32 灌溉项目协作规范

本项目是基于 ESP32 的智能灌溉控制器应用项目，当前目标模型为最多 2 个 Flow 流量计、最多 6 个 Zone 电磁阀水路。Zone 可任意归属 Flow 1 或 Flow 2；同一 Flow 下 Zone 互斥运行，不同 Flow 下 Zone 可并行运行。`old-docs/` 中的旧版设计文档只作为需求来源和背景材料；后续需求必须再次优化确认后再实现。新实现不继承旧代码、旧接口或旧数据格式的历史包袱。

## 基本原则

- 项目实现以最佳方案、最优实现为标准，不考虑历史兼容性，不保留旧 key、旧 API、旧行为迁移层。
- 不管是新需求、bug 修复还是优化，都坚决不要历史包袱；不做打补丁式实现、临时方案、兼容式绕路或“先凑合”的处理。
- 如果最佳方案会影响大量代码、文档、配置或既有设计，也必须按最佳实践重构到位，不为了减少改动量牺牲长期正确性。
- 需要引用 `/Users/tyg/dir/claude_dir/Esp32Base` 作为基础库，并优先使用该库提供的所有基础模块能力。
- 默认按 `ESP32BASE_PROFILE_FULL` 规划应用底座，覆盖 Core、Runtime、Bus、Watchdog、Sleep、FS、FileLog、Health、WiFi、DNS、NTP、mDNS、Web、OTA 等基础能力。
- 应用层只实现灌溉业务：阀门控制、流量计量、定时调度、本地交互、业务 Web/API、记录与告警策略；不复制基础库已有能力。
- 如果实现过程中发现涉及基础库能力不足、基础库 bug，或更适合沉淀到基础库的新需求/新设计，不在本项目里打补丁；必须整理成完整提示词，由用户到 `Esp32Base` 项目中完善基础库。
- 业务 namespace 不使用 `eb_` 前缀，避免和 `Esp32Base` 内部 NVS namespace 冲突。
- 所有 restart 走 `Esp32BaseSystem::restart(reason)`；所有 deep sleep 走 `Esp32BaseSleep`；不要直接调用底层重启或 deep sleep API。
- Web/API 输出必须做 HTML/JSON escape；危险操作使用 POST、鉴权、二次确认；Web 页面中所有改变状态的表单操作都必须有 JavaScript `confirm`。
- 业务记录、二进制定长日志、分页读取和环形覆盖写入必须优先使用 `Esp32BaseFs::readBytesAt()` / `Esp32BaseFs::writeBytesAt()`，不要绕过基础库直接 include `LittleFS.h` 或使用 Arduino `File`。
- 同一路径需要同时处理 GET/POST 时，必须使用 `Esp32BaseWeb::currentMethod()`、`Esp32BaseWeb::isMethod()` 或 `Esp32BaseWeb::currentMethodName()` 判断当前请求方法，不要绕过基础库直接访问底层 `WebServer`。
- CSV/JSONL/二进制等导出必须使用 `Esp32BaseWeb::beginResponse()`、`beginCsv()`、`sendChunk()`、`sendBytes()`、`writeCsvEscaped()`、`endResponse()`，不要绕过基础库直接访问底层 `WebServer`。
- 户外长期运行可靠性优先：非阻塞 loop、明确状态机、默认安全关阀、掉电后配置/记录不损坏。
- 不过度设计。需求尚未优化确认前，只搭建必要基础结构，不提前堆完整业务模块空壳。

## 基础库问题转交规则

当遇到基础库问题时，在本项目答复中给出可直接复制到 `Esp32Base` 的完整提示词，至少包含：

```text
项目背景：
我正在实现 /Users/tyg/dir/claude_dir/Esp32_Irrigation，它通过 symlink:///Users/tyg/dir/claude_dir/Esp32Base 引用 Esp32Base，并使用 ESP32BASE_PROFILE_FULL。

遇到的问题/新增需求：
<清楚描述基础库缺失能力、bug 表现或建议设计>

期望行为：
<描述基础库应提供的公开 API、行为、边界条件、错误处理和文档要求>

当前规避状态：
本灌溉项目不在应用层打补丁；等待基础库完善后再按公开 API 接入。

验证建议：
<给出 PlatformIO 示例构建、单元/实机验证、Web/API/OTA/日志等检查项>
```

## 旧文档解释规则

- `old-docs/01-产品需求规格说明书.md` 是产品目标来源。
- `old-docs/03-软件需求规格说明书.md` 是软件行为优先依据。
- 当旧文档之间冲突时，采用更适合新项目边界的方案：例如天气联动不内置到控制器核心，而通过开放原子 API 交给外部系统扩展。
- 旧文档中的 EC11 编码器、固定 4 路、每路一个流量计、启动阶段补偿校准模型不再作为当前实现依据。
- 当前方案为 2 Flow / 6 Zone 板级配置，默认启用 Flow 1 和 Zone 1/2；本地控制使用独立按键在所有 enabled Zone 中选择、启动、停止和 Stop All，不特殊限制 Zone 1/2。
- 当前阶段已进入业务闭环实现；新增能力仍需先确认需求边界，再按最佳方案小步落地。

## 工程实践

- 修改前先阅读相关旧文档和 `Esp32Base` 文档/API，优先沿用基础库模式。
- 手动编辑文件使用 `apply_patch`。
- 新代码保持模块边界清晰，小步构建、逐步验证。
- 能构建时至少运行 PlatformIO 编译；涉及硬件、功耗、流量、RTC、LCD、OTA 的验证，必须区分“已实机验证”和“未实机验证”。
