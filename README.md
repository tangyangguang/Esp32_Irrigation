# ESP32 Irrigation

本仓库维护单板 12V DC 智能浇水控制器。

当前有效依据：

- 硬件：`pcb_irrigation/BOM_Board1_Schematic1_2026-07-11.xlsx`
- 网表：`pcb_irrigation/Netlist_Schematic1_2026-07-11.tel`
- 产品与软件方案：`docs/当前方案/`
- ESP32 固件工程：`firmware/`

当前方案位于 `docs/当前方案/`，固件在方案确认后按层实现和测试。

当前控制入口包括设备本地 Web 和统一 IoT 平台 MQTT 适配；两者调用同一套浇水业务入口。
旧 MQTT 远程控制方案、Mac 测试控制台和 N1/Cloudflare 直连设备 Web 方案保持删除且不兼容。
设备类型定义和通信规则以
`/Users/tyg/workspace/iot-device-lab/device-types/irrigation-controller/` 与
`/Users/tyg/workspace/iot-device-lab/docs/02-公共通信规则.md` 为唯一协议权威。
