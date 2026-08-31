# ESP32 Irrigation

本仓库维护单板 12V DC 智能浇水控制器。

当前有效依据：

- 硬件：`pcb_irrigation/BOM_Board1_Schematic1_2026-07-11.xlsx`
- 网表：`pcb_irrigation/Netlist_Schematic1_2026-07-11.tel`
- 产品与软件方案：`docs/当前方案/`
- ESP32 固件工程：`firmware/`

建议阅读顺序：

1. `docs/当前方案/README.md`：当前产品与业务边界；
2. `firmware/README.md`：固件接入、构建、OTA、验证和发布方法；
3. `../Esp32Base/docs/13_integration_and_upgrade.md`：基础库四 Profile 及其它业务项目的版本适配。

当前固件使用 Esp32Base `LOCAL` Profile，并显式启用 DS3231 RTC、Record Store、App Events 和 App Config。当前本地控制入口包括设备 Web 和 RTC 自动调度；最终统一 IoT 平台 MQTT 适配仍调用同一套浇水业务入口，但固定在本地功能、风险和实机回归完成后按届时最新契约实施。产品不实现 LCD2004、本地菜单或四按钮业务交互；板载 GPIO13 红色状态灯提供最小运行提示，现场紧急止水使用上游水源开关并可同时关闭控制器电源。
设备类型定义和通信规则以
`/Users/tyg/workspace/iot-device-lab/device-types/irrigation-controller/` 与
`/Users/tyg/workspace/iot-device-lab/docs/02-公共通信规则.md` 为唯一协议权威。
