# Esp32_Irrigation 建设计划

当前需求已先收敛到 [需求优化稿 V1](docs/01_requirements_v1.md)。后续实现按该文档和 [下一阶段实施计划](docs/02_next_implementation_plan.md) 推进，旧文档只作为背景材料。

## 1. 项目定位

本项目实现 1-2 路可裁剪的 ESP32 智能灌溉控制器，面向家庭阳台、楼顶花园等户外长期运行场景。核心目标是稳定、安全、可维护：所有浇水动作必须有明确时长，任何异常或紧急停止都默认关阀。

旧版需求中的核心能力保留：

- 1-2 路 12V 常闭电磁阀独立控制，默认启用 1 路，软件可配置实际启用路数。
- 手动浇水、定时浇水、紧急停止，优先级为：紧急停止 > 手动浇水 > 定时浇水。
- 每路 YF-S201 流量计量与独立校准。
- LCD1602、独立按键、蓝色状态 LED。
- 本地 Web/API 管理、WiFi 配网、NTP/mDNS、文件日志、Web OTA。
- 浇水记录、系统日志、异常告警、恢复出厂、按键锁定、免打扰。

本项目只负责设备自身固件，设备能力边界为局域网 Web/API。复杂自动化可通过局域网 HTTP API 扩展。

## 2. 基础库使用策略

基础库路径：

```ini
lib_deps =
  symlink:///Users/tyg/dir/claude_dir/Esp32Base
```

应用默认启用：

```cpp
#define ESP32BASE_PROFILE ESP32BASE_PROFILE_FULL
#include <Esp32Base.h>
```

计划使用的 `Esp32Base` 模块：

| 模块 | 本项目用途 |
| --- | --- |
| `Esp32BaseLog` | 串口日志、统一日志格式、业务诊断 |
| `Esp32BaseConfig` | NVS 配置、deferred 写入、出厂恢复 |
| `Esp32BaseSystem` | 重启、heap/flash/reset 诊断 |
| `Esp32BaseBus` | 基础事件订阅，联动 WiFi/OTA/健康事件 |
| `Esp32BaseWatchdog` | 长期运行看门狗 |
| `Esp32BaseSleep` | 动态低功耗策略入口 |
| `Esp32BaseFs` | 文件存储、定长记录分页读取和环形覆盖写入 |
| `Esp32BaseFileLog` | 现场日志文件与 Web 日志查看 |
| `Esp32BaseHealth` | loop 健康诊断 |
| `Esp32BaseWiFi` | STA、配网 portal、WiFi 节能切换 |
| `Esp32BaseDns` | 配网 captive portal DNS |
| `Esp32BaseNtp` | 网络时间同步 |
| `Esp32BaseMdns` | 局域网服务发现 |
| `Esp32BaseWeb` | 业务页面、HTTP API、鉴权、HTML/JSON helper |
| `Esp32BaseOta` | Web OTA、回滚、升级状态 |

`Esp32BaseAppConfig` 暂不接入。业务配置仍保留在现有 `/irrigation/settings` 和 `SettingsStore`，后续需要先确认哪些业务参数进入基础库 App Config 页面。

Web 首页模型：

- 本项目声明 `/irrigation` 为业务首页。
- 使用 `Esp32BaseWeb::HOME_COMBINED`：根路径 `/` 进入灌溉业务首页，`/esp32base` 保留为基础库融合/系统工具入口。
- 使用 `Esp32BaseWeb::SYSTEM_NAV_SECTION`：基础库 WiFi、OTA、Logs、Reboot 等系统工具放在页面底部系统入口区。
- 不在应用层实现根路径重定向补丁，全部使用 `Esp32BaseWeb::setHomePath()` / `setHomeMode()` / `setSystemNavMode()` 声明。

## 3. 目标工程结构

```text
Esp32_Irrigation/
├─ AGENTS.md
├─ PROJECT_PLAN.md
├─ platformio.ini
├─ include/
│  ├─ Pins.h
│  └─ Version.h
├─ src/
│  ├─ main.cpp
│  ├─ app/
│  │  └─ IrrigationApp.*
│  ├─ domain/
│  │  ├─ ValveController.*
│  │  ├─ FlowMeter.*
│  │  ├─ LeakMonitor.*
│  │  ├─ MaintenanceService.*
│  │  ├─ SafetyManager.*
│  │  ├─ WateringPlanScheduler.*
│  │  └─ WateringSession.*
│  ├─ io/
│  │  └─ ButtonInput.*
│  ├─ storage/
│  │  ├─ SettingsStore.*
│  │  ├─ RecordStore.*
│  │  ├─ EventStore.*
│  │  ├─ PlanStore.*
│  │  └─ PlanSkipStore.*
│  └─ web/
│     └─ IrrigationWeb.*
```

本地 LCD/LED 菜单尚未实装，因此当前工程结构中没有 `src/ui/`。进入本地交互阶段时再按实际确认后的设计新增，不预留空目录。

## 4. 模块建设路线

### 阶段 0：项目骨架

- 创建 PlatformIO 工程，目标先用 `esp32dev` / `ESP32-WROOM-32 4MB`。
- 引用 `Esp32Base` 本地库，启用 `ESP32BASE_PROFILE_FULL`。
- 设置固件名、版本、hostname、Web Basic Auth 应用默认认证、文件日志 INFO。
- 注册最小业务首页和 `/api/v1/status`，确认 Web 入口可达。
- 建立引脚表：阀门 GPIO13/14，LED GPIO17，I2C GPIO21/22，按键 GPIO0/18/19/23/25/26/33，流量 GPIO34/35。

### 阶段 1：配置与数据模型

- 设计无历史包袱的数据结构：系统配置、路配置、浇花计划、浇水会话、浇水记录、系统事件。
- 使用应用 namespace，例如 `irr_cfg`、`irr_state`，不使用 `eb_`。
- 配置项覆盖所有重要参数：路配置、流量校准、默认执行模式、安全阈值、浇花计划、交互设置、Web/API 设置和维护设置。
- 浇花计划最多 8 个；记录容量目标先以现场诊断和导出够用为准，存储方案实测后再锁定容量。

### 阶段 2：硬件驱动层

- 实现阀门驱动，启动时和异常时强制全关。
- 实现流量传感器中断计数，ISR 只做最小计数，计算放到 loop。
- 实现 LED 状态模式：常亮、慢闪、快闪、休眠微亮/熄灭。
- 实现按键消抖、短按事件和 GPIO0/BOOT 恢复出厂长按检测。
- 实现 6 个面板按键事件：STOP ALL、R1/UP、R2/DOWN、START/OK、MENU/BACK、LOCK。
- 实现 LCD1602 显示抽象，所有字符串限制在 16 字符内。
- RTC DS3231 作为后续硬件时间源接入；若基础库时间能力不足，转交基础库或明确由应用实现硬件 RTC 适配。

### 阶段 3：浇水核心状态机

- 建立统一 `WateringSession`，所有触发来源共享同一流程。
- 会话内每路独立时长、独立倒计时、独立关闭；红色停止键关闭整个会话。
- 实现优先级：紧急停止立即打断一切；手动可打断定时；计划触发时如已有会话则跳过本次并记录原因。
- 支持同时浇水和顺序浇水两种执行模式；顺序模式按路号从小到大执行，不做复杂队列。
- 所有会话必须有倒计时，手动最长 4 小时。
- 浇水启动后默认 10 秒无脉冲判定水流异常并关阀；配置范围 1-60 秒。
- 待机状态 10 秒内检测到 >=3 个脉冲判定漏水/阀粘连。

### 阶段 4：浇花计划与记录

- 实现最多 8 个浇花计划。
- 实现统一循环模型：`cycle_days` 表示周期长度，`cycle_mask` 表示周期内执行日，用同一结构表达每天、每周固定日和浇 N 停 M。
- 实现跳过今天和取消跳过今天。
- 计划触发时如已有浇水会话，跳过本次并记录原因。
- 记录字段保留开始/结束时间、执行模式、每路目标时长、每路实际时长、每路体积、触发类型、结束原因。
- 系统事件字段保留启动、配置变更、计划变更、浇水启动/停止/异常、漏水告警、恢复出厂等原始事实。
- 记录和系统事件均提供分页读取和 CSV 导出能力。

### 阶段 5：Web/API

- 使用 `Esp32BaseWeb::addPage()` 注册轻量业务页面：`/irrigation` 首页、`/irrigation/manual` 手动、`/irrigation/plans` 计划、`/irrigation/settings` 配置、`/irrigation/data` 记录；通过基础库首页模型让 `/` 进入业务首页。
- 使用 `Esp32BaseWeb::addApi()` 注册 `/api/v1/*` 原子 API。
- 首版复用 `Esp32BaseWeb` Basic Auth；业务 API token 等完整 API 设计确认后再加入。
- 登录账号和密码使用 `Esp32Base` Web Auth；认证优先级为已保存认证、应用默认认证、库默认认证。业务配置页只链接 `/esp32base/auth`，不在应用层保存基础库鉴权密码。
- 状态 API 输出 WiFi 名称、RSSI、IP、当前时间、每路流速、配置、记录和事件容量；计划 API 输出 8 个计划的原始配置字段。
- 所有危险动作使用 POST：启动/停止浇水、修改配置、恢复出厂、重启、OTA。
- 不做大型 SPA，采用轻量服务端页面，保证局域网低资源可用。

### 阶段 6：本地交互

- 实现 LCD 待机、浇水、菜单、异常提示页面。
- 菜单层级控制在 3 级内。
- STOP ALL、R1/UP、R2/DOWN 的停止功能任何状态始终有效，包括按键锁定。
- `START/OK` 从主界面快速进入手动浇水。
- LOCK 短按锁定/解锁普通按键。
- GPIO0/BOOT 长按 3 秒直接执行仅配置恢复并重启；Web 恢复出厂仍使用确认文本。

### 阶段 7：低功耗与可靠性

- 活跃窗口默认 10 分钟，按键/Web/API/浇水动作重置。
- 待机进入 WiFi power save / modem sleep 策略，保持 Web 可访问。
- OTA、浇水、配置写入等关键操作期间暂停激进休眠。
- 看门狗、健康日志、文件日志组合用于长期运行诊断。
- 所有异常路径默认关阀并落记录。

### 阶段 8：验证

- 编译验证：PlatformIO `esp32dev`。
- 主流程测试：手动浇水、浇花计划、紧急停止、启用路数裁剪。
- 硬件测试：阀门、流量、LCD、按键、RTC。
- 网络测试：WiFi 配网、Web/API、mDNS、OTA、日志页面。
- 可靠性测试：100 次启停、掉电恢复、72 小时连续运行。
- 计量验收：校准后误差 <= 5%。

## 5. 首批里程碑

1. `M0`：工程可编译，`Esp32Base FULL` 启动，Web/API 最小状态页可访问。
2. `M1`：配置模型、阀门驱动、流量计数、紧急停止闭环。
3. `M2`：手动浇水和记录闭环，包含异常无流量检测。
4. `M3`：定时任务和跳过规则闭环。
5. `M4`：本地 LCD/按键菜单可用。
6. `M5`：Web 页面/API 完整覆盖核心功能。
7. `M6`：OTA、恢复出厂、文件日志、导出能力完成。
8. `M7`：实机验证和 72 小时稳定性测试。

## 6. 当前已识别的基础库关注点

这些不是本项目内补丁项，后续实现遇到真实阻塞时应整理完整提示词转交 `Esp32Base`：

- 业务记录和二进制定长日志优先使用 `Esp32BaseFs::readBytesAt()` / `Esp32BaseFs::writeBytesAt()` 实现分页读取和环形覆盖，不直接 include `LittleFS.h` 或使用 Arduino `File`。
- 同一路径 handler 需要区分 GET/POST 时，使用 `Esp32BaseWeb::currentMethod()`、`Esp32BaseWeb::isMethod()` 或 `Esp32BaseWeb::currentMethodName()`，不直接访问底层 `WebServer`。
- RTC DS3231 不属于 `Esp32Base` 当前定位；如果希望基础库抽象“多时间源融合”，需单独评估是否适合沉淀到基础库。
- `Esp32BaseSleep` 是否足够支持“保持 WiFi Web 可用的 modem sleep 策略”，需要在实机阶段验证。若不足，转交基础库完善。
