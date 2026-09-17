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
| Flash（应用分区） | 1,571,593 B（88.8%，分区 1,769,472 B） |
| 静态 RAM | 103,132 B（31.5%，327,680 B） |
| OTA 镜像 firmware.bin | 1,543,248 B |
| 对侧 OTA 槽余量 | 226,224 B（约 221 KiB，12.78%） |

运行堆/栈峰值未测量；OTA 余量偏紧，尺寸优化为待定项，不通过削弱 TLS/OTA/日志/记录预算来换体积。2026-09-17 已对 192.168.2.155（esp32-irr-28562f795e60）Web OTA 烧录，启动日志确认 MQTT 经 TLS 连接 z84e9fd1.ala.cn-hangzhou.emqxsl.cn:8883 成功；连接后约 1 分钟进入 dev 平台发现候选（iot_home_dev.device_discovery_candidates，状态 online），待小程序确认绑定。小程序联调、物理水路动作、长稳与断电验证尚未在本机检查范围内，需另行授权。

## 调度边界修复（2026-09-17）

- 定时暂停到期**自动恢复的那一分钟不触发计划**，与手动恢复、启动分钟、只读“下一次”查询及 `03` 设计第 183 行统一；恢复后分钟仅重建检查点，下一分钟正常调度。
- 修复阈值内**跨午夜 RTC 回拨**时 `markProcessed` 可能用较早日期覆盖较新当天掩码的问题：回跨到已保存的前一天时合并进前一天掩码并保留较新当天掩码，避免重启后重复浇水；落入未跟踪的更早日期时安全跳过。
- 覆盖：`test_pause_modes_skip_or_resume_without_immediate_manual_run` 更新，新增 `test_cross_midnight_rollback_never_clobbers_newer_day_mask`；两例在旧实现上均失败、修复后通过。native 82 项全过，主目标 `pio run -e esp32_irrigation_arduino3` 链接成功；纯逻辑改动，不改变配置 schema、NVS blob 结构与平台契约。

## 存储与平台契约

- 配置 schema 5 不变；试验阶段零历史兼容、零数据迁移，旧测试数据不读取、不转换、不自动清理。
- 浇水 codec v4：270 B 业务 payload（54 B 头含 36 B commandId + 6×36 B 水路，含建议基准脉冲率）；审计 20 B 业务 payload。v3 旧记录不读取。
- 两个持久 Store（浇水 v9 / 审计 v5，均 `PreserveUnreleased`）物理槽为 24 B IR/v1 头 + 业务字节，预算 160/48 KiB；**单一持久 Store 同时服务本地历史与平台可靠补发**，内嵌 SDK `RecordStream`，不设第二份 outbox，断网补发与 record-ack 经同一存储水位管理。
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
