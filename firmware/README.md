# ESP32 灌溉控制器固件

本目录包含当前唯一有效的灌溉控制器固件。产品和业务规则以 `../docs/当前方案/` 为准，硬件事实以 `../pcb_irrigation/` 下 2026-07-11 的 BOM 和网表为准，公共设备能力以`../../../foundation/Esp32Base` 当前文档和代码为准。

## 当前升级计划（2026-09-10）

本节是唯一当前待办。当前试运行编码与平台接入已完成，无进行中的编码事项；已清理被替代的存储过程记录和旧容量结论。类型级验收见 [灌溉类型账本](../../../platform/iot-device-lab/device-types/irrigation-controller/README.md)。

当前硬件是 **classic ESP32、4 MiB**，双 1728 KiB OTA、512 KiB LittleFS、64 KiB coredump。用户允许调整历史条数：watering/audit 预算为 160/48 KiB，容量为 **666/639 条**；完整记录字段、业务功能、128 KiB 日志和 128 KiB FS 安全空间保留，另留 48 KiB 元数据/配置余量。当前 bin **1615120 B**，每槽剩余 **154352 B（8.72%）**；静态 RAM **100396 B**。

| 状态 | 交付结果 / 责任 | 尚缺工作与边界 |
| --- | --- | --- |
| 用户试运行 | 现场边界 | 本机串口试验已授权；核心板没有 DS3231、流量计或泵阀，RTC 故障提示符合现场事实。长稳、满容量、反复断电和真实水路由用户验证。 |
| 正式发布前 | 安全与交付边界 | 单设备凭据/ACL、首次 Web 默认认证治理和受控工具链分发需单独确认；未改真实账号、权限或 NUC。 |

最新修复已提交推送：设备 e961ad0（栈峰值与 active 读取）、server 21b9eb9（无回执过期收尾），Base 10505fe；lab 4a2f1e4 与 wx 5fe103c 更新实际证据。此前已提交推送：设备 SDK 会话/双流 5180992、来源与未知时间 d727e24、Base 诊断 3d9483b、RAM 命令账本 4b98650、存储故障恢复 877a606、4 MiB 分区 514e149；Base WiFi 尝试计数 c206326，SDK a076543；lab b963ad0 / 7c63141，server d2046a8，wx ce16e19。设备只保留灌溉策略，Base 负责通用资源与维护，SDK 负责平台会话、消息和可靠流。

本机目标为 `/dev/cu.usbserial-57460296581`，ESP32-D0WD-V3 rev 3.1，4 MiB，MAC `08:d1:f9:3b:2c:f4`。硬件 deviceId `esp32-irrigation-f42c3bf9d108`，本机业务实例 UUID `ad182049-f086-443b-a46a-9bf0cfdc6862`。首次更新时逐段 Hash 校验通过；保留原始日志及 NVS，旧文件系统仅有可舍弃实验记录，没有业务配置文件。私密备份和串口原日志仅存放于忽略目录 `local_private/`。

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
- 当前 `irrigation-controller/v1` MQTTS 外围适配：固定七个 channel Topic、QoS 1、retain/LWT/正常 shutdown、五类命令、receipt/progress、八项业务状态及独立 60 秒诊断投影和可靠业务记录流。

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

填写 Broker 主机、8883 端口、项目用户名/密码和签发 Broker 证书的 CA PEM。不得提交该文件、设备凭据或正式环境地址。Client ID 和协议 `deviceId` 均由运行时芯片 MAC 生成的永久 `esp32-irrigation-{12位小写十六进制}`，不允许私密文件覆盖身份。当前固定 Arduino Core 3.3.8 与 Base 受控完整 TLS 工具链；CA、hostname 和证书 notBefore/notAfter 均校验，不启用跳过日期检查的例外。

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

IOT 在 Base 启动前注册网络停止回调，通过 SDK 发起同一 connectionId 的正常 shutdown。维护等待最多 3 秒，必须匹配最终 PUBACK 并收到断开事件才开始 OTA；失败恢复网络许可并拒绝更新。安全重启仍先关输出，退出失败不伪装成功。MQTT PUBACK 不是业务记录落库确认。

WiFi modem sleep 保持 STA、Web、NTP、OTA、调度和保护可用；本项目不进入 Deep-sleep，不降低 CPU 频率，也不改变泵阀控制时序。OTA 期间由 Esp32Base 临时关闭 power save，结束后恢复。

## 4. 配置与持久数据

当前数据定义：

- 灌溉 JSON 配置：schema v4，权威路径 `/app/irrigation/config.json`；
- 浇水事实：`watering` Store v7，固定 221 B payload、160 KiB 逻辑预算；它同时是本地历史与平台补发的唯一事实源；
- 必要审计事实：`irrigation-audit` Store v2，固定 52 B payload、48 KiB 逻辑预算；保存自动计划运行/跳过、自动总控、计划修改、校准和水路基准保存；
- 两个 Store 各自拥有独立 `recordStreamId + recordSequence`、累计业务 ACK 和 Base Store 释放检查点，在同一 MQTT Client 与 event topic 上公平交错发送；
- MQTT 命令账本：SDK 管理固定 16 条 RAM 记录，二进制 UUID、不可变语义签名、receipt 和可信终态；未交付证据或未终结记录不因过期覆盖。重启按 SDK 启动截止时间拒绝旧命令，不恢复任务、不推断终态；
- RTC 不可用、可信时间不可用、RTC 倒退和关阀异常水流只由 `Esp32BaseConditions` 在 NVS 保存当前活动位图，不形成通用历史；
- 系统文件日志：4 × 32 KiB，默认 WARN；
- 标量系统参数：Esp32Base App Config / NVS；
- 自动浇水总控、调度防重复标记、在线检查点、记录流身份和低频累计 ACK 检查点：由 Base Store 控制记录维护；调度等小状态仍为项目 NVS。

IoT 记录 QoS 1 PUBACK 只解除本次 MQTT 在途发布，不删除补发事实；只有匹配对应 `recordStreamId` 且不越过该 Store 日志头的累计 `record-ack` 才推进该流 RAM 水位，绝不联动另一流。各流 ACK 每累计 32 条或推进后满 24 小时才保存 Base Store 释放检查点；Store 容量满且仍有未确认记录时停止该 Store 追加，全部保留记录已确认后才允许分段轮转。

配置文件存在但当前副本和备份都无效时，固件保持安全停机，不用默认值覆盖。重新编译、串口烧录或 HTTP OTA 不得清理或覆盖已有有效 NVS/LittleFS 数据。只有业务结构明确不兼容时才拒绝启动并提示重新配置；不得自行猜测或迁移旧结构。

LittleFS 挂载失败不会自动格式化。格式化只允许用户在确认没有需保留数据后从 System 页明确执行；它不会清 WiFi、Web Auth 或其它 NVS。格式化完成后，业务回调负责重建当前配置和存储。

当前应用在源码中提供 `admin/admin` 作为首次启动 Web 默认认证，已保存的 `eb_web` 认证优先。该共享默认值只适用于受控本地初始化，部署后必须立即在 Web Auth 页面修改；在进入批量交付前应改为每台设备唯一的受控初始化凭据方案，不能把共享默认值当作量产安全边界。

## 5. 构建与自动测试

在本目录执行；复用 Base 已准备的受控 Core 3 TLS 工具链，不重复重建工具链或全量测试矩阵：

```sh
python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 3 --tls-toolchain run -e esp32_irrigation_arduino3
python3 scripts/test_storage_views.py
python3 scripts/test_hardware.py
python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 2 test -e native -f test_command_journal
```

按改动选择定向检查。当前证据：实际 Base Store/Conditions + SDK 内存 FS 检查通过（容量/预算、读写损坏、原始时间冻结、恢复重试、双流 ACK 隔离、显式格式化、故障恢复）；GPIO/LEDC 安全预置与故障注入通过；命令账本 4 项、控制器/调度 50 项通过；lab 协议 8 项/定义校验、server 定义加载 3 项/统计 1 项/源码类型检查、wx records 8 项通过。Base 维护 MQTT 26 项及架构/安全检查通过。

完整本机 MQTT/TLS 配置参与目标链接，bin 通过每槽至少 8% 余量门禁。无私密配置时可用 `scripts/build_iot_release_fixture.py` 检查链接容量；它使用无效 CA 夹具，不能用于真实联网验收。设备端存储测试仅适用于明确可清空的试验设备，本轮未执行该测试固件。

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
[env:esp32_irrigation_arduino3]
custom_esp32base_webota_host = irrigation-controller.local
custom_esp32base_webota_user = <current-web-auth-user>
custom_esp32base_webota_password = <current-web-auth-password>
```

操作者确认设备空闲和维护窗口后显式执行：

```sh
python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 3 --tls-toolchain run -e esp32_irrigation_arduino3 -t webota
```

普通构建和测试不会触发 OTA。不得提交真实设备地址、账号或密码。不得配置 espota、ArduinoOTA 或 3232 端口。

## 8. 实机验证边界

514e149 版本已串口烧录、校验并启动，双 Store 就绪、业务无存储故障；NTP 同步、MQTTS 连接（含证书日期校验），平台收到完整业务状态和诊断。实机发现 Core 3 初始 GPIO 电平预置失效及 OTA 退出预算不足，两项修复已进入 f76f37b / Base cdd1923，两项已通过实机与 Web OTA。

本机服务通过 `./server restart` 更新至当前源码，入口 `http://127.0.0.1:17832`；未更新 NUC 或公开发布小程序。目标编译、主机测试、真实 MQTT、OTA 与真实水路分别记录。核心板缺少 DS3231 和水路部件，不能把其 RTC/流量故障当作平台接入失败，也不能以核心板通过代替现场验收。

### 2026-09-10 当前固件实机闭环

- 修复 MQTT 参数执行时 loopTask 栈溢出：完整学习状态快照不再叠加在无关的暂停/恢复分支；计划配置、命令判断和活动快照保持独立栈帧（LTO 下也不内联）。22 处仅查询 active 的调用改为直接读取控制器标志，保留完整状态 API，不新增常驻缓冲、不调大栈。
- 控制器/调度定向测试 50/50，当前 Core 3 TLS 目标编译及 8% OTA 余量门禁通过，静态 RAM 不变。最终 Web OTA 耗时 26.76 秒，当前运行 app0 / valid，镜像校验 SHA256 `cb96281227d520d2275e52a04a77194308f6950aa6f0f82c07dd5d49ce9d484d`；原 NVS、配置和记录保留。
- 自动总控 `9ed17407-d72b-4fc6-a42a-7faf151b6a04`、最终计划保存 `30be985e-2f0e-4e6a-97e7-7c17a3fa7746` 均 accepted → succeeded。计划测试保留空计划，仅按正常保存递增 revision。
- 单次出水 `e16e6d3f-f406-4b4b-b700-8c90de754d2f` accepted → running → failed / flow_start_timeout；核心板没有流量计，20 秒启动保护安全结束，非实际水路成功验收。平台已收到 watering.failed，流 `e20eebc1-e2da-40f5-83ac-43eb85c58296` 序号 1 的累计业务 ACK，原生小程序显示失败结果与浇水事实。设备仍 ready、空闲、无存储故障。
- Base API 文档宽限上限漏改已在 `10505fe` 修正推送；本次设备源码构建依赖与 `cdd1923` 相同。旧 panic 证据不能冒充新版复位原因；新版 OTA 后为 software，已验证命令期间无重启。

- 最终本机 server 21b9eb9 已构建重启、ready=true；原生微信暂停 `09347a1a-06d9-408d-99cb-0917a05eb67c` 和恢复 `8e415aa1-40b2-4eab-98f3-64ceecb0102f` 均约 2 秒 succeeded，相应审计事实显示正常。结束时同一 bootCount 90、reset=software、在线/空闲、自动总控 enabled、空计划 revision 3，无存储故障。旧无回执命令已过期并退出活动区。
- 已核对存储重构遗留：SDK 序号/世代/ACK 接管、首次启动与当前格式恢复、未确认保护、维护安全及资源容量均已实现并完成相关快速检查；旧方案中 NVS 命令账本、临时 RecordSync 和“不拦截维护”描述已删除并同步当前设计。剩余仅上表现场试运行及正式发布边界。
