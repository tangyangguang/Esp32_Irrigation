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
- `LED1 / GPIO13` 低电平点亮的非阻塞状态指示。

当前本地定稿固件不编译、不配置、不连接 MQTT。最终平台接入必须重新读取当时 `iot-device-lab` 中灌溉设备类型和公共通信规则的唯一最新契约，并切换到 `Esp32Base` 的 `IOT` Profile；不得恢复已删除的旧平台适配。

产品永久不实现 LCD2004、本地菜单和四按钮业务交互。MQTT 或平台不可用不得阻断本地 Web、RTC 自动调度、保护和安全停机。

## 2. Esp32Base 接入契约

正式固件使用：

```ini
-D ESP32BASE_PROFILE=ESP32BASE_PROFILE_LOCAL
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

## 3. 启动与运行边界

启动顺序固定为：

1. 第一项初始化 `BoardHardware`，关闭泵信号、全部阀 PWM 和阀驱动总使能。
2. 在输出安全关闭后初始化状态灯。
3. 初始化 I2C，并将 DS3231 总线交给 `Esp32BaseRtc`。
4. 设置 Web 默认认证，注册业务 Web、App Config 和文件系统格式化回调。
5. 调用 `Esp32Base::begin()`；失败时保持全部输出关闭并进入故障指示。
6. 基础库启动成功后启用 WiFi modem sleep。
7. 加载业务配置、调度状态、记录和事件存储。
8. 只有全部必需状态有效时才进入业务 ready。

正常循环先推进业务状态机，再调用 `Esp32Base::handle()`。Web handler 不执行校准、等待出水或其它长时间流程。中断只累计流量脉冲，不做日志、存储、业务判断或硬件切换。

WiFi modem sleep 保持 STA、Web、NTP、OTA、调度和保护可用；本项目不进入 Deep-sleep，不降低 CPU 频率，也不改变泵阀控制时序。OTA 期间由 Esp32Base 临时关闭 power save，结束后恢复。

## 4. 配置与持久数据

当前数据定义：

- 灌溉 JSON 配置：schema v4，权威路径 `/app/irrigation/config.json`；
- 浇水记录：`watering` Store v5，最大逻辑预算 512 KiB；
- App Events：使用 Esp32Base 当前事件格式和默认 100 KiB 预算；
- 系统文件日志：4 × 32 KiB，默认 WARN；
- 标量系统参数：Esp32Base App Config / NVS；
- 自动浇水总控、调度防重复标记和在线检查点：项目 NVS 小状态。

配置文件存在但当前副本和备份都无效时，固件保持安全停机，不用默认值覆盖。重新编译、串口烧录或 HTTP OTA 不得清理或覆盖已有有效 NVS/LittleFS 数据。只有业务结构明确不兼容时才拒绝启动并提示重新配置；不得自行猜测或迁移旧结构。

LittleFS 挂载失败不会自动格式化。格式化只允许用户在确认没有需保留数据后从 System 页明确执行；它不会清 WiFi、Web Auth 或其它 NVS。格式化完成后，业务回调负责重建当前配置和存储。

当前应用在源码中提供 `admin/admin` 作为首次启动 Web 默认认证，已保存的 `eb_web` 认证优先。该共享默认值只适用于受控本地初始化，部署后必须立即在 Web Auth 页面修改；在进入批量交付前应改为每台设备唯一的受控初始化凭据方案，不能把共享默认值当作量产安全边界。

## 5. 构建与自动测试

在本目录执行：

```sh
python3 scripts/generate_web_assets.py --check
pio test -e native
pio test -e esp32_record_test --without-uploading --without-testing
pio run -e esp32_irrigation
```

含义：

- Web 资源检查：确认 `web-src/` 与生成的压缩固件数组一致；
- Native：覆盖配置、控制器、记录编解码、调度、异常水流、校准和时间；
- 设备记录测试编译：确认 Esp32Base OFFLINE、Record Store、App Events 和项目设备测试可链接；
- 正式构建：使用 classic ESP32 4MB balanced 双 OTA 分区，并执行镜像 slot 余量检查。

当前自动验证基线：

- Web 资源漂移检查通过；
- Native 测试 87/87 通过；
- `esp32_record_test` 设备测试固件编译通过，未在当前版本实机运行；
- `esp32_irrigation` 构建通过；
- RAM 90884 B / 27.7%；
- Flash 1247441 B / 79.3%；
- `firmware.bin` 1254016 B；
- 1.5 MiB OTA slot 剩余 318848 B / 20.27%。

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
pio run -e esp32_irrigation -t webota
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

当前 Profile 定型版本未执行烧录、HTTP OTA、清 NVS、格式化 LittleFS 或真实水路回归。已有历史实机结果只证明当时版本和硬件状态，不能作为当前镜像的发布证据。
