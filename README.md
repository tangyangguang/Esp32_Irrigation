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

当前固件使用 Esp32Base `IOT` Profile，并显式启用 DS3231 RTC、Record Store、App Events、App Config 和 MQTTS。设备 Web、RTC 自动调度与平台命令共用同一浇水业务状态机；MQTT 断开或平台不可用不阻断本地控制、调度、保护和安全停机。固件实现当前 `irrigation-controller/v1` 的固定 Topic、命令幂等证据、完整状态快照以及带累计 `record-ack` 的持久记录流。产品不实现 LCD2004、本地菜单或四按钮业务交互；板载 GPIO13 红色状态灯提供最小运行提示，现场紧急止水使用上游水源开关并可同时关闭控制器电源。

设备类型定义和通信规则以
`/Users/tyg/workspace/iot-device-lab/device-types/irrigation-controller/` 与
`/Users/tyg/workspace/iot-device-lab/docs/02-公共通信规则.md` 为唯一协议权威。当前只完成 G4 代码、契约、构建和非硬件回归；真实 MQTT/TLS/ACL 属于 G5，泵阀和水路属于 G6。
