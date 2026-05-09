# ESP32 智能浇花系统 - 代码与文档全面评估问题记录

> 评估日期：2026-05-09
> 评估范围：本项目根目录下 `AGENTS.md`、`PROJECT_PLAN.md`、`docs/`、`include/`、`src/`、`platformio.ini`、`prototypes/`（不含 `old-docs/` 中的历史背景文档）。
> 评估目的：识别代码缺陷、设计问题、安全/可靠性隐患、文档与代码不一致项；只记录问题，不修复。

---

## 严重程度说明

- 🔴 **严重**：影响安全/可靠性、数据丢失、明显业务逻辑错误、违反核心设计原则。
- 🟠 **较高**：用户可观察到的功能缺陷、设计缺陷、协议生成错误、可能误触发的情形。
- 🟡 **中等**：边缘场景下的不严谨之处、可维护性/一致性问题、易混淆的 UI/UX 表现。
- 🟢 **轻微**：风格/可读性、冗余代码、性能优化空间。

---

## 一、代码层面发现的问题

### 1.1 浇水会话与状态机

#### 🟠 1. `WateringSession::handle()` 启动初期可能误判"水流异常"
**位置**：[src/domain/WateringSession.cpp:177-194](src/domain/WateringSession.cpp:177)

`startRoadByIndex` 中 `lastPulseMs = now`，随后 `handle()` 用 `noPulseMs = now - lastPulseMs` 与 `flowNoPulseTimeoutSec * 1000` 比较。当 `flow_no_pulse_timeout_s = 1`（合法配置）时，从开阀到首个脉冲到达只需要 1 秒以上即误判异常关阀。常闭电磁阀机械响应 + 管路水路启动通常需要 1-3 秒。
- 默认 10 秒尚可接受；但配置允许 1 秒，文档同时声称"配置范围 1-60 秒，1、10、60 为有效值"，1 秒在真实硬件上几乎必然误触发。
- 此外文档 [docs/01_requirements_v1.md:99-100](docs/01_requirements_v1.md:99) 写的是"启动后 10 秒内无流量脉冲判定异常"，而代码并未保证启动后有最少缓冲窗口；只要任何相邻两次 loop 之间间隔 ≥ timeout，就会误判。

#### 🟠 2. `WateringSession::stopAll()` 在 `REASON_REPLACED` 路径会污染事件来源
**位置**：[src/domain/WateringSession.cpp:232-234, 302-309](src/domain/WateringSession.cpp:232)

在 `startManual()` 中，若已有会话则先调用 `stopAll(REASON_REPLACED, ...)`。`stopAll` 末尾会写一次 `TYPE_WATER_STOP` 事件，使用 `eventSource(g_session.source)`——而此时 `g_session.source` 仍是旧会话的来源（如 PLAN）。如果一个手动会话替换了一个计划会话，事件流会出现"PLAN→stop→replaced"紧跟一个新的 "WEB→start"，逻辑上没错；但**记录**写入的 `record.source` 也是旧来源，对外看起来是被替换的旧会话，可能造成统计混乱（实际行为是 OK 的，但语义需要文档说明）。

#### 🟠 3. `WateringSession::stopRoad()` 在非活动状态下直接关阀，但跳过事件/记录
**位置**：[src/domain/WateringSession.cpp:316-318](src/domain/WateringSession.cpp:316)

```cpp
if (!g_session.active) {
    return ValveController::off(road, textReason);
}
```
若设备因异常状态有阀门处于打开状态而 `g_session` 已结束，`stopRoad` 会调用 `ValveController::off` 关阀，但不写任何事件，事后排查时没有痕迹。漏水监控、紧急停止、Web 停止全部都应该至少留事件。

#### 🟡 4. `stopRoad()` 在已 RUNNING/PENDING 状态下事件第二个数值参数固定为 0
**位置**：[src/domain/WateringSession.cpp:325-331](src/domain/WateringSession.cpp:325)

```cpp
(void)EventStore::append(EventStore::TYPE_WATER_STOP,
                         eventSource(g_session.source),
                         road,
                         static_cast<uint8_t>(reason),
                         status.targetSec,
                         0,                  // 与其他位置传 r2 targetSec 不一致
                         textReason);
```
其他 `TYPE_WATER_STOP` 调用第二个数值传的是 `targetSec` 数组，单路停止时第二位置硬编码 0，CSV/JSON 导出后语义不一致，给数据分析带来隐患。

#### 🟢 5. `finalReason()` 仅在所有路结束后被调用，但即使中途某路 ERROR、其他路完成，也会强制将整个会话标记为 ERROR
**位置**：[src/domain/WateringSession.cpp:76-83](src/domain/WateringSession.cpp:76)

`maybeFinishSession` 用 `finalReason(REASON_COMPLETED)` 覆盖：只要任意一路 ROAD_ERROR，整段会话 stopReason 变 error。这与 [docs/01_requirements_v1.md:91-93](docs/01_requirements_v1.md:91)"某一路异常关闭，不影响另一路继续完成"的精神有出入：路级别确实独立，但会话级 stopReason 不区分"全部完成"和"全部完成但其中一路出过异常"。建议在记录中保留 `roads[i].state` 区分，这一点已经做了；但会话级 stopReason 把状态吞并了，依然偏粗。

---

### 1.2 浇水计划与定时调度

#### 🔴 6. `WateringPlanScheduler` 的"同分钟去重"在跨日时存在边界遗漏
**位置**：[src/domain/WateringPlanScheduler.cpp:88-104](src/domain/WateringPlanScheduler.cpp:88)

```cpp
if (minuteOfDay == g_lastMinuteOfDay && today == g_lastYmd) {
    return;
}
g_lastMinuteOfDay = minuteOfDay;
g_lastYmd = today;
```
- 设备重启后 `g_lastMinuteOfDay = 1440, g_lastYmd = 0`，第一次进入 `handle()` 时即使分钟未变，也会进入循环。如果在同分钟内重复进入（多次 NTP 同步切换、loop 迭代），由于 `plan.lastRunYmd` 在 NVS 上有持久化，"该计划今天已运行"作为兜底，避免重触发——但 `Esp32BaseConfig::setInt` 是否同步落盘要看 `Esp32BaseConfig::flushAll()` 调用频率。如果掉电窗口在 `triggerPlan` 内、`setLastRunYmd` 之后但未 flush 之前，重启后 `lastRunYmd` 仍是旧值，可能在同一天再次触发同一计划。
- NTP 时间被回拨（云端校正）会让某分钟计划被重新触发；同样依赖 `lastRunYmd` 兜底。

建议明确 NVS flush 时机或在内存里多一份 `lastRunMinute`。

#### 🟠 7. `triggerPlan` 在已有会话时记录的 record 数据语义混乱
**位置**：[src/domain/WateringPlanScheduler.cpp:41-66](src/domain/WateringPlanScheduler.cpp:41)

为"计划被跳过"创建 record 时：
- `record.sessionStartedMs = millis()`，`sessionEndedMs = sessionStartedMs`：表示"开始即结束"。
- `record.roads[i].state = ROAD_STOPPED`，但是 `startedMs/endedMs/startedPulseCount/endedPulseCount` 都没有赋值（保持 `Record record = {}` 的默认 0）。
- record 中 `enabledRoads` 用的是当前 SettingsStore 的 mask，而非"计划当时的"。

这种 record 在 CSV/JSON 导出后，会出现"目标 5 分钟、实际 0 秒、开始时间 0"的诡异行（uptime 0）。如果只是为了写一条"被跳过的痕迹"，建议改用 EventStore 单独记录，不要混入 RecordStore。

#### 🟡 8. `shouldRunOnDate()` 内重复调用了 `validate()`
**位置**：[src/storage/PlanStore.cpp:189-198](src/storage/PlanStore.cpp:189)

`begin()` 已经在加载阶段对每个 plan 进行 `validate`，invalid 直接替换为默认 plan；后续任何 `set()` 也都先 `validate`。`shouldRunOnDate` 再次校验属于冗余，但在每分钟扫描调度时执行 8 次 validate（含一次 mktime），会带来不必要的开销。

#### 🟡 9. `PlanSkipStore` 永不自动清理过期跳过项
**位置**：[src/storage/PlanSkipStore.cpp:79-94](src/storage/PlanSkipStore.cpp:79)

- `Capacity = 32` 的 entry 一旦填满，按 FIFO 淘汰（不是按日期淘汰）。
- 已经过去的 ymd 永远占据条目，浪费容量；同时每次 `setSkipped`/`clearSkipped` 都会把全部 32 个槽位写一遍 NVS（`persist()` 是全量写），写放大严重，长期运行会显著缩短 NVS 寿命。

#### 🟡 10. `PlanSkipStore::begin()` 重复持久化
**位置**：[src/storage/PlanSkipStore.cpp:54-72](src/storage/PlanSkipStore.cpp:54)

启动时无脏数据也会强制 `(void)persist()`，导致每次开机都把全部 32 个槽位写一次 NVS，同样存在 NVS 写放大。

---

### 1.3 流量计

#### 🟡 11. 流量计 GPIO 缺少软件层文档说明的外部上拉
**位置**：[src/domain/FlowMeter.cpp:39-43](src/domain/FlowMeter.cpp:39)

ESP32 的 GPIO34/35 是**纯输入引脚，没有内部上拉/下拉**。`pinMode(... INPUT)` 仅配置方向，YF-S201 通常是开漏输出，硬件必须外部上拉。代码本身没有问题，但项目当前**没有任何运行时检查或文档级 TODO 提醒"未接传感器时输入悬空可能误触发漏水告警"**。建议在 README/AGENTS.md 中显式记录该硬件假设。

#### 🟡 12. 流量计 ISR 使用普通 `++`，未用 `portENTER_CRITICAL`
**位置**：[src/domain/FlowMeter.cpp:19-25](src/domain/FlowMeter.cpp:19)

```cpp
void IRAM_ATTR onFlow1Pulse() { ++g_pulses[0]; }
```
ESP32 32-bit `++` 通常是非原子的多条指令，但 IRAM ISR 中只有这一条增量、没有其他干扰。读端使用 `noInterrupts()/interrupts()`，理论上在单核 + ISR 抢占模型下能避免读到中间态；但 ESP32 是双核，`noInterrupts()` 仅屏蔽**当前核**中断。如果中断绑核与业务 loop 不在同一核，存在极低概率读到撕裂值。建议用 `portENTER_CRITICAL_ISR`/`portENTER_CRITICAL`。

#### 🟡 13. `FlowMeter::reset()` 不被任何业务调用
**位置**：[src/domain/FlowMeter.cpp:83-93](src/domain/FlowMeter.cpp:83)

仅在 header 中暴露，没有调用方。如果是为了校准/清零的外部接口，缺少对应的 Web/API。死代码或 TODO，建议补 API 或删除。

---

### 1.4 漏水监控

#### 🟠 14. `LeakMonitor::handle()` 在告警触发后立即重置窗口，可能"消音"持续告警
**位置**：[src/domain/LeakMonitor.cpp:75-95](src/domain/LeakMonitor.cpp:75)

第一次告警后调用 `ValveController::allOff("idle leak alert")`，但只在 `firstAlert` 时写一次事件。之后每个窗口都会重置 windowStartPulses，由于阀门已关，新增脉冲应该为 0，所以不会再告警——但如果阀门粘连导致水流持续，每个窗口都重置 windowStartPulses 等于"重新开始计数"，理论上每个窗口又会触发告警写日志（`firstAlert` 已 false 不再写事件，但 `g_roads[i].alert = true` 重复赋值）。这部分逻辑没有实质问题，但代码写得拐弯，可读性差。

#### 🟡 15. `monitoringAllowed()` 仅判断 `WateringSession::isActive` + `ValveController::isOpen`
**位置**：[src/domain/LeakMonitor.cpp:36-40](src/domain/LeakMonitor.cpp:36)

阀门关闭瞬间到水管中的余水真正停下需要数秒（特别是常闭阀机械延迟 + 重力沉降）。当前实现在阀门刚关、漏水监控立即开始计数，可能将"余水尾流"误判为漏水告警。建议加一个最小空闲时间（例如阀门关闭后等待 N 秒再开始监控）。

---

### 1.5 维护与恢复出厂

#### 🔴 16. GPIO0/BOOT 长按"恢复出厂请求"无后续触发路径
**位置**：[src/domain/SafetyManager.cpp:93-103](src/domain/SafetyManager.cpp:93)、[src/domain/MaintenanceService.cpp](src/domain/MaintenanceService.cpp)

`SafetyManager::handle()` 在长按 3 秒后将 `g_factoryResetRequested = true`，写一条 `TYPE_FACTORY_RESET_REQUESTED` 事件，但**没有任何模块再读这个标志推动后续流程**（`MaintenanceService::handle()` 只对 `g_pending`，而 `g_pending` 仅由 `requestFactoryReset()` 设置，后者只被 Web Handler 调用）。

结果：本地 BOOT 长按只在事件日志中留下"用户按了"的痕迹，**实际无法仅通过本地按键完成恢复出厂**。设置页 [src/web/IrrigationWeb.cpp:840](src/web/IrrigationWeb.cpp:840) 显示 "已由 BOOT 键请求"，但页面里也没有"确认执行"的入口，需要用户走"恢复出厂"独立表单。

文档 [docs/01_requirements_v1.md:142](docs/01_requirements_v1.md:142)、[PROJECT_PLAN.md:122](PROJECT_PLAN.md:122) 多处声明"GPIO0/BOOT 长按 3 秒进入恢复出厂确认流程"，但当前阶段缺少 LCD/菜单确认 UI，业务上未闭环。建议在 README 或 docs/02 中显式标注"本地按键触发尚未实装，仅记录请求事件"。

#### 🟡 17. `MaintenanceService::handle()` 的 `EventStore::append` 在 `EventStore::clear()` 之后
**位置**：[src/domain/MaintenanceService.cpp:38-48](src/domain/MaintenanceService.cpp:38)

当 `clearRecords = true` 时，`EventStore::clear()` 先执行，紧接着 `EventStore::append(TYPE_FACTORY_RESET_EXECUTED ...)` 写入。clear 后写入是合法的（store 已重新初始化），但 record 流和 event 流在重启前的最后一条信息容易让现场排查产生误解（"我清了又有数据"）。建议要么在 clear 前先 append、要么 clear 不清最新这一条。

#### 🟡 18. 工厂复位的 750ms 延时硬编码且无 Web 反馈
**位置**：[src/domain/MaintenanceService.cpp:31](src/domain/MaintenanceService.cpp:31)

`requestFactoryReset()` 立即返回 200/302 给浏览器，loop 750ms 后才执行清空 + 重启。如果用户在这个窗口内继续操作（例如再次点击某个 POST），可能在被清空前后产生不一致行为（虽然概率低）。当前没有"已请求中、其他写操作被锁定"的保护。

---

### 1.6 安全/锁定/按键

#### 🟡 19. `SafetyManager::setLocked()` 在已是相同状态时仍然写 NVS + 事件
**位置**：[src/domain/SafetyManager.cpp:110-125](src/domain/SafetyManager.cpp:110)

如果用户连按 lock 按键（防抖后多次触发），每次都会 `Esp32BaseConfig::setBool` 并写一条 CONFIG_CHANGED 事件。建议先比较 `g_locked == locked` 再决定是否落库。

#### 🟡 20. 锁定状态下消费按键事件的设计与文档相反
**位置**：[src/domain/SafetyManager.cpp:43-47](src/domain/SafetyManager.cpp:43)

代码：
```cpp
if (g_locked) {
    (void)g_startOk.wasPressed();
    (void)g_menuBack.wasPressed();
    return;
}
```
锁定状态下"消费"按键事件意味着：用户先按下 START/OK→进入锁定→解锁后这次按键不会再触发开始浇水。但 GPIO0/BOOT 长按仍会被检测（因为它走 `wasLongPressed`），逻辑一致性还行，但这个"消费但不响应"的设计与文档中"锁定后这些按键无效"的语义稍有差异（"无效"通常指"忽略"）。需要在文档中明确表述。

#### 🟡 21. 按键起点的初始稳态不会触发事件，但 `m_pressEvent` 没有 begin 阶段额外保护
**位置**：[src/io/ButtonInput.cpp:19-29](src/io/ButtonInput.cpp:19)

`begin()` 中 `m_lastRawPressed = readPressed(); m_stablePressed = m_lastRawPressed;` 然后 `m_pressEvent = false`。若设备开机时 GPIO0 已经被按住（用户重启时按住 BOOT 按键），不会立即被识别为长按事件——但**3 秒后**仍然可能被 `wasLongPressed` 判定为长按（因为 `m_stableSinceMs = now`，`now - m_stableSinceMs` 在 3 秒后达到 3000ms）。这导致每次冷启动如果 BOOT 没及时松开，3 秒后会触发恢复出厂请求事件（仅事件，无实际后果）。

---

### 1.7 配置存储与数据完整性

#### 🟡 22. `RecordStore`/`EventStore` 一次性 `calloc` 整文件大小
**位置**：[src/storage/RecordStore.cpp:44-49](src/storage/RecordStore.cpp:44)、[src/storage/EventStore.cpp:45-51](src/storage/EventStore.cpp:45)

`createEmptyStore` 直接 `calloc(total, 1)` 然后一次写入：
- Record：256 × `sizeof(Record)`，按 80B 估算约 20KB。
- Event：256 × `sizeof(Event)`，按 64B 估算约 16KB。

ESP32 启动初期堆碎片小，能分配；但 OTA 进行中、Web 并发请求中执行 clear 时存在分配失败的可能，导致 store 进入未就绪状态。建议改成分块写零或使用 `Esp32BaseFs` 的预分配/截断 API。

#### 🟡 23. `RecordStore::append`/`EventStore::append` 每条都写 3 个 NVS key
**位置**：[src/storage/RecordStore.cpp:146-152](src/storage/RecordStore.cpp:146)、[src/storage/EventStore.cpp:166-168](src/storage/EventStore.cpp:166)

`head`、`count`、`next_id` 三个 key 每次落盘。如果一次会话写多条事件 + 一次记录 = 4 次 append × 3 key × 2 store = 大量写入。NVS 有磨损均衡，但仍然不必要地放大。可考虑：
- 把这些元数据合到一个 blob 写一次。
- 或允许"deferred"，在 loop 空闲或会话结束时统一刷盘。

#### 🟢 24. `RecordStore::Record` / `EventStore::Event` 没有显式打包/对齐声明
**位置**：[src/storage/RecordStore.h:19-45](src/storage/RecordStore.h:19)、[src/storage/EventStore.h:29-43](src/storage/EventStore.h:29)

虽然项目只在 ESP32 一种平台上运行，但写入二进制文件、且字段顺序敏感，缺少 `__attribute__((packed))` 或显式 `static_assert(sizeof(Record) == ...)`，未来若任意人调整成员或者升级 GCC，文件版本号 `kVersion` 不变但实际布局变了，会丢历史数据且不报错。

#### 🟡 25. `RecordStore::append` 的 `g_nextId` 溢出回绕到 1
**位置**：[src/storage/RecordStore.cpp:142-144](src/storage/RecordStore.cpp:142)

```cpp
++g_nextId;
if (g_nextId == 0) g_nextId = 1;
```
- ID 复用会让外部分析工具出现"看似时间倒退的同 ID"问题。
- 实际 4G 条 record 在嵌入式上几乎不可能达到，仅理论问题。

#### 🟢 26. `record/event` 的 `id` 与 `next_id` 被持久化，但 `clear()` 全部重置为 1
**位置**：[src/storage/RecordStore.cpp:155-170](src/storage/RecordStore.cpp:155)

clear 后 ID 从 1 开始，与历史导出的 CSV/JSON 中 ID 重叠。如果用户先导出旧记录、清空、再导出新记录，两份文件 ID 域会冲突。考虑保留 next_id 单调递增，或在导出时附 reset_count。

---

### 1.8 Web/API

#### 🔴 27. `handleStatusApi` 在 `ESP32BASE_ENABLE_WIFI` 关闭时输出非法 JSON
**位置**：[src/web/IrrigationWeb.cpp:1011-1025](src/web/IrrigationWeb.cpp:1011)

```cpp
#if ESP32BASE_ENABLE_WIFI
    // 路径里 "ip":" 末尾不带闭合引号，依靠后续 chunk 闭合
    Esp32BaseWeb::sendChunk(",\"ip\":\"");
    char ip[24] = "";
    (void)Esp32BaseWiFi::ip(ip, sizeof(ip));
    Esp32BaseWeb::writeJsonEscaped(ip);
#else
    writeBool(false);
    Esp32BaseWeb::sendChunk(",\"ssid\":\"\",\"rssi\":0,\"ip\":\"\"");   // ← 这里 "ip":"" 已经闭合
#endif
    Esp32BaseWeb::sendChunk("\"},\"time\":{\"synced\":");                 // ← 又补了一个 "
```
- 当 WIFI 启用：拼成 `,"ip":"<ip>"},"time":...` ✅
- 当 WIFI 关闭：拼成 `,"ip":""""},"time":...`（连续 3 个引号）❌ 不是合法 JSON。

虽然 `ESP32BASE_PROFILE_FULL` 默认启用 WiFi，目前不会触发，但只要任何人改 profile 或手动关 WiFi 就会暴露。属于潜在数据完整性 bug。

#### 🔴 28. AGENTS.md 强制要求"所有改变状态的表单操作都必须有 JS confirm"，但设置页 modal 关闭了 confirm
**位置**：[src/web/IrrigationWeb.cpp:713](src/web/IrrigationWeb.cpp:713)

设置 modal 表单使用 `data-confirm='off'`，与 [AGENTS.md:16](AGENTS.md:16) "Web 页面中所有改变状态的表单操作都必须有 JavaScript `confirm`" 直接冲突。git log `a426938 Allow settings modal saves without confirmation` 显示这是有意改动，但 AGENTS.md 未同步修改。

要么 AGENTS.md 放宽规则、要么恢复 confirm。无论选哪种，都是**规则与实现不一致**。

#### 🟠 29. `handleConfigApi` 用"哪个参数存在"做开关式分发，多参数场景静默失败
**位置**：[src/web/IrrigationWeb.cpp:1063-1094](src/web/IrrigationWeb.cpp:1063)

`if (hasParam(...)) ... else if (hasParam(...)) ...` 串行检查，第一个匹配的参数被处理，其它被忽略；如果两个参数同时出现（攻击者自构 form），只有一个生效，UI 不会反馈。同时整个 handler 一次最多保存一项。设置 modal 一次只改一项尚可，但 API 设计本身缺少多字段批量保存能力。

#### 🟠 30. `handleConfigApi` 错误返回 400 JSON，成功返回 302 重定向
**位置**：[src/web/IrrigationWeb.cpp:1095-1101](src/web/IrrigationWeb.cpp:1095)

成功路径 `redirectTo("/irrigation/settings")`；失败路径直接返回 `{"ok":false,"error":"invalid_config"}`。
- 浏览器表单提交看到 400 时只能看到原始 JSON，没有任何重定向回去的反馈，用户体验差。
- API 调用方又会被重定向折腾。
建议区分"业务页面表单"和"API 调用"，要么页面用 PRG（POST/Redirect/GET）+ flash message，要么 API 一律返回 JSON 由前端处理。

#### 🟡 31. `handlePlansApi` POST 时间字段使用 `atoi` 不严格校验
**位置**：[src/web/IrrigationWeb.cpp:1209-1211](src/web/IrrigationWeb.cpp:1209)

```cpp
if (Esp32BaseWeb::getParam("time", text, sizeof(text)) && strlen(text) >= 5) {
    plan.minuteOfDay = static_cast<uint16_t>(atoi(text) * 60 + atoi(text + 3));
}
```
`atoi("ab:cd")` 返回 0，`atoi("12:cd")` 返回 12 但分钟为 0。任何不符合 HH:MM 的输入会被静默接受为 00:00。HTML5 `type='time'` 在浏览器层面有约束，但伪造 POST 仍可绕过；服务端缺少明确的格式校验。

#### 🟡 32. `handleWaterStartApi` 静默接受非法分钟数为 0
**位置**：[src/web/IrrigationWeb.cpp:1109-1126](src/web/IrrigationWeb.cpp:1109)、`minutesToSeconds` [src/web/IrrigationWeb.cpp:112-114](src/web/IrrigationWeb.cpp:112)

`minutesToSeconds(uint16_t minutes)` 在 `>240` 时返回 0。用户提交 `r1_min=300` 会被解释为该路不参与，没有错误提示。前端 `max=240` 已限制，但没有服务端校验+反馈对应错误，问题会被吞掉。

#### 🟡 33. `handlePlanSkipApi` `skip_day` scope `remaining` 仅当 `ymd == currentYmd()` 时生效
**位置**：[src/web/IrrigationWeb.cpp:1252-1262](src/web/IrrigationWeb.cpp:1252)

如果时间未同步 (`currentYmd()` 返回 0)，`remaining` 退化为按日期 0 比较——所有计划被无差别 skip。极端边界，但应该对"时间未同步"场景拒绝跳过操作。

#### 🟡 34. `/api/v1/records.csv`、`/api/v1/events.csv` 硬编码导出最近 50 条
**位置**：[src/web/IrrigationWeb.cpp:1378](src/web/IrrigationWeb.cpp:1378)、[src/web/IrrigationWeb.cpp:1443](src/web/IrrigationWeb.cpp:1443)

容量 256，CSV 一次最多导 50。文档要求"原始数据"导出但代码上没分页参数（`limit` 参数仅 JSON 接受）。设备退役、清记录前用户无法完整导出历史。

#### 🟡 35. `handleSettingsPage` 用 modal 直接复用 `/api/v1/config` 端点
**位置**：[src/web/IrrigationWeb.cpp:702-779](src/web/IrrigationWeb.cpp:702)

modal 表单直接 POST 到 `/api/v1/config`。失败时跳到 JSON 400 错误页（参考问题 30）。同时 modal 关闭了 confirm（参考问题 28）。

#### 🟡 36. `handleDebugPage` 的状态 JSON 实际上是硬编码的示例，不是当前状态
**位置**：[src/web/IrrigationWeb.cpp:897-903](src/web/IrrigationWeb.cpp:897)

```cpp
Esp32BaseWeb::sendChunk("...{\"watering\":{\"active\":false},\"plans\":{\"count\":8},\"settings\":{\"roads\":{\"r1\":{\"enabled\":true}}}}</pre>");
```
这是写死的字符串，不会反映真实状态。如果只想做"格式示意"应当在文案上明确——目前用户很可能误以为是当前快照。

#### 🟡 37. `handlePlanConfigPage` "下次执行" 永远显示 `-`
**位置**：[src/web/IrrigationWeb.cpp:580](src/web/IrrigationWeb.cpp:580)

表头存在"下次执行"列但代码硬编码为 `-`。属于"UI 占位但功能未实装"。建议要么实现要么删除该列。

#### 🟡 38. `writeRecentRows` 中 `running` 判定与文档语义不一致
**位置**：[src/web/IrrigationWeb.cpp:479](src/web/IrrigationWeb.cpp:479)

```cpp
const bool running = WateringSession::isActive() && offset == 0 && plan.minuteOfDay == nowMinute;
```
只要当前有任何会话运行 + plan 时间恰好等于当前分钟，就会标"进行中"——即使该会话是手动会话或别的计划触发的。建议在 WateringSession 层暴露 `currentSourceIndex`，区分到底是哪个 plan 在跑。

#### 🟡 39. `writeRecentRows` 第一段过滤是死代码
**位置**：[src/web/IrrigationWeb.cpp:473-474](src/web/IrrigationWeb.cpp:473)

```cpp
if (!plan.enabled && !PlanStore::shouldRunOnDate(plan, ymd)) continue;
if (plan.enabled && !PlanStore::shouldRunOnDate(plan, ymd)) continue;
```
两个条件并集等价于 `if (!shouldRunOnDate(plan, ymd)) continue;`。同时 `shouldRunOnDate` 内部对 `!plan.enabled` 直接返回 false，所以"未启用计划"也会一起被过滤掉——这与之后 `disabled` 显示"已停用"的逻辑相矛盾，导致已禁用计划在这个表里**永远不会出现**。文档说"近期计划页应显示昨日/今日/明日/后天的计划状态"，但禁用计划其实根本看不到。Bug。

#### 🟡 40. `writeUIntText` 与 `writeUInt` 完全相同
**位置**：[src/web/IrrigationWeb.cpp:41-43](src/web/IrrigationWeb.cpp:41)

```cpp
void writeUIntText(uint32_t value) { writeUInt(value); }
```
冗余封装，无任何语义差异，建议删除其一。

---

### 1.9 引脚与硬件假设

#### 🟡 41. GPIO0 既作为按键、又是 ESP32 启动模式选择脚
**位置**：[include/Pins.h:20](include/Pins.h:20)、[src/io/ButtonInput.cpp:20](src/io/ButtonInput.cpp:20)

代码 `pinMode(m_pin, INPUT_PULLUP)`，OTA 完成后冷启动若 GPIO0 因外部按键被按下会进入下载模式。文档已经说明这个限制（[docs/01_requirements_v1.md:142](docs/01_requirements_v1.md:142)），但没有运行时检查或机械防误触建议；建议在硬件文档/外壳设计中加一条"按键不能在启动期间被按住"的红色提示。

#### 🟡 42. 引脚分配与 `old-docs/02-硬件设计说明书.md` 不一致
**位置**：[include/Pins.h](include/Pins.h)、[old-docs/02-硬件设计说明书.md:13-30](old-docs/02-硬件设计说明书.md:13)

旧硬件设计文档：
- 4 路阀门 GPIO13/14/15/16
- 编码器 GPIO25/27/32 + 临时浇水键 GPIO23 + 红停 GPIO26
- 4 路流量计 GPIO34/35/36/39

代码中只用了 2 路阀门、6 个面板按键、2 路流量。AGENTS.md 已声明硬件最多 2 路、放弃 EC11 编码器，但 `Pins.h` 里没有任何注释或文档链接说明"为什么 GPIO15/16/27/32/36/39 不再使用"。新接手的人会困惑。建议在 `Pins.h` 顶部加来源/取舍说明。

#### 🟢 43. `IrrigationPins::DefaultEnabledRoads = 1` 但 `setRoadEnabledMask` 用 `roads >= 2 ? 0x03 : 0x01`
**位置**：[include/Pins.h:26](include/Pins.h:26)、[src/storage/SettingsStore.cpp:230-234](src/storage/SettingsStore.cpp:230)

`setEnabledRoads(roads)` 把 1 当 0x01、>=2 当 0x03。如果未来加第 3 路，这里需要重写。隐式硬编码。

---

### 1.10 编译构建/工程配置

#### 🟢 44. `platformio.ini` 把 webota 用户名密码明文写在配置里
**位置**：[platformio.ini:20-22](platformio.ini:20)

```ini
custom_esp32base_webota_host = 192.168.2.112
custom_esp32base_webota_user = admin
custom_esp32base_webota_password = admin
```
如果工程将来推到公开仓库，等于公开默认凭据。建议改成 `${sysenv.ESP32_OTA_PASSWORD}` 这种。同时 `Version.h` 里默认 Web 用户密码也是 `admin/admin`，文档没提醒首次开机必须立即修改。

#### 🟢 45. `platformio.ini` 显式 include 了 framework 自带库的源码路径
**位置**：[platformio.ini:27-34](platformio.ini:27)

```ini
-I ${platformio.packages_dir}/framework-arduinoespressif32/libraries/FS/src
-I ${platformio.packages_dir}/framework-arduinoespressif32/libraries/Hash/src
...
```
通常 PlatformIO 自动处理 framework 库 include 路径。这种手动注入的方式如果 platform 升级换路径会立即编不过，且与 `lib_deps = WiFi, DNSServer, ESPmDNS, ...` 重复。建议确认是否真有必要。

#### 🟢 46. `lib_deps` 里直接列出了 `WiFi, DNSServer, ESPmDNS, LittleFS, WebServer, Update`
**位置**：[platformio.ini:10-17](platformio.ini:10)

这些是 framework 自带库，通常不需要在 `lib_deps` 中显式添加（让 lib dependency finder 自动发现）。冗余声明但无功能影响。

---

### 1.11 系统/设计层面

#### 🟠 47. 启动流程把 `EventStore::append(BOOT)` 放在 FlowMeter/Safety/WateringSession 之前
**位置**：[src/app/IrrigationApp.cpp:31-39](src/app/IrrigationApp.cpp:31)

`EventStore::append` 调用早于 `FlowMeter::begin()`、`SafetyManager::begin()`，意味着 boot 事件写入完成时部分模块还没就绪。当前没问题；但如果以后 `EventStore::append` 想拿当前阀门状态/流量/会话状态作为 value1/value2，会读到未初始化数据。可考虑把 BOOT 事件移到所有 `begin()` 之后或改用专门的 `applicationReady()` 事件。

#### 🟠 48. 多处状态/事件的 `value1`/`value2` 字段语义未文档化
**位置**：`EventStore::append` 各调用点

`Event::value1`/`value2` 是 int32_t，但每个事件类型用法不一：
- TYPE_CONFIG_CHANGED：value1=mask、value2=0
- TYPE_LEAK_ALERT：value1=delta、value2=threshold
- TYPE_WATER_START/STOP：value1=r1Sec、value2=r2Sec（部分调用 value1=stopReason、value2=targetSec，参考问题 4）

CSV 导出后无法理解每行 value1/value2 是什么含义。需要：
- 文档约定每种 type 下 value 的含义；
- 或者改成 type-specific 字段。

#### 🟡 49. 项目目录中残留 `prototypes/web/` 静态原型与运行时实现耦合不强
**位置**：[prototypes/web/](prototypes/web/)

`prototypes/` 的 HTML 是设计稿，但和 `src/web/IrrigationWeb.cpp` 的实际产出不强一致。原型 README 说"原型用于确认页面结构、不接入构建"，但目前文件还在仓库根，未来人很可能误以为是当前运行的页面。建议：
- 在 README 里明确标记"已实现的真实代码以 `src/web/IrrigationWeb.cpp` 为准"。
- 或者将原型移到 `archive/`、加阅读时间戳。

#### 🟡 50. `MaintenanceService::handle()` 在重启前调用 `Esp32BaseConfig::flushAll()`，但记录/事件使用文件 IO，二者刷盘策略不同步
**位置**：[src/domain/MaintenanceService.cpp:57-58](src/domain/MaintenanceService.cpp:57)

事件 store 通过 `Esp32BaseFs::writeBytesAt` 写入，假定其同步落盘；NVS 通过 `Esp32BaseConfig::setInt` 异步落盘+末尾 `flushAll()`。如果文件系统也存在写缓冲，需要明确调用对应 flush。

---

## 二、文档层面发现的问题

### 2.1 文档与代码/规则不一致

#### 🔴 D1. AGENTS.md 与设置页 modal 现状互斥
- [AGENTS.md:16](AGENTS.md:16) "Web 页面中所有改变状态的表单操作都必须有 JavaScript `confirm`"
- 实际：[src/web/IrrigationWeb.cpp:713](src/web/IrrigationWeb.cpp:713) modal 表单 `data-confirm='off'`
- 影响：规则与实现不一致；新人维护时不知道哪边是权威。

#### 🟠 D2. `docs/01_requirements_v1.md:99-100` "10 秒内无脉冲" vs 代码"任何相邻两次 loop 间隔 ≥ 阈值都判定异常"
代码用的是 `now - lastPulseMs`，并不区分"启动后 10 秒缓冲期"。如果阀门刚开还没出水，立刻就开始计时。文档表述需要校对（参考问题 1）。

#### 🟠 D3. `docs/01_requirements_v1.md:142`/`PROJECT_PLAN.md:122-124` 描述本地恢复出厂流程，但代码未实装
GPIO0 长按只写事件、不能完成本地复位流程。文档应当显式标注"本地按键流程依赖后续 LCD/菜单实装"。

#### 🟡 D4. `docs/02_next_implementation_plan.md` 的"已完成闭环"表述与代码实际状态略有出入
- "已完成"列出"恢复出厂入口"，但实际是 Web 端入口，本地按键未闭环，应当注明。
- "回退原因"中提到 `REASON_REPLACED`，但记录字段命名 `stopReason`，未在文档中提及"replaced"语义如何被前端解读。

#### 🟡 D5. `docs/03_web_validation_checklist.md` 有但代码未实现的检查点
- "记录文件应能自动初始化，启动过程不应触发 task watchdog"——`createEmptyStore` 一次性 `calloc + writeBytes` 18KB（参考问题 22），如果在 OTA 后第一次启动且 NVS 第一次写入，可能触及 watchdog 阈值。建议补一句"已知风险"或在代码加分块写。
- "禁用 `ESP32BASE_ENABLE_WIFI` 验证"——状态 API 在 WiFi 关闭时输出非法 JSON（问题 27），checklist 没覆盖。

#### 🟡 D6. `docs/03_web_validation_checklist.md` 列出了"清除已处理异常表单提交后……写入 `alert_clear` 事件"
代码确实写了 `TYPE_ALERT_CLEAR`，但**事件 source 始终是 `SOURCE_SYSTEM`**（[src/domain/LeakMonitor.cpp:121](src/domain/LeakMonitor.cpp:121)）。Web 操作触发的清除事件 source 应该是 `SOURCE_WEB`，文档未覆盖此区别，导出 CSV 时无法区分"用户清除"和"系统自清"。

#### 🟡 D7. `PROJECT_PLAN.md` 计划阶段 4 提到"系统事件字段保留启动、配置变更、计划变更、浇水启动/停止/异常、漏水告警、恢复出厂等原始事实"
当前 EventStore 提供了类型枚举但缺少 `WIFI_STATUS_CHANGED`、`OTA_*` 事件——基础库或许会发出，但应用层是否汇聚到 EventStore 没有明文说明，文档需要明确"哪些事件由基础库自动转发、哪些需要应用层 subscribe"。

#### 🟡 D8. `docs/01_requirements_v1.md` 5.1 节"路数与路配置：启用路数，范围 1-2"
代码 `setRoadEnabledMask` 接受任意 2-bit 非 0 mask；启用 mask=0x02 时实际"启用路数=1，但只启用路 2"，而 `enabledRoads()` 返回 1 没问题，但 UI 表示用 `road1=未启用`、`road2=已启用`——文档没有明确这种"只启用第 2 路"的支持，需要确认是否是有意设计。

#### 🟡 D9. `docs/01_requirements_v1.md` 5.1 5.2 节"水流异常检测窗口可配置范围 1-60 秒"
项目已实现 1-60 范围校验。但 1 秒在硬件上几乎一定误触发（参考问题 1）。文档应当推荐"建议下限 5 秒"。

#### 🟢 D10. `PROJECT_PLAN.md` 的目标工程结构包含 `src/ui/Display1602.*`、`src/ui/LedIndicator.*`、`src/ui/LocalMenu.*`
当前仓库 `src/` 下没有 `ui/` 目录。`docs/02_next_implementation_plan.md` 已说明"下一阶段：本地交互"。但 `PROJECT_PLAN.md` 目录结构展示的是目标态，新人首次浏览会以为已存在。

#### 🟢 D11. `prototypes/web/README.md` 与最终实现页面命名不一致
- 原型 `dashboard.html`、`status-api.html`、`records.html`
- 实际路由 `/irrigation`、`/irrigation/debug`、`/irrigation/data`
原型 README 未给出原型→实际路径的对照表。

---

### 2.2 文档自身问题

#### 🟡 D12. `AGENTS.md` 与 `PROJECT_PLAN.md` 重复声明基础库引用方式
两个文档都列了 `symlink:///Users/tyg/dir/claude_dir/Esp32Base`、`ESP32BASE_PROFILE_FULL`。文档源头不唯一，未来修改容易漂移。建议有一个权威文档作为 SOT。

#### 🟡 D13. `AGENTS.md:21` "不过度设计。需求尚未优化确认前，只搭建必要基础结构"
但当前已经有完整 8 计划、设备记录、CSV 导出、modal 设置、漏水监控等大量功能。AGENTS.md 这条原则与 `docs/02` 中"已经完成阶段 A-D"的定位呈现互相牵制——本质上需要更新 AGENTS.md，明确当前是"业务闭环阶段"。

#### 🟡 D14. `PROJECT_PLAN.md` 5.x 阶段表述用"阶段 0/1/.../8"，`docs/02` 用"阶段 A/B/C/D"
两套阶段编号同时存在，新人不清楚 `阶段 4`（计划）和 `阶段 D`（计划）是不是同一个里程碑。

#### 🟢 D15. 无顶层 `README.md`
项目根没有 `README.md`，新接手的人无法在 1 分钟内理解项目用途、构建命令、烧录步骤、Web 入口。`AGENTS.md` 是规范文档不是入门文档。

#### 🟢 D16. `docs/03_web_validation_checklist.md` "未实机验证项"清单
4 条未验证项里包括"阀门 HIGH=开阀 是否一致"——这是非常基础的硬件方向校验，如果买了不同极性的固态继电器/MOS 板，HIGH=开阀 假设不成立。建议在 `Pins.h` 旁加 BOM 链接或"开阀电平"配置项让用户在不更改代码的前提下反转。

---

## 三、安全与可靠性专题

### 3.1 安全相关
- 🟠 **默认 Web 凭据 `admin/admin`** 写在源码、`platformio.ini` 中。文档没有"必须立即修改"红字提醒。
- 🟡 **没有日志告警的失败计数**，例如登录失败次数、API 滥用速率。基础库可能提供，但本应用未配置文档。
- 🟢 **CSRF 保护**：依赖 Basic Auth 实现，但没有 SameSite/origin 检查。同局域网攻击者可借用浏览器已认证状态构造恶意 form。属于已知 ESP32 开发板的常见限制。

### 3.2 可靠性相关
- 🟠 **掉电恢复**：当前 `setLastRunYmd` 在 NVS 上落盘是否同步、能否撑过电源抖动，未在文档中验证。72 小时连续运行验收有列，但需要单独的"突然断电"测试。
- 🟠 **NVS 写放大**：参考问题 9、10、23、24，长期运行下 NVS 寿命可能比预期短得多，需要现场实测。
- 🟡 **无看门狗触发记录**：基础库的 watchdog 是否会写 EventStore，文档没说明。设备神秘重启时无法定位是否 WDT 引发。
- 🟡 **流量计满量程行为**：未来流量计如果故障产生超高频脉冲，ISR 拉满 CPU，业务 loop 饥饿。建议在 ISR 加 simple debounce 或脉冲速率上限检测。

---

## 四、推荐处理优先级

### 必须先处理
1. **问题 16**：本地 BOOT 长按恢复出厂流程未闭环——文档承诺 vs. 实际可用性差异大。
2. **问题 27**：`handleStatusApi` 在 WiFi 关闭分支输出非法 JSON——隐患固化在代码里。
3. **问题 28 / D1**：AGENTS.md 与 modal confirm 现状冲突——团队规则需要先一致。
4. **问题 6**：调度器对 NVS flush + 时钟回拨的依赖说明不清——可能造成同一计划重复执行。

### 应当尽快处理
5. **问题 1 / D2 / D9**：`flow_no_pulse_timeout_s = 1` 的实测可行性，文档应至少给出推荐下限。
6. **问题 22 / D5**：18KB 一次性 `calloc + write` 在恶劣场景下触发 watchdog。
7. **问题 39**：禁用计划在"近期计划"页完全消失——业务可见 bug。
8. **问题 7**：被跳过的计划写 `RecordStore` 而不是 `EventStore`，导致记录数据不规范。
9. **问题 30**：`/api/v1/config` 错误返回模式不一致，影响表单提交后的 UX。

### 可纳入下阶段重构
10. 问题 9、10、22、23（NVS/FS 写放大）—— 一并设计批量 flush。
11. 问题 24、48（二进制布局/事件字段语义）—— 锁结构 + 文档化。
12. 问题 36、37、49 等 UI/原型一致性 —— 进入交互阶段时一并处理。

---

## 五、未深入审查/超出范围的项

- `Esp32Base` 基础库内部行为（按 AGENTS.md 规则不在本项目修复）。
- 实际硬件信号完整性、阀门驱动电路、电源浪涌——属于 `old-docs/02-硬件设计说明书.md` 范畴，本次只针对软件实现做静态评估。
- LCD/按键/LED 的运行时表现——本阶段尚未实装。
- OTA 升级实战流程、72 小时稳定性验证——需要真实硬件。

---

> 本报告只作问题陈述，不包含修复方案。建议在团队讨论后选择处理顺序，对每个问题创建独立 PR/任务以便代码评审。
