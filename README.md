# ESP32 Irrigation

单板 12V DC 智能浇水控制器：六路电磁阀互斥、可选外部泵控制信号、流量保护、手动浇水、RTC 自动计划、校准和本地 Web。

- 硬件权威：`pcb_irrigation/` 下 2026-07-11 的 BOM 与网表。
- 产品与安全：[当前方案](docs/当前方案/README.md)。
- 实现、构建、验证和唯一当前待办：[固件说明](firmware/README.md)。
- 平台定义与类型验收：[irrigation-controller](../../platform/iot-device-lab/device-types/irrigation-controller/README.md)。
- 基础库接入：[Esp32Base](../../foundation/Esp32Base/README.md)；平台 SDK：[iot-device-sdk](../../platform/iot-device-sdk/README.md)。

当前源码已完成双 Store 存储重构：watering 保存浇水事实，irrigation-audit 保存必要审计；本地读取和平台补发使用同一份事实，四项持续条件使用 Base Conditions。旧 App Events、独立 200 条补发队列和 616 字节记录设计已经废弃。

固件目前仍直接使用 Base MQTT，尚未完成最新 SDK、完整 TLS 工具链和诊断心跳接入。历史版本的核心板或平台通过结果不能证明本次候选版本可试运行。未完成事项及后续物理验证统一列于固件说明，不保留独立过程流水账。
