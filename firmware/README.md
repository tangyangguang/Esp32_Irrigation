# ESP32 灌溉控制器固件

PlatformIO 工程，Arduino Core 3 classic ESP32，固定使用 Esp32Base **IOT Profile**：本地 Web、灌溉业务核心与 MQTT 平台接入并存于同一固件，共用同一执行链路。

- 产品、硬件与业务边界：[`../docs/01-产品与硬件边界.md`](../docs/01-产品与硬件边界.md)、[`02-业务规则与安全.md`](../docs/02-业务规则与安全.md)、[`03-软件设计原则.md`](../docs/03-软件设计原则.md)。
- 烧录、Web OTA、串口监视的操作步骤：[`../docs/04-设备维护操作手册.md`](../docs/04-设备维护操作手册.md)。
- 公共能力（WiFi、Web、认证、OTA、MQTT、文件系统、Conditions、AppConfig、RTC、看门狗）来自 [`../../../foundation/Esp32Base`](../../../foundation/Esp32Base/README.md)；平台契约来自 `platform/iot-device`。

## 目录

```text
firmware/
├── platformio.ini                 # env: esp32_irrigation_arduino3（主目标）、native、esp32_record_test
├── partitions/esp32-4mb-ota.csv   # 4 MiB 布局：双 1728 KiB OTA + 512 KiB LittleFS
├── irrigation_iot_private.example.h  # 复制为 local_private/irrigation_iot_private.h 填真实凭据
├── src/main.cpp                   # setup/loop，仅转发给 IrrigationApp
├── src/irrigation/                # 一层业务模块（执行、调度、计量、配置、记录、Web、平台）
├── src/irrigation/generated/      # Web 资产生成物（由 web-src 生成，签入）
├── web-src/                       # CSS/JS 权威源（10 个 gzip 静态资产）
├── test/                          # native 行为测试 + 主机独立测试夹具（host_executor/host_hardware/host_storage）
└── scripts/                       # 构建钩子与独立本机检查
```

## 本机检查

命令均在 IoT 工作区根目录（`/Users/tyg/workspace/iot`）执行。

```sh
# 1. Web 资产：gzip 还原、注册、JS 语法
python3 devices/Esp32_Irrigation/firmware/scripts/test_web_assets.py

# 2. 纯业务逻辑 native 测试（Unity，82 项：配置/控制器/调度/记录/时间/异常流/平台记录投影）
python3 foundation/Esp32Base/scripts/pio_arduino.py 2 test -d devices/Esp32_Irrigation/firmware -e native

# 3. 独立主机检查（直接用宿主 c++ 编译，不依赖硬件）
python3 devices/Esp32_Irrigation/firmware/scripts/test_executor.py       # 执行任务并发/时序
python3 devices/Esp32_Irrigation/firmware/scripts/test_storage_views.py  # 真实 Base 存储引擎 + 内存 FS
python3 devices/Esp32_Irrigation/firmware/scripts/test_hardware.py       # BoardHardware + 伪 GPIO/LEDC

# 4. 主目标链接（Core 3 + 受控 TLS 工具链，含 Web 资产漂移与 OTA 容量门禁）
python3 foundation/Esp32Base/scripts/pio_arduino.py 3 --tls-toolchain run \
  -d devices/Esp32_Irrigation/firmware -e esp32_irrigation_arduino3
```

- `--tls-toolchain` 会把产物放到 `firmware/.pio/build/arduino3-tls/esp32_irrigation_arduino3/`；直接用 `pio run` 时产物在 `firmware/.pio/build/esp32_irrigation_arduino3/`。两者产物内容一致。
- `esp32_record_test` env 的设备测试会格式化设备文件系统，不随本机批次运行，仅在明确授权的设备维护场景使用。
- 平台契约改动时，另需在 `platform/iot-device` 运行其定义检查、类型检查与 SDK native 测试。

## 当前构建基线

主目标最近一次链接（Core 3 + TLS 工具链）：

| 项 | 值 |
| --- | --- |
| Flash（应用分区） | 1,553,536 B（87.8%，分区 1,769,472 B） |
| 静态 RAM（DRAM data+bss） | 109,620 B（33.5%，327,680 B）；IRAM 91,491 B |
| OTA 镜像 firmware.bin | 1,553,536 B |
| 对侧 OTA 槽余量 | 215,936 B（约 211 KiB，12.20%） |

运行堆/栈峰值未测量；OTA 余量偏紧，尺寸优化为待定项，不通过削弱 TLS/OTA/日志/记录预算来换体积。

## 调度边界修复（2026-09-17）

- 定时暂停到期**自动恢复的那一分钟不触发计划**，与手动恢复、启动分钟、只读“下一次”查询及 `03` 设计第 183 行统一；恢复后分钟仅重建检查点，下一分钟正常调度。
- 修复阈值内**跨午夜 RTC 回拨**时 `markProcessed` 可能用较早日期覆盖较新当天掩码的问题：回跨到已保存的前一天时合并进前一天掩码并保留较新当天掩码，避免重启后重复浇水；落入未跟踪的更早日期时安全跳过。
- 覆盖：`test_pause_modes_skip_or_resume_without_immediate_manual_run` 更新，新增 `test_cross_midnight_rollback_never_clobbers_newer_day_mask`；两例在旧实现上均失败、修复后通过。native 82 项全过，主目标 `pio run -e esp32_irrigation_arduino3` 链接成功；纯逻辑改动，不改变配置 schema、NVS blob 结构与平台契约。

## 平台适配对齐当前 SDK（2026-09-21）

9/19 晚 iot-device `1641a92` 重构设备端 MQTT 契约后，本项目未同步，主目标编译中断：

- `PlatformIdentity` 字段改为 environment/typeKey/protocolId/protocolMajor/modelKey/deviceId，按新顺序初始化（环境 test）；
- 诊断上报从 full/dynamic 双帧（`shouldSendFull/readFull/readDynamic/commitFull/commitDynamic`，已删除）改为 SDK 单帧 `observe -> read -> 发布 -> commit`，型号计数器 bootNo/mqttAtt/mqttErr/wdt 并入同一帧；采样仍在 `poll()` 现有采样点，不新增定时器。

`pio run -e esp32_irrigation_arduino3` 通过（Flash/IRAM 见上表）。本次只保证编译，未烧录；真机 TLS/MQTT、物理水路仍需另行授权验证。

## 修复首帧被只写参数卡死（2026-09-23）

用户烧录新固件后，小程序长期显示“等待灌溉状态同步”。定位根因：`PlatformPublishPolicy.connected()` 会把契约内全部 state/parameter 快照标脏，而 `parameter.zone`、`parameter.zone-baseline`、`parameter.system-field` 是只写维护命令（契约明确“单条修改”，设备从不主动上报），`buildSnapshot` 没有它们的构建器、返回 false；发送循环把这误判为队列失败并触发 5 秒退避阻塞，排在它们之后的 `state.zones`、`state.zone-maintenance`、`state.calibration`、`state.system-parameters` 首帧永远发不出去，服务端首帧集合不完整、`device_runtime_states` 一直 unconfirmed，小程序即显示等待同步。此前 09-20～09-21 的流量异常同源。

修复：发送循环识别这三个只写参数后直接清除 dirty 位跳过，不发帧、不占用队列、不触发退避；其余首帧随后正常发出。烧录重验后发现 `state.zones` 等四个维护 state 仍收不到：`publishStates` 每轮无条件把 overview/runtime/plan/automatic 重新标脏，而发送扫描每轮从 index 0 开始、每轮只发 4 帧，前四个位置永远被这四个高频帧占满，排在 index 11+ 的 `state.zones` 等被永久饿死。再修：发送扫描改为从轮转游标开始的公平轮询，任何 dirty 帧在有限轮次内必被发送。随后空闲流量核对暴露**真正的流量违例**：设备空闲时仍约每 5 秒重发 overview/runtime/automatic 三帧（实测 75 秒内 45 帧），原因是 `publishStates` 每轮无条件把这四个快照重新标脏，与“只在真实变化时发送”直接冲突——这就是 09-20～09-21 灌溉流量高峰的根因。三修：发布前对四类快照重新投影并计算 FNV 签名，只有值真正变化才标脏；overview 600s 锚点、diagnostics 3600s 仍由 `policy.poll` 周期处理。

## 子代理审查后批量收口（2026-09-23）

用两个 reviewer 子代理对 f6e1817～8db10b5 及上下游做全面审查，确认主链路（重连首帧、overview 600s、diagnostics 3600s、权限投影、无旧帧型残留）正确，另发现并一次修完：

- **浇水运行时 runtime 无 5 秒节流【中】**：连续量每轮变 → 每轮标脏，节奏是“突发连发 + outbox 满后 5 秒全局阻塞”。改为运行期间 runtime 最多 5000ms 重发；active 边沿、phase 切换、回到空闲等离散变化立即发；
- **只写参数空耗 seq 跳号【低】**：SDK 新增 `stateSkipped()`（只清脏不动 seq），替下只写参数路径上的 `stateQueued`，首帧 9 条消息 seq 连续；
- **重连首帧不保证 overview 第一【低】**：连接建立后重置 `g_scanCursor = 0`；
- **health 变化未触发 diagnostics【低】**：发布前保存 health 枚举，变化时同时标脏 diagnostics；
- protocol.md 引用了未声明的 `operation.refresh-diagnostics`，删除该表述。

编译通过，native 84 项全过。烧录验证待用户执行。

## 真机端到端验证通过（2026-09-23）

用户授权后由代理烧录（板子此前停在下载模式未进程序）。实测：

- boot 121 经 TLS 上线，**首帧顺序**：overview 为第 0 帧，7 个 state（overview/diagnostics/runtime/zones/zone-maintenance/calibration/system-parameters）+ automatic-watering 参数全部收齐，全部归属当前连接，投影 `complete`；只写参数走 `stateSkipped` 不再空耗 seq；
- **空闲 90 秒零非 retained 帧**（仅订阅时 retained availability 189B），流量违例确认消除；
- 命令链路：下发 `parameter.automatic-watering`（enabled，幂等无物理动作），收到 accepted 后即时 succeeded，command→receipt→progress 闭环正常。

**未覆盖（需另行安排）**：手动浇水 start-manual/stop/single-output 最短 1 分钟且驱动物理水路，本轮未下发；物理水路、长稳、正式环境、上传发布均未验证。本次烧录后 UART 串口无文本输出（USB 串口复用/日志初始化问题），但不影响平台链路，作为后续设备侧待查项。

## 修复浇水启动 prepareTask 鸡生蛋死锁（2026-09-23）
换实验板（未接水路、可任意实验）后多次 start-manual 仍在 accepted 后立即 failed `controller_unavailable`，证明与旧板损坏/供电无关。根因：`WateringRecordStore::prepareTask()` 用 `isWritable()` 做前置判断，而 `isWritable()` 要求 `taskReady_==true`；`taskReady_` 只有 prepareTask 成功后才置位 → 全新启动时 prepareTask 第一行永久返回 false → `startWatering` 返回 NotReady → 设备回 controller_unavailable。纯逻辑死锁，两板均中招。

修复：prepareTask 改为直接判断底层 `store_.isWritable()` 与 stream Ready，不再依赖 taskReady_。编译通过，native 84 项全过。烧录重验由用户执行。

## 修复 WDT 连锁根因与损坏 task marker 不自愈（2026-09-23）

prepareTask 修复后继续真机定位，确认最后一层连锁：

1. RTC 的 I²C 未设总线超时，总线被干扰拉死时 `Esp32BaseRtc::refresh()` 在主循环无限等待，5 秒后 task_wdt 复位（实测已累计 53 次、约 4~6 分钟一轮）；
2. 复位在 task marker / audit 写入瞬间发生，留下写一半的损坏 marker；
3. `recoverTask()` 读到魔数/CRC 不符的 marker 直接返回 false 且无自愈 → businessReady 永久 false → 浇水 controller_unavailable。

修复两处：`Wire.setTimeOut(50)` 从源头不再卡总线；`recoverTask()` 对损坏 marker 调用新增 `resetCorruptTask()` 写回空 inactive marker 并恢复 Ready（已损坏状态也能自愈），不再永久锁死。编译通过，native 84 项全过。

## 记录存储准入与 OTA 挂起语义修复（2026-09-23）

独立子代理评审确认根因后修复四类缺陷，均为状态/语义补全，非补丁：

1. **准入死锁（主阻塞）**：`WateringRecordStore` 单一布尔 `taskReady_` 被赋予两个冲突语义——恢复成功后恒为 true（`isWritable()` 要求 true），而 `prepareTask()` 又要求 false，`IrrigationApp::startWatering()` 同一调用内数学上不可能同时满足，故任何浇水都返回 `controller_unavailable`。拆分为 `ready_`（子系统就绪）+ `taskPrepared_`（在途任务防重入）；写 NVS 失败不清 prepared；删除零调用旧 `taskReady()`。
2. **历史时长读成 0**：readAdapter/readById 曾用槽元数据整体覆盖 fact 解码 timing，而槽元数据 duration 恒为 0。改为合并语义：fact 字节权威（epoch/duration），元数据仅贡献 boot/uptime。浇水/审计两个 Store 同修。
3. **有积压时重启永久初始化失败**：RecordStream 恢复时每轮 poll 只扫描一条记录，`begin()` 单次 poll 后立即 `recoverTask()`，active marker 需要 append 补 Incomplete fact，而 append 要求 Ready。改为 begin 中有界 drain 到 Ready 再恢复（本地扫描不发布，受 160KiB 存储上界约束）。
4. **OTA 写挂起毒化记录流**：临时写挂起原先映射为存储故障、RecordStream 永久进 Fault。分层对齐语义：Esp32Base 新增 `StoreError::WriteSuspended`（临时忙、不改 Ready）；SDK port `Esp32RecordStorage` 映射为可重试 `Busy`；灌溉审计 Store 失败进 pending 冻结缓存（已缓存不被覆盖），挂起解除后 `flushPending` 重试。
5. **契约错误码**：`validateRequest` 替代 `isValidRequest`，引用禁用 zone 精确返回 `zone_unavailable`（结构非法仍为 `invalid_request`）。

验证：native **84 项**全过；三个主机脚本（executor/storage/hardware）、Web 资产检查全过；主目标 TLS 工具链链接成功（基线见上表）。主机测试脚本补齐了自仓库整合后缺失的 SDK/ArduinoJson include 与链接源。

## 真机验证（2026-09-23）

烧录新固件后经 MQTT 实测：TLS 上线、首帧完整（从 `state.zone-maintenance` 快照正确取得 revision=3）；`parameter.zone` 启用一路 accepted→succeeded；**`operation.start-manual` 首次真正进入 running（此前永久卡 controller_unavailable），核心阻塞确认解除**。板子未接水路，约 19 秒后按 `flowStartTimeoutSec=20` 的硬安全保护正确终态 failed（`flowEstablished=false`，泵无流量证据不得空转）——这是预期保护行为，非缺陷。

**未覆盖**：浇水完整成功路径需要真实或模拟水路（流量计脉冲输入 GPIO17，RISING 中断），后续现场或信号发生器喂脉冲再验；物理水路、长稳、正式环境未验证。

## 会话交接：MQTT 启动浇水仍未解决（2026-09-23）

> **已于同日解决**：该 `controller_unavailable` 根因即上文「记录存储准入与 OTA 挂起语义修复」中的准入死锁，修复后 start-manual 已能进入 running。本章保留原始排查过程作为历史证据。

**已验证通过**：TLS 上线、发现候选、小程序绑定；首帧完整（overview 为第0帧，7个 state 全收齐、投影 complete）；空闲 90~120 秒零非retained帧（符合专题01）；参数命令 `parameter.automatic-watering` 经 MQTT accepted→succeeded（命令通道正常）；capability 集合与 definition 一致（符合专题02）。

**未解决缺陷（下一会话起点）**：`operation.start-manual`（MQTT）设备 accepted 后立即 failed `controller_unavailable`，多次复现；但同时本地 `GET http://<设备IP>/irrigation/api/status`（basic auth admin/admin）显示 `ready:true`、全部 storageFault=false、且 **`zones:[]` 为空**。矛盾点说明 execute 路径独有检查失败。**首要排查假设**：配置中 zone 未启用/未配置（status.zones 为空），命令引用 zoneId=1 被判无效；下一会话先读 `parameter.plan`/zone 配置确认，勿再进入烧录循环。

**实验环境事实**：当前实验板 device_id=`esp32-irr-8caab58ed1cc`（IP 漂移 .127/.155）；固定串口 `/dev/cu.usbserial-110`；该板必须手动 BOOT+EN 进下载模式，**烧录始终由用户执行**；MQTT 可用 `/tmp/mqttvenv`（已装 paho+pyserial，临时目录可能失效需重建），命令下发凭据用 `.env.local` 的 MQTT_DEVICE 账号，start-manual TTL 上限严格 30000ms。


## 存储与平台契约

- 配置 schema 5 不变；试验阶段零历史兼容、零数据迁移，旧测试数据不读取、不转换、不自动清理。
- 浇水 codec v4：270 B 业务 payload（54 B 头含 36 B commandId + 6×36 B 水路，含建议基准脉冲率）；审计 20 B 业务 payload。v3 旧记录不读取。
- 两个持久 Store（浇水 v9 / 审计 v6，均 `PreserveUnreleased`）物理槽为 24 B IR/v1 头 + 业务字节，预算 160/48 KiB；**单一持久 Store 同时服务本地历史与平台可靠补发**，内嵌 SDK `RecordStream`，不设第二份 outbox，断网补发与 record-ack 经同一存储水位管理。
- 每次浇水启动尝试只有一条浇水记录：完成/停止/失败为实际出水过程；调度点到了但启动被拒（手动或另一计划占用、基准学习中、上一任务未落盘、设备/存储未就绪、计划配置无效）记录为 `failed` + 启动失败原因，zones 为空或全部未启动、时长水量为 0，不补浇；暂停期间到点和断电停机不形成记录。审计流不再保存计划运行事实（原 `operation.automatic-run.completed` 删除，平台 recordKey 10 永久保留不重用）。
- 浇水任务标记为紧凑 NVS marker，重启据此重建 Incomplete/RebootInterrupted 事实。
- 平台适配 `IrrigationPlatform*` 按 `platform/iot-device` 的 irrigation-controller 契约实现：设备 ID 由 STA MAC 生成 `esp32-irr-<12hex>`，8 类状态投影、5 类命令（plans/automatic-watering/start-manual/stop/single-output）全部复用 `IrrigationApp` 唯一执行入口；来源 `WateringSource` 区分 LocalWeb/AutomaticPlan/WechatMiniprogram。
- MQTT host/CA 缺失时安全降级为纯本地运行，不阻塞 Web 与业务。
- 模型头 `IrrigationSdkModel.generated.h` 由 `scripts/generate_sdk_model.py` 在构建目录从契约生成，**不签入**；Web 资产由 `scripts/generate_web_assets.py` 从 `web-src/` 生成到 `src/irrigation/generated/`，签入并在构建前做漂移检查。

## MQTT 凭据

真实凭据只放在 Git 忽略的 `firmware/local_private/irrigation_iot_private.h`（编译参数 `-I local_private`）。模板：

```sh
cp firmware/irrigation_iot_private.example.h firmware/local_private/irrigation_iot_private.h
```

`src/irrigation/IrrigationIotSecrets.h` 提供空安全默认值并优先采用私有头的覆盖；不得提交真实 host、账号、密码或 CA。

## HTTP Web OTA

网络升级只使用 Esp32Base HTTP Web OTA（入口 `/esp32base/ota`），具体操作步骤、串口整片烧录和串口监视见 [设备维护操作手册](../docs/04-设备维护操作手册.md)。在 Git 忽略的 `platformio.local.ini` 中配置设备地址与 Web Auth 后执行 `-t webota`；普通构建和测试不会触发 OTA。不得配置 espota、ArduinoOTA 或 3232 端口。
