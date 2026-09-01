# ESP32 灌溉控制器固件

本目录包含当前唯一有效的灌溉控制器固件。产品和业务规则以 `../docs/当前方案/` 为准，硬件事实以 `../pcb_irrigation/` 下 2026-07-11 的 BOM 和网表为准，公共设备能力以同级 `../../Esp32Base` 当前文档和代码为准。

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
- 当前 `irrigation-controller/v1` MQTTS 外围适配：固定七个 channel Topic、QoS 1、retain/LWT、五类命令、receipt/progress、八项完整状态投影和可靠业务记录流。

MQTT 适配只调用现有配置、调度和浇水入口，不直接操作 GPIO，也不建立第二套网络生命周期。未提供本机私密 MQTT 配置时，固件保持本地能力可用且不尝试连接 Broker；掉线、重连或平台不可用不得阻断本地 Web、RTC 自动调度、保护和安全停机。

产品永久不实现 LCD2004、本地菜单和四按钮业务交互。

## 2. Esp32Base 接入契约

正式固件使用：

```ini
-D ESP32BASE_PROFILE=ESP32BASE_PROFILE_IOT
-D ESP32BASE_MQTT_MAX_PAYLOAD_BYTES=4096
-D ESP32BASE_MQTT_ALLOW_UNCHECKED_CERTIFICATE_DATES=1
-D ESP32BASE_ENABLE_RECORD_STORE=1
-D ESP32BASE_ENABLE_APP_EVENTS=1
-D ESP32BASE_ENABLE_APP_EVENT_CONDITIONS=1
-D ESP32BASE_ENABLE_APP_CONFIG=1
-D ESP32BASE_ENABLE_RTC=1
-D ESP32BASE_RTC_DRIVER=ESP32BASE_RTC_DRIVER_DS3231
```

设备记录测试使用 `ESP32BASE_PROFILE_OFFLINE`，只显式开启测试需要的 Record Store 和 App Events。

当前基础库只支持 `MINIMAL / OFFLINE / LOCAL / IOT` 四个 Profile。项目不得重新引入旧 Profile、`ESP32BASE_ENABLE_WEB_OTA`、`ESP32BASE_ENABLE_ARDUINO_OTA`、ArduinoOTA/espota、3232 监听端口或已删除的认证读取 API。其它项目接入或适配当前基础库时，先阅读：

- `../../Esp32Base/docs/13_integration_and_upgrade.md`
- `../../Esp32Base/docs/02_profiles.md`
- `../../Esp32Base/docs/04_web.md`
- `../../Esp32Base/docs/05_ota.md`

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
7. 加载业务配置、命令幂等 journal、调度状态、本地历史、IoT 记录流和事件存储。
8. 只有全部必需状态有效时才进入业务 ready。

正常循环先推进业务状态机和 IoT 外围适配，再调用 `Esp32Base::handle()`。Web/MQTT handler 不执行校准、等待出水或其它长时间流程；MQTT 消息由 Esp32Base 有界邮箱串行分发。中断只累计流量脉冲，不做日志、存储、业务判断或硬件切换。MQTT 状态和记录序列化共用 `IrrigationIot` 长期对象中的单一 4097 B 缓冲，不在 `loopTask` 栈上创建 4 KiB 临时数组；这是当前 4096 B payload 上限下的硬性栈安全边界。

WiFi modem sleep 保持 STA、Web、NTP、OTA、调度和保护可用；本项目不进入 Deep-sleep，不降低 CPU 频率，也不改变泵阀控制时序。OTA 期间由 Esp32Base 临时关闭 power save，结束后恢复。

## 4. 配置与持久数据

当前数据定义：

- 灌溉 JSON 配置：schema v4，权威路径 `/app/irrigation/config.json`；
- 本地浇水历史：`watering` Store v5，最大逻辑预算 512 KiB；
- IoT 可靠记录流：`irrigation-iot` Store v1，固定 200 条、151168 B，单调 `recordStreamId + recordSequence`，只在完整事实形成后追加；
- MQTT 命令 journal：NVS 中固定 16 条，保存不可变签名、receipt 和可信终态；重启不为中断任务推断终态；
- App Events：使用 Esp32Base 当前事件格式和默认 100 KiB 预算；
- 系统文件日志：4 × 32 KiB，默认 WARN；
- 标量系统参数：Esp32Base App Config / NVS；
- 自动浇水总控、调度防重复标记、在线检查点、记录流身份和低频累计 ACK 检查点：项目 NVS 小状态。

IoT 记录 QoS 1 PUBACK 只解除本次 MQTT 在途发布，不删除补发事实；只有匹配当前流且不越过已产生日志头的累计 `record-ack` 才推进 RAM 水位。ACK 每累计 32 条或推进后满 24 小时才写 NVS；容量满时只有整个最旧分段都已确认才允许轮转，否则报告 `record_sync_backlog_full` 对应的记录存储故障并停止追加。

配置文件存在但当前副本和备份都无效时，固件保持安全停机，不用默认值覆盖。重新编译、串口烧录或 HTTP OTA 不得清理或覆盖已有有效 NVS/LittleFS 数据。只有业务结构明确不兼容时才拒绝启动并提示重新配置；不得自行猜测或迁移旧结构。

LittleFS 挂载失败不会自动格式化。格式化只允许用户在确认没有需保留数据后从 System 页明确执行；它不会清 WiFi、Web Auth 或其它 NVS。格式化完成后，业务回调负责重建当前配置和存储。

当前应用在源码中提供 `admin/admin` 作为首次启动 Web 默认认证，已保存的 `eb_web` 认证优先。该共享默认值只适用于受控本地初始化，部署后必须立即在 Web Auth 页面修改；在进入批量交付前应改为每台设备唯一的受控初始化凭据方案，不能把共享默认值当作量产安全边界。

## 5. 构建与自动测试

在本目录执行。首次使用先准备Esp32Base仓库隔离的双Core环境；本项目当前固定通过Core 2.x目录构建，不依赖用户默认`~/.platformio`：

```sh
python3 ../../Esp32Base/scripts/ensure_arduino_platformio.py
python3 scripts/generate_web_assets.py --check
python3 ../../Esp32Base/scripts/pio_arduino.py 2 test -e native
IOT_DEVICE_LAB_DIR=/Users/tyg/workspace/iot-device-lab \
  python3 ../../Esp32Base/scripts/pio_arduino.py 2 test -e native_iot_vectors
python3 ../../Esp32Base/scripts/pio_arduino.py 2 test -e esp32_record_test --without-uploading --without-testing
python3 scripts/build_iot_release_fixture.py

# 有专用可清空实验板时，执行设备端测试；端口按实际环境替换
python3 ../../Esp32Base/scripts/pio_arduino.py 2 test -e esp32_record_test \
  -f test_record_store_device \
  --upload-port /dev/cu.usbserial-XXXXXXXX \
  --test-port /dev/cu.usbserial-XXXXXXXX
```

含义：

- Web 资源检查：确认 `web-src/` 与生成的压缩固件数组一致；
- Native：覆盖配置、控制器、记录编解码、调度、异常水流、校准、时间、IoT 严格协议和命令 journal；
- 共享向量：直接消费当前 `iot-device-lab` 合法/非法命令向量，防止终端与平台契约漂移；
- 设备记录测试编译：确认 Esp32Base OFFLINE、Record Store、App Events、200 条 IoT 流容量断言和项目设备测试可链接；
- 正式构建：夹具脚本拒绝覆盖已有私密头，临时生成带 2 KiB CA 正文的非敏感配置，清理旧目标后完整链接 MQTT/TLS 路径，使用 classic ESP32 4MB balanced 双 OTA 分区并检查 slot 余量。

当前自动与设备端验证基线（2026-09-01）：

- Web 资源漂移检查通过；
- Native 测试 101/101 通过；
- 当前共享命令向量测试 1/1 通过；
- `esp32_record_test` 设备端 9/9 通过，覆盖正式 Store 定义、固定 200 条 IoT 容量、追加/分页/重载、分段轮转、CRC 损坏、业务事件、调度 NVS 和在线检查点；
- `esp32_irrigation` Core 2.0.16 构建通过；
- 使用非敏感完整 MQTTS 配置夹具链接，其中 CA 占位正文为 2 KiB（避免空配置被 LTO 裁掉 MQTT/TLS 路径或低估证书尺寸）；
- 使用实验环境真实 3298 B CA 链完整构建：RAM 114940 B / 35.1%，Flash 1438761 B / 91.5%；
- 真实配置 `firmware.bin` 1445344 B；
- 1.5 MiB OTA slot 剩余 127520 B / 8.11%。

N4 设备继续使用现有 1.5 MiB 双 OTA + 896 KiB LittleFS 分区，避免改变分区导致现有配置和业务数据失效。经确认，IOT 固件发布门禁固定为至少 8% OTA slot 余量；当前只高出门禁约 1.65 KiB，后续任何代码、CA 或静态资源变化都必须重新执行带完整 MQTTS 配置的构建，不能以空私密配置构建的 17% 余量作为发布依据。

代码变更后必须重新执行这些命令，并用新的实际结果更新本节；不能保留失效的历史构建数字。

## 6. Web 静态资源

大段业务 HTML、CSS 和 JavaScript 在 `web-src/` 维护，通过以下命令确定性生成压缩固件数组：

```sh
python3 scripts/generate_web_assets.py
```

正式构建前置脚本会执行 `--check`，源文件与生成物不一致时拒绝构建。运行时使用 ESP32 ROM miniz 和单个复用缓冲区解压并通过 Esp32Base 分块响应发送，不写入 LittleFS，不绕过认证或基础 Web 生命周期。

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
python3 ../../Esp32Base/scripts/pio_arduino.py 2 run -e esp32_irrigation -t webota
```

普通构建和测试不会触发 OTA。不得提交真实设备地址、账号或密码。不得配置 espota、ArduinoOTA 或 3232 端口。

## 8. 实机发布验收

自动测试和构建不能替代实机。每个待发布版本至少验证：

- 写入校验、启动日志、业务 ready 和安全输出初始态；
- 已有有效配置、WiFi、Web Auth、计划、记录和事件在非破坏升级后仍保留；
- 六路阀互斥、切换间隔、停止和断电输出关闭；
- 实际安装水泵时的启停延时和继电器行为；
- 流量脉冲、校准、基准学习、无流量、高低流量和关阀后异常水流；
- DS3231、NTP、断网启动、离线自动计划和时间倒退保护；
- 本地 Web 桌面/窄屏、认证、跨站 POST 拒绝和并发请求；
- 浏览器 OTA、命令行 raw HTTP OTA、失败回滚和升级后配置保留；
- LittleFS/NVS 写失败、空间不足、重启和掉电恢复；
- 长时间运行、最小 heap、看门狗、WiFi 恢复和温升。

当前 IOT 镜像已在专用 ESP32-D0WD-V3 核心板完成串口写入和 Hash 校验，并取得以下实机证据：

- WiFi、NTP、真实 CA/hostname 校验的 MQTTS 认证和 CONNECT 成功；command 与 `record-ack` 双订阅完成后，online 和八项状态均由当前 `iot-device-lab` 定义校验通过；
- 首次完整状态发布、周期发布和多次重连后不再出现 `Stack canary watchpoint triggered (loopTask)`；页面观测的 loopTask 栈最低余量为 1.90 KiB，最小 heap 为 76.21 KiB，看门狗 trip reset 为 0；
- 硬复位得到 retained LWT offline，重连使用新的 `connectionId`；命令 journal 中同一 stop 命令的 accepted/succeeded 证据在重启后使用新连接周期重新发布；
- 可靠记录在没有累计 ACK 时按 5 秒窗口补发；累计 ACK 将同一 `recordStreamId` 从 sequence 1 推进到 2；因仅推进 2 条未触发低频 NVS 检查点，重启后按设计再次补发 sequence 1，同时 `recordStreamId` 保持不变；
- 设备记录测试通过后已恢复并再次烧录真实 IOT 固件，启动进入 `business_ready`，记录存储状态正常。

当前核心板没有泵阀、水路、流量计和 DS3231，因此日志中的 DS3231 I2C 写回失败是硬件缺失，不能作为 RTC 验收；六路输出、泵延时、流量保护和现场安全仍需完整灌溉硬件完成 G6。当前镜像也尚未完成 HTTP Web OTA、长时间稳定运行和浏览器视觉验收。
