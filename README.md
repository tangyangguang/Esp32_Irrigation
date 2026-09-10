# ESP32 Irrigation

单板 12V DC 智能浇水控制器：六路电磁阀互斥、可选外部泵控制信号、流量保护、手动浇水、RTC 自动计划、计量参数设置和本地 Web。

- 硬件权威：`pcb_irrigation/` 下 2026-07-11 的 BOM 与网表。
- 产品与安全：[当前方案](docs/当前方案/README.md)。
- 实现、构建、验证和唯一当前待办：[固件说明](firmware/README.md)。
- 平台定义与类型验收：[irrigation-controller](../../platform/iot-device-lab/device-types/irrigation-controller/README.md)。
- 基础库接入：[Esp32Base](../../foundation/Esp32Base/README.md)；平台 SDK：[iot-device-sdk](../../platform/iot-device-sdk/README.md)。

当前源码已完成双 Store 存储重构：watering 保存浇水事实，irrigation-audit 保存必要审计；本地读取和平台补发使用同一份事实，四项持续条件使用 Base Conditions。旧 App Events、独立 200 条补发队列和 616 字节记录设计已经废弃。

固件已通过最新 SDK Session、ModelPublisher、双 RecordStream 接入 Base MQTT，完整 TLS 工具链和诊断心跳已参与目标构建。当前 4 MiB 分区为双 1728 KiB OTA + 512 KiB LittleFS，历史条数按授权调整，记录字段和功能保留；当前版本已完成目标编译、串口/OTA、真实 MQTT、服务端落库/业务 ACK 和原生小程序链路验证。未完成事项及后续物理验证统一列于固件说明，不保留独立过程流水账。
