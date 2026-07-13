# ESP32 灌溉控制器固件

本目录是当前 ESP32 固件工程。产品和软件实现以 `../docs/当前方案/` 为准，硬件引脚以 `../pcb_irrigation/` 下的定稿网表为准。

- 固件使用 `../../Esp32Base` FULL profile 提供的通用基础能力。
- 本项目只实现浇水领域的硬件控制、业务、存储和 Web 页面。
- 不能修改 `../../Esp32Base`；发现基础库缺口时按项目规则单独反馈。

构建：

```bash
pio run -d firmware
```
