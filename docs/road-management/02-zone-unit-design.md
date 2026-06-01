# Zone 单元设计

单个水路（Zone）的完整设计。Zone 是灌溉系统的核心领域概念，每路独立自治。

> 术语：Zone = 水路。灌溉行业标准词（Rain Bird、Hunter、Orbit 等品牌统一使用 "Zone 1/2/3..." 表示独立控制的灌溉区域）。

## 一、Zone 的组成

```
Zone (单路自治单元)
│
├── ZoneConfig config         ← 配置快照，外部写入
├── ZoneState state           ← 当前可用状态（循环）
├── ZoneTaskRunner runner     ← 当前任务执行态（线性）
├── uint8_t lastErrorCode     ← 上次错误码（只读信息）
└── bool leakAlert            ← 漏水告警标志
```

配置和运行态严格分离。配置是"这条 Zone 是什么"，运行态是"这条 Zone 在做什么"。

## 二、ZoneConfig

```
ZoneConfig
├── zoneId              // 1-based，不可变
├── name[12]            // 展示用名称
├── valvePin            // 阀门 PWM 引脚，硬件绑定，不可变
├── flowPin             // 流量计引脚，硬件绑定，不可变
├── enabled             // 启用/停用
├── startupPulseLimit   // 启动阶段脉冲数，0 表示不启用启动补偿
├── startupEstimatedMl  // 启动阶段全部脉冲对应的估算水量
├── stablePulsePerLiter // 稳定阶段每升脉冲数
├── startTimeoutSec     // STARTING → ERROR 的超时阈值
├── flowNoPulseTimeoutSec // RUNNING → ERROR 的超时阈值
└── suppressError       // 是否抑制 ERROR 状态
                        //   false（默认）：正常进入 ERROR，禁止再次启动
                        //   true：出错了写记录后回 IDLE，假装正常
                        //   用途：硬件未完整焊接时避免频繁 ERROR 打断调试
```

**配置运行时修改规则**：

| 参数 | 生效时机 |
|------|---------|
| `enabled` | 立即（停用 → 关阀终止当前任务；启用 → 立即可用） |
| `suppressError` | 立即 |
| `name` | 立即（纯展示） |
| `startupPulseLimit` / `startupEstimatedMl` / `stablePulsePerLiter` | 下次任务结束后再生效 |
| `startTimeoutSec` / `flowNoPulseTimeoutSec` | 下次任务开始后再生效 |

原则：影响当前运行中任务计算的参数，等任务结束后再生效。不影响正在执行中的参数，立即生效。

## 三、Zone 状态机（循环）

```
DISABLED ◄───────► IDLE ────────► STARTING ────────► RUNNING
   ↑                  ▲                                │
   │                  └────────────────────────────────┘
                      任务结束后自动回归
```

| 状态 | 含义 | 能否启动任务 | 阀门 | 计时 | 流量异常检测 |
|------|------|------------|------|------|-------------|
| DISABLED | 用户停用，不可用 | 否 | 强制关 | 无 | 不参与 |
| IDLE | 已启用，空闲就绪 | 是 | 关 | 无 | 不参与漏水 |
| STARTING | 任务已启动，等待水流建立 | 否（被占用） | 开（100% 占空比） | 无 | 不检测 |
| RUNNING | 任务正在执行 | 否（被占用） | 开（PWM 保持策略） | 倒计时 | 检测无脉冲超时 |

**为什么需要 STARTING 状态？**

阀门打开后，水流到达喷头需要时间（管路充水、空气排出）。此期间没有脉冲是正常的。STARTING 是缓冲态，期间不判断水流异常，只等待第一个脉冲。收到第一个脉冲 → 自动转入 RUNNING。超过 `startTimeoutSec` 仍未收到脉冲 → ERROR。

**终态处理**：COMPLETED、STOPPED、ERROR 不是 Zone 的可用状态，而是 `ZoneTaskRunner` 的执行终态。任务结束后，Zone 自动回归 IDLE（或 DISABLED，如果用户在此期间停用了该 Zone）。

## 四、ERROR 状态

当 `suppressError = false`（默认，生产模式）时，以下情况进入 ERROR：

| 触发条件 | 动作 |
|---------|------|
| STARTING 超时（启动后 `startTimeoutSec` 内无脉冲） | 关阀 → 写记录 → Zone 进入 ERROR |
| RUNNING 水流异常（运行中连续 `flowNoPulseTimeoutSec` 无脉冲） | 关阀 → 写记录 → Zone 进入 ERROR |
| 漏水告警（待机检测到异常脉冲） | 关闭全部阀门 → 该 Zone 进入 ERROR |

**ERROR 行为**：

- 不允许自动或手动启动任何任务
- 计划触发时跳过该 Zone，记录"Zone 异常"
- 用户必须执行"清除错误"操作后才能恢复 IDLE
- 即使抑制 ERROR（`suppressError = true`），记录仍然写入

## 五、ZoneTaskRunner（任务执行态，线性）

```
[无任务] ──start()──► 执行中 ──到达时长──► COMPLETED
                         │
                         ├──stop()────► STOPPED
                         │
                         ├──超时──────► FLOW_ERROR
                         │
                         └──漏水─────► LEAK_PROTECTED

                        （写入记录，结束，不循环）
```

```
ZoneTaskRunner
├── active: bool                      // 是否有正在执行的任务
├── task
│   ├── type                          // TASK_MANUAL / TASK_PLAN
│   ├── startSource                   // web_page / http_api / button / scheduler
│   ├── planSlot                      // 关联计划槽，手动任务为 0xFF
│   ├── targetSec                     // 目标时长
│   ├── startedMs                     // 启动时间戳
│   └── startedPulseCount             // 启动时的脉冲快照
├── runtime
│   ├── lastPulseCount                // 最新脉冲数
│   └── lastPulseMs                   // 最新脉冲时间戳
└── finished                          // 执行终态（只读信息）
    ├── result                        // COMPLETED / STOPPED / FLOW_ERROR / LEAK_PROTECTED / FACTORY_RESET
    ├── stopSource
    ├── stopScope
    └── endedMs
```

**行为**：

- `start(type, source, planSlot, targetSec, pulseCount, now) → bool`
  - 检查参数合法性、当前无活跃任务
  - 初始化 task，清空 finished
  - 打开阀门
- `tick(pulseCount, now, config) → 无返回值`
  - STARTING 状态：收到第一个脉冲 → 转入 RUNNING
  - STARTING 状态：超时 → 触发 FLOW_ERROR
  - RUNNING 状态：更新 lastPulseCount / lastPulseMs
  - RUNNING 状态：elapsed >= targetSec → 触发 COMPLETED
  - RUNNING 状态：无脉冲超时 → 触发 FLOW_ERROR
- `stop(source, scope, result, now)`
  - 关阀，填充 finished，标记 active = false
- `elapsedMs(now)` → 已运行时长
- `pulseDelta()` → 本次任务的脉冲差

## 六、Zone 对外接口

```
Zone
├── begin(config, now)
├── tick(pulseCount, now)                  // 每个 loop 调用一次
│
├── start(type, source, planSlot, targetSec, now) → bool
├── stop(source, scope, result, now)
├── clearError(now)                        // 清除 ERROR，回 IDLE
├── applyConfig(newConfig)                 // 更新配置快照
│
├── isEnabled() → bool
├── enable()
├── disable()                              // 停用 → 关阀，终止当前任务
│
├── getState() → ZoneState
├── getStateName() → const char*
├── isBusy() → bool                        // STARTING 或 RUNNING
├── isError() → bool                       // ERROR 状态
├── hasLeakAlert() → bool
│
├── getConfig() → const ZoneConfig&
├── getTaskInfo() → const ZoneTaskInfo&
├── getLastResult() → Result
│
├── checkIdleLeak(pulseCount, now, windowSec, threshold, observedPulses*) → bool
├── resetLeakWindow(now, pulseCount)
│
├── valvePin() → uint8_t
├── flowPin() → uint8_t
└── estimateMilliliters(pulses) → uint32_t
```

## 七、系统层（ZoneManager）

```
ZoneManager
├── zones[MaxZones]: Zone[]       ← N 份 Zone 实例
│
│  每个 loop:
│  ├── for zone in zones:
│  │       zone.tick(pulseCount, now)
│  │
│  ├── bool allIdle = all(zones[].isBusy() == false)
│  ├── if allIdle:
│  │       for zone in zones:
│  │           if zone.isEnabled() && zone.isIdle():
│  │               if zone.checkIdleLeak(now, &observedPulses):
│  │                   stopAll(SOURCE_LEAK_MONITOR, RESULT_LEAK_PROTECTED)
│  │                   zone.markLeakAlert()
│  │                   appendLeakDetected(zoneId, observedPulses, threshold, windowSec)
│  │                   break
│  └── else:
│       for zone in zones: zone.resetLeakWindow(now)
│
├── startZone(zoneId, task...) → 委托给对应 Zone
├── stopZone(zoneId, source...) → 委托给对应 Zone
├── stopAll(source, result) → 遍历 stop 所有 Zone
└── clearAllErrors() → 恢复出厂时清除所有 Zone 的 ERROR
```

**漏水监控逻辑**：
- `checkIdleLeak()` 属于 Zone 内部方法——只检查自己的脉冲窗口
- "是否允许监控"（所有 Zone 都空闲 + 阀门都关）属于 ZoneManager 的全局判断
- 不需要独立的 LeakMonitor 组件

**ZoneManager 是系统协调器**，职责：
1. 初始化 N 份 Zone 实例
2. 分发 tick 调用
3. 全局漏水监控前提判断
4. 提供 `stopAll` 等跨路操作

## 八、预设时长（非 Zone 配置）

手动启动时必须指定时长，不接受空值或零值。页面提供系统级预设快捷输入 + 手工调整：

```
DurationPresets（系统级，全局共享）
├── presets[] = [5min, 10min, 15min, 30min, 60min, 120min]
│
每个 Zone 手动启动时：
├── 展示所有预设时长供点选
└── 支持手工输入任意时长
```

未来可在设置页让用户自定义预设列表。首版可硬编码默认值。

## 九、数据流总结

```
用户/计划触发
    │
    ▼
ZoneManager.startZone(zoneId, task)
    │
    ▼
Zone.start(type, source, planSlot, targetSec, now)
    ├── 检查 enabled
    ├── 检查 state == IDLE
    ├── 初始化 runner.task
    └── ValveController::open()
    │
    ▼
每个 loop: Zone.tick(pulseCount, now)
    ├── 更新 runner.runtime
    ├── 判断超时/时长到/无脉冲
    └── 触发 stop 时：
        ├── ValveController::close()
        ├── 写入 RecordStore
        ├── 按需写入 Esp32BaseAppEventLog 业务事件
        ├── runner.finished 填充终态
        └── state → IDLE（或 ERROR）
```
