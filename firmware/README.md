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
| Flash（应用分区） | 1,554,352 B（87.8%，分区 1,769,472 B） |
| 静态 RAM（DRAM data+bss） | 109,620 B（33.5%，327,680 B）；IRAM 91,491 B |
| OTA 镜像 firmware.bin | 1,554,352 B |
| 对侧 OTA 槽余量 | 215,120 B（约 210 KiB，12.16%） |
| loopTask 栈 | 16 KB（`ARDUINO_LOOP_STACK_SIZE`，运行时从堆分配，不计入上方静态 RAM；空闲堆约因此少 8 KB） |

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


## 修复首页 loopTask 栈溢出（2026-10-22）

现象：局域网打开本地 Web 首页 `/irrigation` 极慢，每次请求设备都 `Stack canary watchpoint triggered (loopTask)` panic 复位，页面在输出约 3 KB（“每日浇水”区块入口）后挂死；记录/计划/系统页和全部 API 正常。

根因：8 KB loopTask 栈在首页渲染路径上被两类深栈叠加击穿——

1. `WateringStatus` 单对象实测 1136 B（含 10 个流量学习窗口和 6 路明细）。`IrrigationWeb::overview/activeTask` 已持有一份，`renderDay → IrrigationApp::wateringDay() → wateringStatus()` 又按值返回第二份；
2. 同一 loopTask 上文件日志 2 秒 flush 时 littlefs `fsync/rename/dir_traverse` 与 newlib `fopen/vsnprintf` 调用极深（panic backtrace 同时包含这两组帧）。首页渲染慢拉长了与 flush 相撞的窗口。

修复：

- `wateringDay()` 改为接收调用方已有的 `const WateringStatus&`，不再在页面渲染时复制第二份 1.1 KB 状态；
- 主目标 `build_flags` 增加 `-D ARDUINO_LOOP_STACK_SIZE=16384`（8 KB→16 KB）。注意必须定义 `ARDUINO_LOOP_STACK_SIZE`（`#ifndef` 保护）；定义 `CONFIG_ARDUINO_LOOP_STACK_SIZE` 会被框架 `sdkconfig.h` 强制重定义回 8192，不生效。

验证：native 84 项、executor/storage/hardware 三个主机脚本、Web 资产检查全过；主目标链接成功。真机（`esp32-irr-8caab58ed1cc`，TLS+MQTT 在线、6 条浇水记录）烧录后：首页完整 9475 B、0.3–0.85 s 返回，串行 15 次、4 并发、6 并发全部 200 且 uptime 连续增长不再复位；状态页 Loop stack low-water 从 ~0.9 KB 回升到约 8.95 KB。已知限制：人为 12 并发极端冲击会因单线程同步 Web 请求排队触发一次 task_wdt，正常浏览器（单域名约 6 连接以内）达不到，本轮不重构 Web。

## 修复维护类 state 快照远程写入后不重推（2026-10-22）

现象（真机证据）：`parameter.system-field` 命令 accepted→succeeded、审计事件 `configuration.system-field-changed` 正常落盘上报，但 `state.system-parameters` 不重推；错过 MQTT 连接首帧后，订阅方只能看到旧值。`parameter.zone` / `parameter.zone-baseline` 对 `state.zones` / `state.zone-maintenance` 同理。违反 irrigation-controller `protocol.md`：「parameter 当前值只在成功应用后发布」「客户端以 succeeded 之后的 state/parameter 快照为当前值证据」。

根因：`publishStates()` 只对 overview/runtime/`parameter.plan`/`parameter.automatic-watering` 做每轮 FNV 签名变化检测；四个维护快照（zones/zone-maintenance/calibration/system-parameters）只在 `PlatformPublishPolicy::connected()` 时标脏发布一次，命令成功路径与本地参数页保存（`handleParameterConfigSaved`）都没有变化通知。

修复：把四个纯静态配置投影纳入与 `parameter.plan` 相同的每轮 `markIfChanged` 签名检测，任何写入者（三条远程维护命令、本地水路页、本地参数页）落配置后下一循环自动重推；`meter.pulsesPerLiterX100` 同时进 calibration 与 system-parameters，两帧签名都会变；zone 禁用联动清零计划时长由既有的 plan 检测覆盖。每轮新增 4 次 ≤约 500 B 序列化（静态 6144 B doc 复用，新增 RAM 约 16 B），开销与既有 plan 投影同量级。

验证：native 84 项全过；主目标 `--tls-toolchain` 链接成功，OTA 余量 215104 B（12.16%）与基线一致；独立 reviewer 审核通过。真机回证（三命令 + 本地页保存后四帧按新值重推）待烧录，新 elf_sha256 前缀 `77e60ca2`。

附：本次构建前 tls penv（`foundation/Esp32Base/.piohome/arduino3-tls/penv`）pip 丢失（疑似 Homebrew Python 3.14 小版本升级使 venv 失效），PIO 报 `Failed to install Python dependencies into penv`；`penv/bin/python -m ensurepip` 恢复后正常；中途 scons-4.8.1.zip 因 GitHub 瞬时超时失败，网络恢复后重试即过（包本已缓存，repair 才触发重装）。

## 修复多水路记录 JSON 解码容量不足导致记录流永久堵死（2026-10-22）

真机证据（浇水实验步骤 C 期间）：60 s 手动浇水本地记录完整（603 脉冲/2.412 L、succeeded），但 broker 上完全没有完成事件；服务端浇水流水位自 9/23 起停在 seq5。重启重放显示：设备发布 seq1 后服务端跳位 ACK 到 5，之后设备不再发布 seq6/7。

根因：`ModelPublisher::publishRecord` 把存储事实解码进共享文档 `g_recordDoc`，原为 `StaticJsonDocument<512>`。用真实解码器 native 实测（ArduinoJson 6.21.6，与设备同版本）：单水路投影约 590–608 B、双水路约 871–927 B、6 水路最坏情况（completed + 基准/报警字段 + 最大合法数值 + commandId）约 2353 B，64 位主机 JSON 池需 4096（3072 溢出），32 位 ESP32 slot 更小（单水路能进 512 与此吻合）。9/24 的 seq6 是**双水路**自动计划失败记录，解码确定性溢出 → 发布每 5 s 静默重试永不成功；RecordStream 严格按序发布，seq6 成为队头毒记录，把其后的 seq7（本次单水路成功记录）一并永久堵死（审计流因记录小未受影响）。

修复：记录解码文档容量提为共享常量 `IrrigationPlatformRecords::kRecordDocBytes = 4096`（host 实测最坏情况有余量；整包约 ≤2.75 KB < MQTT payload 4096 与 g_output 4097）；新增 native 回归 `test_six_zone_fact_fits_record_document`（6 水路最坏 payload 必须解码进生产容量、不溢出、过 schema、body 留信封余量）。RAM BSS +3584 B（109644→113228/327680），Flash 与 OTA 余量不变（215104 B/12.16%）。烧录后 seq6/seq7 应在重放中自动排空，无需数据迁移。

遗留（单独立项，本轮不动 SDK）：RecordStream 对永久性发布错误（容量/schema/超大包）仍是无告警无限重试并堵死整条保序流；本次从容量根因消除已知触发路径，但未来投影/契约演进仍可能静默复发，需要错误可观测性与毒记录策略。另：本地操作记录页把 `configuration.system-field-changed` 审计显示为「未识别的历史事项」，为本地展示映射缺口，待后续修。

验证：native **85 项**全过；主目标 `--tls-toolchain` 构建+OTA 门禁通过；独立 reviewer 审核通过。真机回证（seq6/7 broker 可见、水位推进、free heap/无 JsonCapacity 错误）待烧录，新 elf_sha256 前缀见构建产物。

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

## 真机实验交接（2026-10-22）

### 浇水主流程真机实验完成（2026-10-23 追加）

步骤 C–G 全部在真机完成并通过，期间修复 3 个固件缺陷（均已提交推送）：

1. `8faf754`（10-22）维护类 state 快照远程写入后不重推；
2. `f9ac996`（10-22）多水路记录解码文档 512 B 溢出堵死记录流（详见上文）；
3. `358058e`（10-23）**`state.zone-maintenance` 的 `baselineFlowMlPerMinute` 恒为 null**：`projectZoneMaintenance` 从未按 `baselinePulseRateX10000` 与当前流量计系数换算，小程序拿不到基准流量。修复后真机 zone1 基线 100800（10.08 P/s，K=250 P/L）→ `2419 mL/min`，快照立即重推、落盘 revision 6，服务端 `device_runtime_states.evidence_state=complete`。这是修复①扩展维护快照覆盖时暴露的遗漏。

真机实验数据（设备 `esp32-irr-8caab58ed1cc`，EMQX 测试环境 + Mac `./server start`）：

- **C 手动成功**：60 s 10 Hz → succeeded，604 脉冲 / 2.416 L / 恒定 2400 mL/min（250 P/L 吻合），干节点门控正确。
- **D 中途停止**：运行 23 s（出水 21 s）后 stop → seq10 `watering.stopped/user_stopped`，222 脉冲 / 0.888 L，即时生效。
- **E 无流保护**：出水建立后断开门控停脉冲 → 10 s（`noFlowTimeoutSeconds`）精确 `watering.failed/no_flow_timeout`（seq13，19 s / 98 脉冲 / 0.392 L 如实入账）。
- **F 基准与报警**：完整 60 s 10 Hz 记录自动给出稳定建议基线 `suggestedBaselinePulseRateX10000=100800`（连续 5×5 s 窗口波动达标），经 `parameter.zone-baseline`（revision=5）确认落盘并产出审计 seq8 `zone.baseline-saved`；2 Hz（480 mL/min≈基准 20%）持续 20 s → `lowFlowDetected/lowFlowActive=true`；22 Hz（5304 mL/min≈218%）持续 20 s → `highFlowDetected/highFlowActive=true`；action=alert 不停机，任务正常 completed，记录含 average/baseline 流量与检测位。
- **G 记录与审计**：浇水流 16 条、审计流 9 条，服务端 ACK 水位一致（16 / 8→9），无积压；三类浇水（completed/stopped/failed）与 `wechat_miniprogram`/`device_schedule` 来源齐全；今日（北京 10-23）用量 9 次 / 593 s / 19328 mL，逐记录求和核对一致；多水路记录（seq8 双水路 4824 mL）聚合正常；审计 recordKey 覆盖 automatic.resumed/paused、configuration.zone-changed/system-field-changed、zone.baseline-saved。实验后自动总控已恢复 enabled。

### 步骤 H 断网补发真机通过（2026-10-23 下午）

单次成功路径，未做 soak。断网手段：EMQX Serverless 控制台 **Access Control → Blacklist 按 Client ID `esp32-irr-8caab58ed1cc` 临时拉黑**（设备 TLS 可建连但 MQTT 被快速明确拒绝），WiFi/局域网全程正常；物理拔 WAN 与 H3C 路由器黑名单均验证为错误手段（见下）。

- **断网离线**：拉黑后 observer 约 100 s 零帧，设备 boot 820 连续运行 9 min+ **无复位、无 errno=11**，两个 Store 无 fault；本地 `POST /irrigation`（action=start_manual，zone1 60 s，来源 local_web）正常执行。
- **断网浇水**：61 s 完成，zone1 60 s、**603 脉冲 / 2412 mL**，11 个 5 s 采样恒定约 2400 mL/min（flow-history 2398–2448），`watering.completed` 落本地 watering.v9（historyNextId 17→18），断网期间无任何串口异常。
- **恢复补发**：删除黑名单后设备重连（新 connectionId `72c81b6f…`），**仅补发 seq17 一条** `watering.completed`，保持原 `observedAt=07:52:00Z`、只替换 connectionId；服务端即时回 **record-ack through=17**。DB 权威核对：浇水流 **ack=17 / rows=17 / max_seq=17**（唯一约束保证不重不丢），审计流仍 **9/9/max_seq=9**（隔离正确，不被浇水流影响）。
- **堆水位**：在线空闲 free heap ≈ 40–46 KB；**离线（无 TLS/MQTT 缓冲）free heap ≈ 90 KB**（比在线宽裕约一倍），max alloc 恒 35.99 KB，loop stack low-water 10.8 KB，diag 帧 boot820 `heapLow=40600` B；离线浇水窗口无堆/FD 压力。之前 `Unable to allocate FD` 的堆压力来自 TLS/网络路径而非记录补发本身——离线补发窗口反证。
- **意外但重要的定性证据（半挂网络 → WDT）**：实验前半段物理拔 WAN 与 H3C 黑名单制造的是「WiFi 关联在、WAN 半挂」，设备出现 `NetworkClient write(): fail on fd 50 errno:11`（TLS 写 EAGAIN）**阻塞 loopTask 满 5 s 连续触发 task_wdt 复位**（14:32 起 boot 807–815、15:26 起 811–822 多轮，累计 watchdog reset count=110），复位后重放历史 seq 被服务端幂等去重（ack 不后退）。而 EMQX 拉黑的「干净不可达」零复位。结论：已知「长运行 TLS 写卡死」缺陷的触发条件是**路由半挂/连接半死导致的写阻塞**，不是快速连接失败；责任在框架 NetworkClient/Base MQTT 传输，本项目不改，建议 Esp32Base 立项（写超时/非阻塞发送）。

排障澄清（非缺陷，避免重复排查）：

- 中途怀疑「config.json 一个月写不进 / FD 耗尽锁死配置」，加临时分步日志后**证伪**：全新启动 7 步写链（encode→ensureDir→writeTmp→readback→removeBak→renameCur→renameTmp）全部成功，当时 free heap≈49 KB、max alloc≈36.8 KB。先前手工录入失败是测试值 2.419 L/min 经「流量→脉冲率→流量」整数往返不严格相等、在存储层前被校验挡回；一次 MQTT `persistence_error` 是长运行 TLS 反复重连堆压力瞬间 audit stream 暂不可写的连带门控，重启不复现。诊断日志与临时 native 测试均已还原，未提交。
- `parameter.automatic-watering`/`parameter.zone-baseline` 等维护命令参数必须带**契约全部 required 字段**（如 paused 也要带 `resumeAtEpoch:null`、基线要带 `revision`）；缺字段被 schema 静默拒绝、无回执（易误判为链路故障）。revision 每次配置落盘自增，过期 revision 返回 `revision_conflict`。

仍需关注（后续立项，非本轮阻塞）：

- **半挂网络下 TLS 写阻塞触发 loopTask WDT 复位（步骤 H 已定性，待 Esp32Base 修复）**：触发条件是**路由半挂/连接半死**（物理断 WAN、路由器反复踢关联）：`NetworkClient write(): fail on fd 50 errno=11`（TLS 写 EAGAIN）在 loopTask 上阻塞满 5 s 即 task_wdt 复位，半挂期间反复发生（实验中 boot 807→822，watchdog 累计 count=110）；而 EMQX 拉黑的「快速明确不可达」零复位、离线浇水 free heap≈90 KB。电脑同 broker 长连正常，问题在设备端框架 NetworkClient/Base MQTT 写路径（缺写超时/非阻塞发送）。9/23 高负载抓到的 `esp_littlefs: Unable to allocate FD` 同属网络路径堆压力（在线 free heap 仅约 40 KB vs 离线约 90 KB），记录补发本身不造成 FD 压力。责任在 Esp32Base/iot-device，本项目只读未改，建议立项。
- 命令 schema 校验失败静默无回执，建议平台/SDK 侧补拒绝回执（可观测性）。
- 本地事件页把 `configuration.system-field-changed` 显示为「未识别的历史事项」的映射缺口待补。
- 实验临时工具与 MQTT venv 在 `/tmp`（`irr_exp/`、`irr_h/`：mqtt_h 固定 client+持久会话监听、堆轮询、串口抓 log、DB 核对脚本），重启即失；**设备现存 19 浇水记录、平台审计水位 18**（步骤 I 后；seq19 浇水为 17:18 自动计划 61 s completed）。

### 晚间进展（浇水实验开始前，待烧录继续）

- **新固件待烧录**：本轮共两个修复（均已提交推送、native 85 项与主目标构建全过、**尚未烧录**）：①维护类 state 快照远程写入后不重推（提交 `8faf754`，elf 前缀 `77e60ca2`）；②多水路记录 JSON 解码容量不足（512 B）使双水路记录永久发布失败并按保序堵死整条浇水记录流（见上文「修复多水路记录 JSON 解码容量不足…」，含 seq6/7 积压，烧录后自动排空）。烧 app 保留 NVS/配置/记录，下列已启用配置不丢；烧录后 elf 前缀以最新构建为准。
- **实验前置已完成（旧固件 5e8539db5 上操作）**：Mac 测试服务原本未运行，设备因无 record-ack 每 5 s 重传挂起记录；已启动 Postgres（docker `iot-home-postgres-dev`）+ `./server start`（127.0.0.1:17832，MQTT healthy），重传立即平息。`pump.enabled` 经 MQTT 命令 accepted→succeeded（审计 `configuration.system-field-changed` seq 4、fieldIndex 5，NVS 回读 true），startDelayMs 本就为 0。zone1「试验一路」、zone2「区域 2」实际早已启用（revision=4，计划「花园」07:00 各浇 1 分钟，自动总控开启）；与早前交接「zones 为空」不同，以当前为准。
- **历史异常观察（步骤 H 已部分定性）**：9/23 服务端缺席、记录重传风暴期间，一条 system-field 命令的审计落盘撞上 `esp_littlefs: Unable to allocate FD`（FD 耗尽），审计流进 StorageFault 永久态 + 1 条审计 pending，EN 复位后恢复、数据无损。步骤 H 测得在线 free heap 仅约 40 KB、离线约 90 KB，支持「FD 压力来自 TLS/网络路径」的方向；但「服务端缺席重传风暴」与本次「干净离线无重发」场景不完全等价（干净离线时 TLS 不建连、无持续重发），重传风暴与落盘并发的精确复现留待长稳专项。
- MQTT 实验工具事实：state/progress/event 全部**非 retained**（仅 availability retained），观察命令结果必须实时订阅；`/tmp/mqttvenv` 本次重建（paho 2.1.0），临时脚本在 `/tmp/irr_exp/`（`irr_mqtt.py listen|cmd|snap`，traffic.jsonl 全量留痕），重启系统会丢。

### 本轮已完成

- **首页 loopTask 栈溢出已修复并真机验证**（提交 `2b78237`，已推送）：现象、根因、验证见上文「修复首页 loopTask 栈溢出（2026-10-22）」。
- **RTC DS3231 硬件正常；开机误报 1 分钟「不可用」已修复并真机验证**：DS3231 接线、电池、I2C 均正常，系统页 `RTC ok, Last sync uptime 0` 证明首次读取在开机 0 秒即成功、NTP 同步前就已授时。旧固件每次开机 `rtcUnavailable=true` 约 60 s 的真正根因**不是读 DS3231 失败**，而是 Base 条件状态机的去抖语义：条件从「未知」首次观测为健康也要走完一次完整的**恢复确认窗口**，而 `IrrigationEvents` 的 RTC 条件激活/恢复确认都配成 60000 ms、又恰好每 60 s 才 observe 一次，于是开机必误报约 1 分钟。修复（`IrrigationEvents.cpp`）：恢复确认 `60000→0`（DS3231 读取确定性，健康立即采信），故障激活确认仍保留 60 s（偶发 I2C 毛刺不误报）。实测新固件开机约 20 s 内 `rtcUnavailable` 即连续 `false`。期间曾按「首读 NACK」误判加过 `Wire.begin()` 后地址探测重试，验证无效（`Last sync uptime 0` 否定该假设），已回滚，未提交。9/23 的 `rtc_writeback_failed i2c_error` 是更早的 I2C 总线被拉死（尚无超时）连锁 WDT 的历史问题，当天已用 `Wire.setTimeOut(50)` 修复；NTP 回写仅在 RTC 与 NTP 时差超阈值时发生，时间既准则不回写也不报错（没有 `rtc_writeback_ok` 属正常）。断网纯 RTC 授时仍留待后续断网实验做端到端确认。
- 当前固件 version 0.9.0、`ARDUINO_LOOP_STACK_SIZE=16384`，含上述栈修复与 RTC 条件修复，真机 elf_sha256 前缀 `5e8539db5`。
- **启动日志 `events_fault=yes` 不是缺陷**：时间未可信（NTP 同步前）SDK RecordStream 未到 `Ready`，审计流按设计暂不可写；NTP 同步后实时恢复 `eventStorageFault=false`，不锁存。

### 设备与环境事实

- 实验板 device_id `esp32-irr-8caab58ed1cc`（MAC `8c:aa:b5:8e:d1:cc`）；IP 由 DHCP 分配，本次为 `192.168.2.127`（会漂移，失联先按 MAC 或路由器查）；Web auth `admin/admin`。
- 固定实验串口 `/dev/cu.usbserial-110`（CH340），115200。**板子自动复位/下载电路不工作**：进下载模式必须用户手动「按住 BOOT → 按一下 EN → 松 EN → 保持约 1 s 再松 BOOT」；烧录一律由代理在用户确认已进下载模式后执行，烧完 RTS 硬复位无效，需用户手动按 EN 启动。
- 代理读串口必须用 pyserial 且打开时 `dtr=False; rts=False`（裸 `Serial()` 默认置位会导致零字节；`pio device monitor` 需真实 TTY，代理后台无 TTY 会报 `termios` 错；用户本人在终端跑 `pio device monitor -b 115200` 正常，PlatformIO 入口 shebang 失效时用 venv python：`foundation/Esp32Base/.piohome/arduino3-tls/penv/bin/python -m platformio device monitor -p /dev/cu.usbserial-110 -b 115200`）。
- 代理烧录 app（保留 WiFi/配置/记录）在 `/Users/tyg/workspace/iot` 下执行（产物随 `-t` 变化，README 检查用 `--tls-toolchain`，故用 `.pio/build/arduino3-tls/...`）：

```sh
foundation/Esp32Base/.piohome/arduino3-tls/penv/bin/python -m esptool \
  --chip esp32 --port /dev/cu.usbserial-110 --baud 460800 \
  write-flash 0x10000 \
  devices/Esp32_Irrigation/firmware/.pio/build/arduino3-tls/esp32_irrigation_arduino3/firmware.bin
```

- 当前设备数据（2026-10-23 傍晚步骤 I 后）：**19 条浇水记录（watering.v9，nextRecordId=20）；平台审计流 ACK/DB 水位 18（irrigation-audit.v6）**，最后一次 rev10 计划恢复的 `configuration.plans-changed` 审计在一次 MQTT 重连前产生、未进平台 ACK（parameter 快照已是 rev10 权威）；自吸泵启用（startDelayMs=0）、zone1「试验一路」/zone2「区域 2」启用，zone1 基准已保存（100800 / 2419 mL/min）；配置 **revision=10**；计划「花园」**07:00、zone1+zone2 各 1 分钟（已恢复实验前配置）**，**自动总控 enabled**；MQTT 连 EMQX 测试环境。实验遗留：07:00 自动计划依赖信号发生器 10 Hz 门控，不接发生器会走无流超时，必要时提前暂停。

### 信号发生器门控实验（C–I 全部完成）

用户已确认：不接真实水管和 12V 阀，无物理安全顾虑；全部用 **10 Hz**（每秒 10 脉冲，方波 3.3 V、共地）模拟廉价流量计，默认 K=250 P/L → 约 **2.4 L/min**，60 s ≈ 600 脉冲 ≈ 2.4 L，便于核对计量。

接线：

- 脉冲输出 → **CN2（PUL_SIG / GPIO17）**，与板子共地，幅值不得超过 3.3 V；
- 泵干节点 **CN5 的 RELAY1A/RELAY1B（AQY210 PhotoMOS，GPIO18 低电平导通，47k 上拉默认关）** 串入信号发生器门控/供电，做到「泵信号输出→干节点闭合→出脉冲，停泵→停脉冲」；
- 12V 阀、真实水路不接。

进度（C–G 已于 2026-10-23 完成，数据见上文「浇水主流程真机实验完成」；H 断网补发当日下午完成，见上文「步骤 H 断网补发真机通过」）：

1. ✅ 启用泵 / zone1、state 回证；
2. ✅ C 手动 60 s 成功（succeeded，604 脉冲 / 2.416 L）；
3. ✅ D 约 20 s 停止（user_stopped，按实际脉冲）；
4. ✅ E 运行中断脉冲 → 10 s no_flow_timeout；
5. ✅ F 基准学习/确认 + 低/高流量报警；
6. ✅ G 记录回路、watering-usage、三类浇水与审计 recordKey；
7. ✅ **H 断网补发（2026-10-23 完成）**：EMQX Client ID 黑名单制造干净离线 → 断网完成 1 次 60 s 浇水（603 脉冲/2412 mL）→ 删黑名单恢复，设备仅补发 seq17、保持原 observedAt、只换 connectionId，DB 核对浇水流 17/17/17、审计流仍 9/9，不重不丢；离线 free heap≈90 KB（在线≈41 KB），无 FD 耗尽。数据与「半挂网络触发 TLS 写卡死 WDT」的定性见上文「步骤 H 断网补发真机通过」。
8. ✅ **自动总控边界（2026-10-23 傍晚完成）**：见下节「步骤 I 自动总控边界真机通过」。

> 验证边界（用户明确要求）：以上剩余项以**关键成功路径 + 明显故障 + 必要安全边界**为准，必要的做完即止，不做 soak/长稳/反复断电/全量矩阵；长稳由用户在半生产环境自行验证。

### 步骤 I 自动总控边界真机通过（2026-10-23 傍晚）

最小集全部在真机（`esp32-irr-8caab58ed1cc`，10 Hz 门控就绪）完成，并修复 1 个真机暴露的固件缺陷（提交 `20473c0`，已推送，烧录后回证）：

- **三态下发回证**：`enabled`（幂等）、`paused-indefinitely`、`paused-until`（带未来整分钟恢复点）均 accepted→succeeded，`parameter.automatic-watering` 快照与审计一一对应：seq10 `automatic.paused{paused-indefinitely}`、seq11 `automatic.resumed{enabled}`、seq12 `automatic.paused{paused-until,resumeAtEpoch}`；定时暂停在其后第一个整分钟边界（16:36:01）自动恢复为 enabled。
- **计划编辑 1 次**：`parameter.plan` upsert（revision 6→7）把「花园」改为 16:58/17:00、仅 zone1 1 分钟，回证快照与审计 seq15 `configuration.plans-changed{revision:7,planIds:[1]}`。
- **自动计划真实触发（成功路径）**：17:18:00 槽位自动启动（`activity=automatic`），干节点闭合→10 Hz 出脉冲，运行 61 s，**seq19 `watering.completed sourceKey=device_schedule planId=1`，09:18:00Z→09:19:01Z、result=completed、ACK through=19**。
- **定时恢复那一分钟不触发**：17:19:45 下发 `paused-until resumeAtEpoch=17:20:00`（审计 seq17 `automatic.paused`）；17:20:01 仅自动恢复（审计 seq18 `automatic.resumed{mode:"expired"}`、快照回 enabled），**17:20 槽位只标记不启动、不形成任何浇水记录**（浇水流仍停在 19），nextAutomatic 跳到次日 07:00，恢复后调度状态正常。

期间两个被「非缺陷」正确跳过/打断的点，作为边界证据保留：16:58 槽位因该计划**当天 07:00 已在「按（计划,启动槽位）持久防重复掩码」中标记**而跳过（掩码按槽位不按分钟值，改分钟值不重置）；17:00 新槽位精确触发后被用户手动按 EN 打断，按 seq18（浇水流）`reboot_interrupted` 如实记账（0 时长）。

**修复的缺陷**：`decodeAutomaticResumed` 对到期恢复也写 `resumeAtEpoch:null`，使 `automatic.resumed` 的到期投影为 `{mode:"expired",resumeAtEpoch:null}`，违反契约 oneOf（`expired` 分支 `additionalProperties:false` 只允许 mode）→ `ModelPublisher` schema 拒绝、`publishRecord` 返回 false → SDK `RecordStream` 每 5 s 静默重试，该 seq 成为**保序流队头毒记录，堵死其后全部审计**（真机 seq13 起的暂停/恢复事件与 record-ack 全部不可见，浇水流不受影响）。修复为 `expired` 只写 `mode`；新增 native 回归（reason=4 投影过 schema 且不含 resumeAtEpoch，旧实现失败、修复后通过）。烧录新固件重连后，毒记录 seq13 `{mode:"expired"}` 立即发布、ACK 推进，其后 seq14/15 自动排空。native 85 项全过，主目标构建 Flash 1,554,043 B / RAM 113,228 B / OTA 余量 215,024 B（12.15%）。遗留（SDK 侧，非本轮）：`RecordStream` 对永久性发布错误（schema/容量）仍无告警无限重试并毒死整条保序流，待立项加错误可观测性与毒记录策略。

实验后已恢复设备配置：`parameter.plan` 回到「花园」**07:00、zone1+zone2 各 1 分钟**（revision 10），自动总控 **enabled**；该次配置恢复审计在一次 MQTT 重连前发出、未落平台审计流（平台审计水位停在 18），但 `parameter.plan` 快照已回证 rev10，按契约 parameter 快照即当前值权威。

### 其他待办

- `iot-device` 本地有 1 个未推送提交 `edcc4ce`（平台准入 `model_mismatch` 收紧，SDK 代码未动，灌溉固件不受影响），下次跨仓工作先跑门禁再推送。
- OTA 对侧槽余量约 215 KB（12.16%），发布前评估固件尺寸优化，不靠削 TLS/OTA/日志/记录预算换体积。
- 已知限制不修：人为 12 路以上并发猛刷单线程同步 Web 会触发一次 task_wdt；正常浏览器单域名 ≤6 连接，实测 6 并发稳定。
- 真机 TLS/MQTT：参数命令、start-manual 成功路径、中途停止、无流失败、基准/高低报警、记录与审计上报、**断网补发（步骤 H，seq 连续/ack 水位/不重不丢）均已真机验证**。正式环境（EMQX 正式账号/ACL、固件 environment `test`→`prod`、NUC 服务、小程序发版）须用户明确发版授权后另立任务。
