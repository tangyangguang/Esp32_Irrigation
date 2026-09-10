# ESP32 灌溉控制器固件

本目录包含当前唯一有效的灌溉控制器固件。产品和业务规则以 `../docs/当前方案/` 为准，硬件事实以 `../pcb_irrigation/` 下 2026-07-11 的 BOM 和网表为准，公共设备能力以`../../../foundation/Esp32Base` 当前文档和代码为准。

## 当前升级计划（2026-09-10）

本节是唯一当前待办，替代根目录存储过程记录；类型级 G0～G6 仍在 [灌溉类型账本](../../../platform/iot-device-lab/device-types/irrigation-controller/README.md)。

已核实：`29ac715` 已完成紧凑双 Store、移除独立 outbox、Conditions 与审计历史分离、统一 Storage 登记。watering v6 = 193 B / 384 KiB，audit v1 = 24 B / 128 KiB。Base 的 FS 协调、FileLog 统一入口和受保护 Store 机制已有实现，不重做底座。文档旧有“未接入”“App Events”“616 B / 200 条”不能作为当前任务依据。

用户已确认本地来源及未知时间契约：如实标记本地 Web / 微信 / 自动调度；未知绝对时间为空、质量 unknown，照常保存与补发，日期统计排除并显示未覆盖量。无历史迁移或兼容分支。

| 状态 | 交付结果 / 责任 | 尚缺工作与边界 |
| --- | --- | --- |
| 待办 | B：4 MiB 容量收口 | 已提交的资源优化减少 4460 B；SDK 会话/双流候选实测 Flash 1614587 B、RAM 100508 B，较已提交基线增加 13340/496 B，超 1.5 MiB OTA 槽 41723 B，8% 余量亦未满足。实际 4 MiB 目标不变，仍需优化及容量收口。 |
| 进行中 | C：SDK 平台职责接入 | 设备阶段 `2133e72` 已提交推送，SDK 命令/ACK 信封、UUID、UTC 解析与受控型号生成已通过目标构建、33 个共享向量及 14 项相关 Native 检查；SDK 双流会话 a076543 已提交推送；设备会话、状态/证据发布和双流接入阶段已验证，用户已确认维持字节预算、允许历史条数变化。接下来替换设备逐命令 NVS journal；保留重连幂等、启动截止点和先关输出后终态。 |
| 待办 | E：定义 + 设备 + 平台投影 | 修复本地来源错标与未知时间队头阻塞；补 state.diagnostics；服务端/小程序复用既有 unknownTimeCount 展示，核对统计类型和记录呈现。JSON 依赖已收敛到 SDK 的 6.21.6，并加入完整模型有界容量，协议接入尚未完成。 |
| 待办 | F：业务安全与集成收口 | 定向检查两流追加失败后的事实/故障处理、RTC 暂停/恢复、stop 与启动互斥、配置失败和维护出口；按最终代码运行目标编译及成功/明显故障路径测试，提交推送、记录产物和资源。 |
| 待办 | 串口试验与现场边界 | 本机 `/dev/cu.usbserial-57460296581` 已核实为 4 MiB ESP32，烧录和短时试验授权有效；当前障碍为固件容量门禁，已取消更换 8 MiB 硬件的错误要求。长稳、满容量、反复断电及真实水路验收由用户后续试运行完成。 |
| 正式发布前 | 安全与交付边界 | 项目凭据/ACL 隔离、首次 Web 默认认证治理、完整 TLS 握手和受控工具链分发位置需在正式范围确认；不改真实账号、权限或共享环境。 |

已交付基础：设备存储收尾 `3913c8d`、Web/JSON `1435fcf`、MQTT 4 KiB 收发容量修复 `575a71a`；Base 维护回调 `4f05677`、资源优化 `5469569`、静态资源与发送超时修复 `8f3176a`；SDK 完整帧诊断 `7e1a865`、型号生成规则去重 `5a1240b`。上述均已提交推送。存储读取未初始化标志及无效事件缓存已修复，旧存储过程记录已删除，未完成事项全部保留在上表。

当前硬件与构建目标均为 **4 MiB ESP32**。8 MiB 分区文件已移除，保留双 1.5 MiB OTA、896 KiB LittleFS 及现有 Store/FileLog 预算；未经同意的容量缩减方案不实施。Core 3 LEDC 安全适配、维护门禁与 SDK 输入阶段 `2133e72`，以及 SDK ESP32 Topic 生命周期修复 `c4e12e0` 已提交推送。

本轮 Base 优化 `0579717`、设备去重及 4 MiB 目标纠正 `2037223` 已提交推送。资源复查结果：Base 公共脚本和配置页 CSS 通过现有 gzip 发送能力缓存，原文逐字无损；设备名称规则收敛到同一有界实现，删除与 SDK 重复的字段集合及 UTF-8 校验，保留数组/整数范围及业务交叉约束。内联编译选项对比无收益，已撤回，不更改最终编译策略。目标 ELF Flash **1601247 B**，相比 **1605707 B** 减少 **4460 B**；静态 RAM **100012 B** 不变。全套协议、日志和 TLS 仍参与链接。

验证：Base Web 22/22、gzip 协商/二进制边界与源文件无损检查、架构/安全/发布卫生通过；设备配置与协议 22 项通过，另有 33 个平台共享向量通过（初次未设置 `IOT_DEVICE_LAB_DIR` 未运行向量，补齐实际路径后通过）。命令：`python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 2 test -e native -f test_config -f test_iot_protocol`；共享向量使用 `IOT_DEVICE_LAB_DIR=/Users/tyg/workspace/iot/platform/iot-device-lab` 运行 `-e native_iot_vectors`。

实际 4 MiB 目标命令：`python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 3 --tls-toolchain run -e esp32_irrigation_arduino3`。编译和链接完成，容量检查 **失败**：1601247 B 超出 1572864 B OTA 槽 **28383 B**，不宣称构建通过或可试运行；8% 余量门禁保留。旧 8 MiB 比较产物不可烧录。本轮没有烧录、复位、Broker 业务命令或泵阀操作，完整平台接入未完成；后续任务见上表。

### SDK 会话与双流接入阶段（2026-09-10）

SDK `a076543` 已推送到 `codex/record-stream`：会话按世代分发两流 ACK，维护检查点逐流尝试、故障隔离；ESP32 端口提供事件观察回调。协议、ESP32 端口及受影响的 ESP8266 单流调用定向检查通过。

设备源码已删除旧流 NVS 元数据和手写连接/上下线/状态证据信封，复用 SDK Session、ModelPublisher、RecordStream。追加失败保留浇水完成事实及原始时间；自动运行审计失败时只重试审计，避免重复浇水记录，未落盘完成结果阻止新浇水及 OTA/格式化。尚未替换命令 journal，其他审计修改入口的失败处理仍在 F 范围。

当前存储为 watering v7、audit v2，不读取或迁移旧格式。24 B SDK 头与 4 B 持续时间叠加现有业务编码，payload 分别为 221 B、52 B；Base 实际算法测得槽 245/76 B，对比原 217/48 B，浇水 1809→1602 条、审计 2722→1719 条。用户明确允许历史条数变化，保留完整功能和字段，维持 384/128 KiB 字节预算；分区与 4 MiB 硬件目标不变。

快速验证：`python3 scripts/test_storage_views.py` 使用实际 Base Store/主机 FS，通过空历史、分页、损坏业务数据、未知时间不回填、恢复期间重试的时间冻结、ACK 流隔离、检查点恢复、审计写故障隔离；`pio_arduino.py 2 test -e native -f test_records` 5 项通过，`-f test_iot_protocol` 8 项通过（删除旧 ACK wrapper 后修复残留测试注册再通过）。设备端存储测试源码已更新，未运行设备测试或烧录。以上不是业务链路验收。

本阶段最终目标命令同上，编译/链接成功、容量检查失败：Flash **1614587 B**，RAM **100508 B**，超 OTA 槽 **41723 B**；日志 `/tmp/irrigation-session-final-size.log`。没有生成可交付烧录固件。

## 1. 当前产品范围

当前固件实现：

- 6 路 12V DC 电磁阀互斥控制；
- 可选外部水泵继电器控制；
- 流量采集、无流量保护、高低流量告警和关阀后异常水流监测；
- 手动浇水、每日计划、自动调度暂停与恢复；
- 流量计校准、水路基准学习和单次出水；
- 配置安全保存、浇水记录、业务事件和在线检查点；
- DS3231 RTC 离线时间、NTP 校时和本地 Web；
- 认证 Web、System 维护、文件系统日志和 HTTP Web OTA；
- `LED1 / GPIO13` 低电平点亮的非阻塞状态指示；
- 当前 `irrigation-controller/v1` MQTTS 外围适配：固定七个 channel Topic、QoS 1、retain/LWT/正常 shutdown、五类命令、receipt/progress、八项完整状态投影和可靠业务记录流。

MQTT 适配只调用现有配置、调度和浇水入口，不直接操作 GPIO，也不建立第二套网络生命周期。未提供本机私密 MQTT 配置时，固件保持本地能力可用且不尝试连接 Broker；掉线、重连或平台不可用不得阻断本地 Web、RTC 自动调度、保护和安全停机。

产品永久不实现 LCD2004、本地菜单和四按钮业务交互。

## 2. Esp32Base 接入契约

正式固件使用：

```ini
-D ESP32BASE_PROFILE=ESP32BASE_PROFILE_IOT
-D ESP32BASE_MQTT_MAX_PAYLOAD_BYTES=4096
-D ESP32BASE_MQTT_MAX_INCOMING_PAYLOAD_BYTES=4096
-D ESP32BASE_MQTT_MAX_OUTBOX_BYTES=4352
-D ESP32BASE_ENABLE_RECORD_STORE=1
-D ESP32BASE_ENABLE_CONDITIONS=1
-D ESP32BASE_ENABLE_APP_CONFIG=1
-D ESP32BASE_ENABLE_RTC=1
-D ESP32BASE_RTC_DRIVER=ESP32BASE_RTC_DRIVER_DS3231
```

设备记录测试使用 `ESP32BASE_PROFILE_OFFLINE`，只显式开启测试需要的 Record Store 和 Conditions。

当前基础库只支持 `MINIMAL / OFFLINE / LOCAL / IOT` 四个 Profile。项目不得重新引入旧 Profile、`ESP32BASE_ENABLE_WEB_OTA`、`ESP32BASE_ENABLE_ARDUINO_OTA`、ArduinoOTA/espota、3232 监听端口或已删除的认证读取 API。其它项目接入或适配当前基础库时，先阅读：

- `../../../foundation/Esp32Base/docs/13_integration_and_upgrade.md`
- `../../../foundation/Esp32Base/docs/02_profiles.md`
- `../../../foundation/Esp32Base/docs/04_web.md`
- `../../../foundation/Esp32Base/docs/05_ota.md`

项目业务代码只实现灌溉领域能力，不复制 Esp32Base 的 WiFi、Web、认证、OTA、文件系统、日志、时间、RTC、配置、健康和看门狗实现。

MQTT 私密配置只放在 Git 忽略的 `local_private/irrigation_iot_private.h`：

```sh
mkdir -p local_private
cp IrrigationIotSecrets.example.h local_private/irrigation_iot_private.h
```

填写 Broker 主机、8883 端口、项目用户名/密码和签发 Broker 证书的 CA PEM。不得提交该文件、设备凭据或正式环境地址。Client ID 和协议 `deviceId` 均由运行时芯片 MAC 生成的永久 `esp32-irrigation-{12位小写十六进制}`，不允许私密文件覆盖身份。Arduino Core 2.0.16 的 TLS 栈执行 CA 与 hostname 校验，但该版本未启用证书 notBefore/notAfter 检查；构建中的 `ESP32BASE_MQTT_ALLOW_UNCHECKED_CERTIFICATE_DATES=1` 是明确、可审计的当前 Core 例外，不代表关闭 CA 或 hostname 校验，升级生产 Core 后必须重新评估并移除。

## 3. 启动与运行边界

启动顺序固定为：

1. 第一项初始化 `BoardHardware`，关闭泵信号、全部阀 PWM 和阀驱动总使能。
2. 在输出安全关闭后初始化状态灯。
3. 初始化 I2C，并将 DS3231 总线交给 `Esp32BaseRtc`。
4. 设置 Web 默认认证，注册业务 Web、App Config 和文件系统格式化回调。
5. 调用 `Esp32Base::begin()`；失败时保持全部输出关闭并进入故障指示。
6. 基础库启动成功后启用 WiFi modem sleep。
7. 加载业务配置、命令幂等 journal、调度状态、watering/audit 两个业务 Store 及 SDK 独立记录流恢复。
8. 只有全部必需状态有效时才进入业务 ready。

正常循环先推进业务状态机和 IoT 外围适配，再调用 `Esp32Base::handle()`。Web/MQTT handler 不执行校准、等待出水或其它长时间流程；MQTT 消息由 Esp32Base 有界邮箱串行分发。中断只累计流量脉冲，不做日志、存储、业务判断或硬件切换。MQTT 状态和记录序列化共用 `IrrigationIot` 长期对象中的单一 4097 B 缓冲，不在 `loopTask` 栈上创建 4 KiB 临时数组；这是当前 4096 B payload 上限下的硬性栈安全边界。

IOT 配置在 `Esp32Base::begin()` 前注册基础库网络停止回调。统一 restart/deep sleep 或 Web OTA 暂停 MQTT 前，如果当前连接和可信 UTC 均可用，回调以 QoS 1 retained enqueue 同一 `connectionId` 的 `online:false, reason:"shutdown"`，并请求基础库允许的 1000 ms 有界网络宽限；restart/deep sleep 在 MQTT DISCONNECT 前后各应用一次，实机验证可形成 `online → shutdown → 新周期 online` 且不触发该正常周期 LWT。enqueue 或宽限不等于 PUBACK/必达，离线、时间不可信、异常掉电或 Broker 不可达仍以本周期预置 LWT 兜底，不能因 shutdown 发布失败阻止安全停机或维护流程。

WiFi modem sleep 保持 STA、Web、NTP、OTA、调度和保护可用；本项目不进入 Deep-sleep，不降低 CPU 频率，也不改变泵阀控制时序。OTA 期间由 Esp32Base 临时关闭 power save，结束后恢复。

## 4. 配置与持久数据

当前数据定义：

- 灌溉 JSON 配置：schema v4，权威路径 `/app/irrigation/config.json`；
- 浇水事实：`watering` Store v7，固定 221 B payload、384 KiB 逻辑预算；它同时是本地历史与平台补发的唯一事实源；
- 必要审计事实：`irrigation-audit` Store v2，固定 52 B payload、128 KiB 逻辑预算；保存自动计划运行/跳过、自动总控、计划修改、校准和水路基准保存；
- 两个 Store 各自拥有独立 `recordStreamId + recordSequence`、累计业务 ACK 和 Base Store 释放检查点，在同一 MQTT Client 与 event topic 上公平交错发送；
- MQTT 命令 journal：NVS 中固定 16 条，保存不可变签名、receipt 和可信终态；重启不为中断任务推断终态；
- RTC 不可用、可信时间不可用、RTC 倒退和关阀异常水流只由 `Esp32BaseConditions` 在 NVS 保存当前活动位图，不形成通用历史；
- 系统文件日志：4 × 32 KiB，默认 WARN；
- 标量系统参数：Esp32Base App Config / NVS；
- 自动浇水总控、调度防重复标记、在线检查点、记录流身份和低频累计 ACK 检查点：由 Base Store 控制记录维护；调度等小状态仍为项目 NVS。

IoT 记录 QoS 1 PUBACK 只解除本次 MQTT 在途发布，不删除补发事实；只有匹配对应 `recordStreamId` 且不越过该 Store 日志头的累计 `record-ack` 才推进该流 RAM 水位，绝不联动另一流。各流 ACK 每累计 32 条或推进后满 24 小时才保存 Base Store 释放检查点；Store 容量满且仍有未确认记录时停止该 Store 追加，全部保留记录已确认后才允许分段轮转。

配置文件存在但当前副本和备份都无效时，固件保持安全停机，不用默认值覆盖。重新编译、串口烧录或 HTTP OTA 不得清理或覆盖已有有效 NVS/LittleFS 数据。只有业务结构明确不兼容时才拒绝启动并提示重新配置；不得自行猜测或迁移旧结构。

LittleFS 挂载失败不会自动格式化。格式化只允许用户在确认没有需保留数据后从 System 页明确执行；它不会清 WiFi、Web Auth 或其它 NVS。格式化完成后，业务回调负责重建当前配置和存储。

当前应用在源码中提供 `admin/admin` 作为首次启动 Web 默认认证，已保存的 `eb_web` 认证优先。该共享默认值只适用于受控本地初始化，部署后必须立即在 Web Auth 页面修改；在进入批量交付前应改为每台设备唯一的受控初始化凭据方案，不能把共享默认值当作量产安全边界。

## 5. 构建与自动测试

在本目录执行。首次使用先准备Esp32Base仓库隔离的双Core环境；本项目当前固定通过Core 2.x目录构建，不依赖用户默认`~/.platformio`：

```sh
python3 ../../../foundation/Esp32Base/scripts/ensure_arduino_platformio.py
python3 scripts/generate_web_assets.py --check
python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 2 test -e native
python3 scripts/test_storage_views.py
IOT_DEVICE_LAB_DIR=/Users/tyg/workspace/iot/platform/iot-device-lab \
  python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 2 test -e native_iot_vectors
python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 2 test -e esp32_record_test --without-uploading --without-testing
python3 scripts/build_iot_release_fixture.py

# 有专用可清空实验板时，执行设备端测试；端口按实际环境替换
python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 2 test -e esp32_record_test \
  -f test_record_store_device \
  --upload-port /dev/cu.usbserial-XXXXXXXX \
  --test-port /dev/cu.usbserial-XXXXXXXX
```

含义：

- Web 资源检查：确认 `web-src/` 与生成的压缩固件数组一致；
- Native：覆盖配置、控制器、记录编解码、调度、异常水流、校准、时间、IoT 严格协议和命令 journal；
- 共享向量：直接消费当前 `iot-device-lab` 合法/非法命令向量，防止终端与平台契约漂移；
- 设备记录测试编译：确认 Esp32Base OFFLINE、两个受管 Record Store、Conditions、独立流 ACK 路由和项目设备测试可链接；
- 正式构建：夹具脚本拒绝覆盖已有私密头，临时生成带 2 KiB CA 正文的非敏感配置，清理旧目标后完整链接 MQTT/TLS 路径，使用 classic ESP32 4MB balanced 双 OTA 分区并检查 slot 余量。

当前快速验证（2026-09-10）：

- Native 99/99 通过，涵盖控制器、流量保护、调度、校准、记录编码及既有协议。
- `python3 scripts/test_storage_views.py` 通过：使用 Base 实际 Store 与其主机 FS 夹具，覆盖空历史、正常记录、分页及非法业务 payload；启用自动变量污染以暴露未初始化读取。
- 历史 Core 2 `esp32_irrigation` 增量构建通过，实际 Base 依赖经 PlatformIO link 描述确认是 `foundation/Esp32Base`；完整本机 MQTT 配置参与链接，无上传。RAM 103140 B，Flash 1419769 B；binary 1426352 B，最小 OTA slot 1572864 B，余量 146512 B / 9.31%，超过 8% 门禁。
- 未运行设备端存储测试或任何硬件动作；以上不证明 Core 3、SDK 接入或真实 MQTT 链路已通过。

IOT 固件使用 1.5 MiB 双 OTA + 896 KiB LittleFS 分区，发布门禁至少 8% OTA slot 余量。完整 MQTT/TLS 配置参与链接后才报告资源，不能用空配置被 LTO 裁剪后的结果。只重跑本次改动影响的定向检查，不重复已通过且未受影响的测试。

## 6. Web 静态资源

大段业务 HTML、CSS 和 JavaScript 在 `web-src/` 维护，通过以下命令确定性生成压缩固件数组：

```sh
python3 scripts/generate_web_assets.py
```

正式构建前置脚本会执行 `--check`，源文件与生成物不一致时拒绝构建。CSS/JS 预生成 gzip，通过 Esp32Base 静态资源接口发送，由浏览器解压；10 个资源均要求认证，带内容摘要版本和 private 缓存。表单 HTML 原样分块输出，不再常驻解压缓冲或调用 ROM miniz；不写入 LittleFS，不绕过基础 Web 生命周期。

页面样式修改通过构建后可以在获得当次授权时烧录；烧录后由用户在实机浏览器完成最终视觉验收，自动化工具不替代视觉确认。

## 7. HTTP Web OTA

项目只使用 Esp32Base HTTP Web OTA。复制本地模板：

```sh
cp platformio.example.ini platformio.local.ini
```

在 Git 忽略的 `platformio.local.ini` 中填写设备地址和当前 Web Auth：

```ini
[env:esp32_irrigation]
custom_esp32base_webota_host = irrigation-controller.local
custom_esp32base_webota_user = <current-web-auth-user>
custom_esp32base_webota_password = <current-web-auth-password>
```

操作者确认设备空闲和维护窗口后显式执行：

```sh
python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 2 run -e esp32_irrigation -t webota
```

普通构建和测试不会触发 OTA。不得提交真实设备地址、账号或密码。不得配置 espota、ArduinoOTA 或 3232 端口。

## 8. 实机验证边界

目标编译、主机测试、模拟 MQTT、真实服务、真实设备与水路实验分别记录。开发交付不等待长稳、满容量或反复断电；这些由用户在半生产环境逐步验证。

本轮已获本机 ESP32 试验设备直接烧录和测试授权，操作前核对具体串口、芯片和容量；真实水路不在此次试验范围。过去核心板的无动作 MQTT/OTA 结果以及用户曾验证的实际水路，只说明对应旧版本和环境可用。当前升级后的设备连接、完整 TLS、互斥控制、流量保护、RTC 和现场安全尚未验证。
