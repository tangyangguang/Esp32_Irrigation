# Esp32_Irrigation 当前建设状态

本文件记录当前项目边界和后续路线。旧版阶段计划已经完成或被新架构替代；旧文档仅作为需求来源，不作为实现兼容依据。

## 1. 项目定位

本项目是固定 4 路 ESP32 智能灌溉控制器应用，默认启用第 1、2 路，第 3、4 路默认通过 Web/API/计划控制。应用层只实现灌溉业务：阀门控制、流量计量、计划调度、本地按键、业务 Web/API、记录与告警策略。

基础能力由 `/Users/tyg/dir/claude_dir/Esp32Base` 提供，默认按 `ESP32BASE_PROFILE_FULL` 使用 Core、Runtime、Bus、Watchdog、Sleep、FS、FileLog、Health、WiFi、DNS、NTP、mDNS、Web、OTA、App Config 和 App Events。

## 2. 当前真实路由

- `/`：由基础库跳转到业务首页。
- `/index`：业务首页，包含 4 路状态、手动启动、停止和告警入口。
- `/irrigation/plans`：计划配置列表。
- `/irrigation/plan?planId=<id>`：单条计划编辑。
- `/irrigation/zones`：水路管理。
- `/irrigation/calibration`：流量校准。
- `/irrigation/records`：浇水记录。
- `/irrigation/events`：灌溉业务事件。
- `/api/v1/status`：轻量状态 API。
- `/api/v1/config`、`/api/v1/zone/*`、`/api/v1/plan/*`、`/api/v1/schedule/*`、`/api/v1/calibration/*`、`/api/v1/records`、`/api/v1/events`：业务 API。

当前没有 `/irrigation/manual`、`/irrigation/settings`、`/irrigation/data`、`/irrigation/plan-config` 等旧路由。

## 3. 当前模块结构

```text
src/
├─ app/IrrigationApp.*
├─ domain/
│  ├─ BusinessEventLog.*
│  ├─ FlowCalibration.*
│  ├─ FlowMeter.*
│  ├─ MaintenanceService.*
│  ├─ PlanExecutionTracker.*
│  ├─ SafetyManager.*
│  ├─ ValveController.*
│  ├─ Zone.*
│  ├─ ZoneManager.*
│  ├─ ZoneScheduler.*
│  ├─ ZoneTaskRunner.*
│  └─ ZoneTypes.h
├─ io/ButtonInput.*
├─ storage/
│  ├─ PlanStore.*
│  ├─ RecordStore.*
│  ├─ ScheduleSkipStore.*
│  ├─ SystemConfigStore.*
│  ├─ ZoneConfigStore.*
│  └─ ZoneErrorStore.*
└─ web/IrrigationWeb.*
```

## 4. 已落地能力

- 固定 4 路水路配置，默认启用第 1、2 路。
- 多路可并行手动浇水，同一路互斥。
- 本地 `STOP ALL` 始终可用；锁定状态下 R1/R2 只允许停止正在运行的对应水路，不允许从空闲启动；`START/OK` 仅作用于第 1、2 路。
- 计划模型为每路最多 6 条，总计 24 条；计划定义持久化，计划执行观测按 `planId + ymd + minuteOfDay` 持久化。
- 单次跳过 API 按 `planId + ymd` 管理；近期计划 Web 视图尚未实现，不作为当前验收承诺。
- 业务 POST 统一通过 `Esp32BaseWeb::checkPostAllowed()` 做认证、POST 方法和同源校验。
- 恢复出厂 pending 期间拒绝启动、配置保存、清告警、计划保存、跳过、校准写入等非停止类写操作；停止单路和停止全部仍允许。
- 浇水记录使用业务 `RecordStore`；业务事件使用 `Esp32BaseAppEventLog`。
- `Esp32BaseAppConfig` 已接入系统级业务参数；水路和计划仍由业务页面管理。

## 5. 后续优先级

1. 记录文件元数据掉电恢复：为 `RecordStore` 增加可恢复的记录序号/CRC 或提交标记，避免文件已写入但 NVS meta 未前进时记录不可见。
2. 持久化结构版本保护：对 `ZoneConfigStore`、`PlanStore` 等版本不匹配场景增加明确事件、导出/备份或显式清空策略，避免静默丢弃可识别用户数据。
3. `ScheduleSkipStore` 写放大优化和过期清理：减少每次跳过/取消跳过的 NVS 全量写入，并明确固定容量覆盖或自动清理语义。
4. 近期计划 Web 视图：如确认需要，再实现今天/明天/后天展开和单次跳过表单，并同步原型与验证清单。
5. Flash 余量治理：区分 debug/release 日志等级，检查 Web 字符串和内联 CSS/JS 体积。
6. 实机验证：阀门有效电平、PWM 保持策略、驱动温升、GPIO0 启动模式风险、流量计上拉与干扰、真实水路启动延迟、OTA、功耗和 72 小时稳定性。

## 6. 验证命令

```bash
node scripts/check-web-structure.mjs
pio run
```

涉及硬件、功耗、流量、RTC、LCD、OTA 的结论必须明确区分“已实机验证”和“未实机验证”。
