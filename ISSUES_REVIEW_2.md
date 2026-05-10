# 第二轮评估报告：修复验证 + 新增问题

> 评估日期：2026-05-09
> 对照基线：[ISSUES.md](ISSUES.md)（第一轮 50 项代码问题 + 16 项文档问题）
> 比对范围：commit `a426938` → `9fda2fb`（即修复批次 `40bcb24`、`9fda2fb`）
> 评估目的：核对每条原问题的修复状态，识别修复过程中引入的新问题。

---

## 一、修复总览

- ✅ **已修复**：26 项（其中代码 19、文档 7）
- 🟡 **部分修复 / 已澄清不修**：6 项
- ❌ **未处理**：34 项（多为低/中优先项，作为遗留任务）
- 🆕 **新引入问题**：2 项（其中 1 项严重回归）

---

## 二、🆕 修复过程中新引入的问题（必须先看）

### 🔴 N1. CSV 导出现在返回 0 行——严重回归

**位置**：[src/web/IrrigationWeb.cpp:1450](src/web/IrrigationWeb.cpp:1450)、[src/web/IrrigationWeb.cpp:1515](src/web/IrrigationWeb.cpp:1515)

为了响应原问题 34（"CSV 硬编码 50 条"），CSV 处理改成了：

```cpp
// /api/v1/records.csv
(void)RecordStore::readLatest(0, RecordStore::capacity(), cb, nullptr);

// /api/v1/events.csv
(void)EventStore::readLatest(0, EventStore::capacity(), cb, nullptr);
```

但 `readLatest` 的签名仍然是：

```cpp
bool readLatest(uint16_t offset, uint8_t limit, ReadCallback callback, void* user);
```

`capacity()` 返回 `uint16_t`，值是 **256**。把 `uint16_t(256)` 赋给 `uint8_t limit` 时，C++ 标准 [conv.integral] 规定结果是 `value mod 2^8`，即 **`limit = 0`**。`readLatest` 内部：

```cpp
uint16_t remaining = g_count - offset;
if (remaining > limit) {
    remaining = limit;     // → remaining = 0
}
for (uint16_t i = 0; i < remaining; ++i) {  // 不执行
    ...
}
```

→ CSV 文件只剩表头一行，**所有记录/事件都不会被导出**。

这是从"只导出最近 50 条"退化到"完全导不出"，验证清单 [docs/03_web_validation_checklist.md:50-58](docs/03_web_validation_checklist.md:50) 里的所有 CSV 检查项都会失败。

修复方向（不在本报告内执行）：把 `readLatest` 的 `limit` 参数升级为 `uint16_t`、或在 CSV handler 内换用其他循环。

**严重程度：🔴 严重，且在线上立即可见。**

---

### 🟡 N2. `WateringPlanScheduler` 的"per-plan 同分钟去重"实际上对大多数计划失效

**位置**：[src/domain/WateringPlanScheduler.cpp:18-19, 35-43, 85-89, 106-109](src/domain/WateringPlanScheduler.cpp:18)

为了响应原问题 6 加入了二级去重数组：

```cpp
uint8_t g_lastTriggeredMinute[PlanStore::MaxPlans] = {};
uint32_t g_lastTriggeredYmd[PlanStore::MaxPlans] = {};
...
g_lastTriggeredMinute[i] = minuteOfDay;        // ❌ 类型错误
...
if (g_lastTriggeredYmd[index] == today &&
    g_lastTriggeredMinute[index] == minuteOfDay) { ... }
```

但 `minuteOfDay` 范围是 0-1439，不能装进 `uint8_t`（最大 255）。具体后果：

- 任何计划时间 ≥ 04:16（即 minuteOfDay ≥ 256）的，赋值时会被截断为 `minuteOfDay & 0xFF`。
- 比较时 `(uint8_t)(720) == (uint16_t)720` 会先把左侧零扩展到 `uint16_t`，但因为之前已经被截断成 `720 & 0xFF = 208`，所以判断 `208 == 720` → false。
- 结果：per-plan dedup 对几乎所有真实使用的浇水时间（早上 7:00=420、晚上 18:00=1080…）**永远不命中**。

实际影响相对低，因为顶部的全局守卫 `g_lastMinuteOfDay`（`uint16_t`）能在 99% 的情况下挡住同分钟重入；但这条二级保险网功能被悄悄破坏了，**未来若有人去掉全局守卫，问题会立即暴露**。也违反了"加保险网就要起作用"的设计意图。

修复方向：把 `g_lastTriggeredMinute` 改成 `uint16_t[]`，并把 `begin()` 中的 `255` 哨兵值改成 `1440` 之类不在合法范围内的值。

**严重程度：🟡 中等，正确性缺陷但当前实际行为正确。**

---

## 三、原问题修复验证

每条按"修复状态 / 验证证据 / 备注"格式列出。

### 3.1 原代码问题（编号沿用 ISSUES.md）

| # | 问题 | 状态 | 验证 |
|---|------|------|------|
| 1 | flow_no_pulse 启动初期误判 | ✅ 已修复 | [src/domain/WateringSession.cpp:13, 182-183](src/domain/WateringSession.cpp:13) 加入 `kStartupFlowGraceMs = 3000UL`，仅在"自启动以来一个脉冲都没有"时把超时下限抬高到 3 秒。 |
| 2 | stopAll(REASON_REPLACED) 来源污染 | ❌ 未处理 | [src/domain/WateringSession.cpp:236-238, 302-309](src/domain/WateringSession.cpp:236) 仍使用旧会话的 source 写 stop 事件/记录。无紧急性。 |
| 3 | stopRoad 非活动状态无事件 | ✅ 已修复 | [src/domain/WateringSession.cpp:320-332](src/domain/WateringSession.cpp:320) 现在写一条 `TYPE_WATER_STOP`/`SOURCE_SYSTEM` 事件。 |
| 4 | stopRoad 事件 value2 固定 0 | ✅ 已修复 | [src/domain/WateringSession.cpp:343-344](src/domain/WateringSession.cpp:343) 改为 `road == 1 ? targetSec : 0` / `road == 2 ? targetSec : 0`，按路放对应槽位。新语义清晰。 |
| 5 | finalReason 把多路结果一锅端为 ERROR | ❌ 未处理 | 仍然是粗粒度。 |
| 6 | 调度器跨日/掉电去重 | 🟡 部分修复 | 加了 `Esp32BaseConfig::flushAll()` 在 `setLastRunYmd` 后；同时加了 per-plan dedup 二级保险，但保险本身有 N2 类型错误。NVS 同步落盘是真实改善。 |
| 7 | 跳过的计划写 RecordStore | ✅ 已修复 | [src/domain/WateringPlanScheduler.cpp:46-58](src/domain/WateringPlanScheduler.cpp:46) 不再 `RecordStore::append`，只写 `EventStore::append(TYPE_WATER_STOP, REASON_SKIPPED, ...)`。 |
| 8 | shouldRunOnDate 重复 validate | ✅ 已修复 | [src/storage/PlanStore.cpp:189-198](src/storage/PlanStore.cpp:189) 移除 `!validate(plan)` 调用。 |
| 9 | PlanSkipStore 不清理过期 | ✅ 已修复 | [src/storage/PlanSkipStore.cpp:65-83, 106-108, 120-122](src/storage/PlanSkipStore.cpp:65) 新增 `pruneExpired(currentYmd())`，begin、setSkipped、clearSkipped 都先剪枝再操作。 |
| 10 | PlanSkipStore::begin 强制持久化 | ✅ 已修复 | 现在仅当 `pruneExpired` 改变状态才 `persist()`；正常启动 0 NVS 写。 |
| 11 | 流量计 GPIO34/35 外部上拉假设 | 🟡 已澄清 | [include/Pins.h:6-8](include/Pins.h:6) 顶部注释提示。运行时仍无检查，文档已说明硬件假设，符合"非缺陷"定位。 |
| 12 | FlowMeter ISR 跨核原子性 | ✅ 已修复 | [src/domain/FlowMeter.cpp:15, 19-25, 65-92](src/domain/FlowMeter.cpp:15) 改为 `portMUX_TYPE` + `portENTER_CRITICAL_ISR`。 |
| 13 | FlowMeter::reset 死代码 | ❌ 未处理 | 仍未接入业务。 |
| 14 | LeakMonitor 重复重置窗口 | 🟡 已澄清 | 行为本身正确，加 settle time 后异常窗口再触发的概率显著降低。 |
| 15 | LeakMonitor 阀关瞬间余水误判 | ✅ 已修复 | [src/domain/LeakMonitor.cpp:15, 23-25, 60-72](src/domain/LeakMonitor.cpp:15) 加入 `kIdleSettleMs = 3000UL`，监控允许的状态切换 3 秒后才开始计数。 |
| 16 | GPIO0 长按恢复出厂未闭环 | ✅ 已修复 | [src/domain/MaintenanceService.cpp:32-39](src/domain/MaintenanceService.cpp:32) `handle()` 现在主动读 `SafetyManager::factoryResetRequested()`，转换为 pending；750ms 后执行"仅恢复配置"并重启。文档 [docs/01_requirements_v1.md:142](docs/01_requirements_v1.md:142)、[PROJECT_PLAN.md:162](PROJECT_PLAN.md:162) 同步更新为"长按直接执行仅配置恢复"。 |
| 17 | EventStore::append 在 clear 之后 | ❌ 未处理 | 工厂复位执行时仍是 `clear → append → restart` 顺序。可接受。 |
| 18 | factory reset 750ms 硬编码 + 无锁 | ❌ 未处理 | 仍存在并发写入风险，但概率极低。 |
| 19 | setLocked 重复写 NVS | ✅ 已修复 | [src/domain/SafetyManager.cpp:111-113](src/domain/SafetyManager.cpp:111) 新增 `g_locked == locked` 早返回；同时 [src/storage/SettingsStore.cpp:255-257, 269-271, 283-285, 298-300, 312-314, 326-328, 337-339, 352-354, 369-371, 386-388](src/storage/SettingsStore.cpp:255) 所有 `set*` 也都加了"值未变即跳过"分支。NVS 写放大问题大幅缓解。 |
| 20 | 锁定状态下消费按键事件语义 | ❌ 未处理 | 文档未细化。 |
| 21 | 启动期间 GPIO0 被按住的 3s 误触 | ❌ 未处理 | [include/Pins.h:23-24](include/Pins.h:23) 仅加了注释，运行时无防护。 |
| 22 | createEmptyStore 一次 calloc 18KB | ✅ 已修复 | [src/storage/RecordStore.cpp:44-63](src/storage/RecordStore.cpp:44)、[src/storage/EventStore.cpp:45-64](src/storage/EventStore.cpp:45) 改成 256 字节缓冲分块 `Esp32BaseFs::appendBytes` + `delay(0)`。每块写入完还会校验文件最终大小。 |
| 23 | append 每次写 3 个 NVS key | ❌ 未处理 | 仍然每条记录写 3 次 NVS。 |
| 24 | 二进制布局未 `static_assert` | ✅ 已修复 | [src/storage/RecordStore.h:47-48](src/storage/RecordStore.h:47) 加入 `static_assert(sizeof(Record) == 88)`、[src/storage/EventStore.h:45](src/storage/EventStore.h:45) 加入 `static_assert(sizeof(Event) == 64)`。布局变化会立刻触发编译失败。 |
| 25 | g_nextId 溢出回绕 | ❌ 未处理 | 几乎不可能触及。 |
| 26 | clear() 重置 ID | ❌ 未处理 | 与 25 同理，纯理论。 |
| 27 | status JSON 在 WIFI 关闭时非法 | ✅ 已修复 | [src/web/IrrigationWeb.cpp:1056-1060](src/web/IrrigationWeb.cpp:1056) WiFi 关闭分支去掉了 `,"ip":""` 末尾的引号，与下一段 `"}, ` 拼接为 `"ip":""},`，合法 JSON。 |
| 28 / D1 | modal 关闭 confirm 与 AGENTS.md 冲突 | ✅ 已修复 | [src/web/IrrigationWeb.cpp:748](src/web/IrrigationWeb.cpp:748) 改回 `data-confirm='确认保存设置？'`，规则与实现一致。 |
| 29 | handleConfigApi 多参数串行分发 | ❌ 未处理 | 仍是 if/else 链。低优先项。 |
| 30 | 错误返回 400 / 成功 302 不一致 | ❌ 未处理 | 仍是同样模式。低优先项。 |
| 31 | handlePlansApi 时间 atoi 静默接受非法 | ✅ 已修复 | [src/web/IrrigationWeb.cpp:112-128, 1248-1251](src/web/IrrigationWeb.cpp:112) 新增 `readMinuteOfDayParam`，严格解析 `HH:MM` 格式，非法立即返回 400。 |
| 32 | handleWaterStartApi 静默接受非法分钟 | ✅ 已修复 | [src/web/IrrigationWeb.cpp:1153-1157](src/web/IrrigationWeb.cpp:1153) 启用路必须有 1-240 范围的合法分钟数，否则 400。 |
| 33 | skip_day scope=remaining 时间未同步 | ✅ 已修复 | [src/web/IrrigationWeb.cpp:1320-1325](src/web/IrrigationWeb.cpp:1320) 当 `today==0` 时直接返回 400 `time_not_synced`。 |
| 34 | CSV 硬编码 50 行 | 🔴 修复引入回归（见 N1） | 改用 `capacity()`，但 `uint8_t` 截断使 CSV 现在只输出表头。**严重回归**。 |
| 35 | 设置 modal 重用 /api/v1/config | ❌ 未处理 | 与 30 一并处理更合适。 |
| 36 | handleDebugPage 硬编码 JSON | ✅ 已修复 | [src/web/IrrigationWeb.cpp:936](src/web/IrrigationWeb.cpp:936) 移除假数据，仅留链接 + "实时内容由接口返回"提示。 |
| 37 | 计划配置"下次执行"列永远是 `-` | ✅ 已修复 | [src/web/IrrigationWeb.cpp:598, 615](src/web/IrrigationWeb.cpp:598) 直接删除该列，避免误导。 |
| 38 | running 误判任意会话 | ✅ 已修复（部分） | [src/web/IrrigationWeb.cpp:512](src/web/IrrigationWeb.cpp:512) 增加 `WateringSession::source() == RecordStore::SOURCE_PLAN` 判断，并暴露了 `WateringSession::source()` 接口。仍无法区分"两个相同分钟的计划中究竟是哪个在跑"，但已大幅收敛误判范围。 |
| 39 | writeRecentRows 死代码导致禁用计划全隐藏 | ✅ 已修复 | [src/web/IrrigationWeb.cpp:507-509](src/web/IrrigationWeb.cpp:507) 删除第一段 `if (!plan.enabled && ...)`，禁用计划现在会以"已停用"badge 出现在每天的表里。 |
| 40 | writeUIntText 与 writeUInt 完全相同 | ❌ 未处理 | [src/web/IrrigationWeb.cpp:41-43](src/web/IrrigationWeb.cpp:41) 仍冗余，纯风格问题。 |
| 41 | GPIO0 启动模式选择脚 | ✅ 已修复（文档） | [include/Pins.h:23-24](include/Pins.h:23) 加注释，硬件层面无运行时检查（也无能力检查）。 |
| 42 | Pins.h 与旧硬件文档不一致 | ✅ 已修复 | [include/Pins.h:6-8](include/Pins.h:6) 加注释说明"当前硬件不是 old-docs/02 的 4 路 EC11 设计"。 |
| 43 | DefaultEnabledRoads 与 mask 隐式映射 | ❌ 未处理 | [src/storage/SettingsStore.cpp:145](src/storage/SettingsStore.cpp:145) 改为直接当 mask 用（删除了 enabledRoadsToMask）。原问题是命名混淆，没有显式注释，仍隐式。 |
| 44 | platformio.ini admin/admin 明文 | ❌ 未处理 | [platformio.ini:20-22](platformio.ini:20) 仍硬编码。 |
| 45 | 手动 include framework 库路径 | ❌ 未处理 | [platformio.ini:27-34](platformio.ini:27) 仍存在。 |
| 46 | lib_deps 重复声明 framework 库 | ❌ 未处理 | [platformio.ini:10-17](platformio.ini:10) 仍存在。 |
| 47 | BOOT 事件在其他 begin 之前 | ✅ 已修复 | [src/app/IrrigationApp.cpp:32-39](src/app/IrrigationApp.cpp:32) 把 `EventStore::append(TYPE_BOOT, ...)` 移到所有 `begin()` 之后，文案改为 "app ready"。 |
| 48 | value1/value2 字段语义未文档化 | ❌ 未处理 | 仍无统一对照表。低优先。 |
| 49 | prototypes/web/ 与运行时强耦合不足 | ❌ 未处理 | 原型未归档/未明确标注。 |
| 50 | FS 与 NVS 刷盘策略不同步 | ❌ 未处理 | MaintenanceService 重启前仅 `flushAll()` NVS。LittleFS 假定同步落盘。 |

---

### 3.2 原文档问题

| # | 问题 | 状态 | 验证 |
|---|------|------|------|
| D1 | AGENTS.md confirm 规则与实现冲突 | ✅ 已修复 | 见原代码问题 28。AGENTS.md 16 行原规则保留，modal 改回符合规则。 |
| D2 | "10 秒缓冲期"文档不实 | ✅ 已修复 | [docs/01_requirements_v1.md:100](docs/01_requirements_v1.md:100) 现在写明"真实硬件建议不低于 5 秒。启动初期应至少保留短暂机械/管路响应缓冲，避免 1 秒配置在刚开阀时误判"。代码也通过 `kStartupFlowGraceMs = 3000UL` 提供保护。 |
| D3 | BOOT 长按文档 vs 代码不一致 | ✅ 已修复 | [docs/01_requirements_v1.md:142-143](docs/01_requirements_v1.md:142)、[docs/02_next_implementation_plan.md:24](docs/02_next_implementation_plan.md:24)、[PROJECT_PLAN.md:162](PROJECT_PLAN.md:162) 全部改为"长按 3 秒直接执行仅配置恢复并重启"。 |
| D4 | docs/02 已完成闭环描述与代码偏差 | ✅ 已修复 | [docs/02_next_implementation_plan.md](docs/02_next_implementation_plan.md) 重新组织为"已完成闭环 / 下一阶段"。 |
| D5 | docs/03 验证清单未覆盖 calloc / WIFI=0 | ✅ 已修复 | [docs/03_web_validation_checklist.md:21](docs/03_web_validation_checklist.md:21) 新增 "禁用 ESP32BASE_ENABLE_WIFI 构建时…仍必须是合法 JSON" 检查。calloc 风险通过代码改成分块 append 解决。 |
| D6 | alert_clear 事件 source 始终 SYSTEM | ✅ 已修复 | [src/domain/LeakMonitor.cpp:126-134](src/domain/LeakMonitor.cpp:126) `clearAlerts(EventStore::Source source)` 接受调用方来源，[src/web/IrrigationWeb.cpp:1196](src/web/IrrigationWeb.cpp:1196) Web 处理传 `SOURCE_WEB`。CSV/JSON 现在能区分用户清除 vs 系统清除。 |
| D7 | EventStore 缺少 WiFi/OTA 转发说明 | ❌ 未处理 | 文档仍未明确哪些事件由基础库转发。 |
| D8 | "仅启用第 2 路"是否为有意设计 | ❌ 未处理 | 文档未明确。 |
| D9 | flow_timeout 1 秒在硬件上误触发 | ✅ 已修复 | [docs/01_requirements_v1.md:100](docs/01_requirements_v1.md:100) 推荐"不低于 5 秒"，代码也加 3 秒缓冲。 |
| D10 | PROJECT_PLAN.md UI 目录尚不存在 | ❌ 未处理 | UI 阶段尚未实装。 |
| D11 | prototypes 命名与实际路由不对照 | ❌ 未处理 | 没有对照表。 |
| D12 | AGENTS.md/PROJECT_PLAN.md 重复声明库引用 | ❌ 未处理 | 文档冗余。 |
| D13 | AGENTS.md "只搭建必要基础结构"与现状不符 | ✅ 已修复 | [AGENTS.md:50](AGENTS.md:50) 改为"当前阶段已进入业务闭环实现；新增能力仍需先确认需求边界，再按最佳方案小步落地"。 |
| D14 | 阶段编号 0/1/.../8 与 A/B/C/D 双轨 | ❌ 未处理 | 两套编号仍并存。 |
| D15 | 无顶层 README.md | ❌ 未处理 | 项目根仍只有 AGENTS.md / PROJECT_PLAN.md / ISSUES.md。 |
| D16 | docs/03 未实机验证项硬件极性提示 | 🟡 部分（更新一条） | [docs/03_web_validation_checklist.md:71](docs/03_web_validation_checklist.md:71) 把"长按是否容易误触"改成"应直接执行仅配置恢复并重启；实机验证是否容易误触，以及是否保留记录和事件"，但 `HIGH=开阀` 的极性问题没有给出可配置项。 |

---

## 四、按严重程度分组的剩余问题

### 🔴 必须先处理
- **N1**：CSV 导出回归（capacity() 截断为 0）。

### 🟡 应当尽快处理
- **N2**：`g_lastTriggeredMinute` `uint8_t` 截断。
- **5**：会话级 stopReason 把单路 ERROR 上升为整体 ERROR。
- **17**：工厂复位 `clear → append → restart` 顺序在掉电窗口可能丢失"已执行"事件。

### 🟢 可纳入下阶段重构
- **2、5、13、18、20、21、23、25、26、29、30、35、40、43、44、45、46、48、49、50**
- 文档：**D7、D8、D10、D11、D12、D14、D15**

---

## 五、修复整体评价

- **关键回归**：1 项（N1）。CSV 导出现已完全失效，是用户立即可见的功能缺陷，必须立即处理。
- **目标命中度**：4/4 🔴 严重项已修复或获得明确澄清。
- **质量改进**：本批修复在以下方向有显著提升：
  - 安全边界：流量启动缓冲、漏水监控 settle 时间、按键长按恢复出厂闭环。
  - 数据完整性：`static_assert` 锁定二进制布局、NVS 写放大显著降低、stopRoad 非活动状态写事件、alert_clear 携带 source。
  - 输入校验：分钟时间格式、手动浇水时长、scope=remaining 在时间未同步时拒绝。
  - 一致性：confirm 规则恢复、文档与代码同步、文档说明硬件配置假设。
- **遗留风险**：
  - CSV 完全失效（N1，紧急）。
  - per-plan dedup 失效（N2，但被全局守卫掩盖，不影响当前行为）。
  - 默认 admin/admin 凭据、platformio.ini 明文密码、NVS 写次数、value1/value2 字段语义、原型 vs 运行时差异等未触及。

---

## 六、推荐下一步

1. **立刻**：修复 N1。最直接方式是把 `RecordStore::readLatest`、`EventStore::readLatest` 的 `limit` 参数改为 `uint16_t`，或在 CSV handler 内自行循环跨多次 `readLatest(0, 255, ...)` 拼接。
2. **顺手**：把 `g_lastTriggeredMinute` 改为 `uint16_t[]`，初始哨兵改成 `1440`。
3. **下一阶段**：把"会话级 stopReason 区分'部分错误 vs 全部完成'"、"NVS 写策略统一为延迟落盘"、"value1/value2 字段语义对照表"作为 next-iteration 主题。
4. **维护**：在 ISSUES.md 中标注 N1/N2，作为未来回归测试用例（"导一份 256 容量的 CSV，应当返回所有可读条目"）。

---

> 本报告只作差异核对，不直接修复任何代码。
