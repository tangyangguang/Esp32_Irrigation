# ESP32 灌溉主控固件

这是当前唯一有效方案的 ESP32 固件工程。

基础原则：

- 固件基于仓库同级目录的 `../Esp32Base`，使用 FULL profile。
- 通用基础能力优先使用 `Esp32Base`，包括 WiFi、Web、认证、OTA、Web OTA、文件系统、日志、事件、配置、健康、看门狗、NTP、mDNS、DNS、时间和 RTC。
- 本项目只实现智能浇水业务能力。
- 如果基础库能力有问题，优先修复 `../Esp32Base`，不要在本工程里写绕过补丁。
- 当前产品不启用 RS485、主从站、分站或历史兼容架构。

当前骨架包含：

- PlatformIO 构建环境。
- `Esp32Base` FULL profile 启动。
- DS3231 RTC、App Config、App Events 启用。
- 当前 PCB 的板级引脚定义。
- 安全上电默认输出关闭。

构建：

```bash
pio run -d master-esp32
```
