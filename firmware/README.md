# Firmware

本目录是 ESP32 灌溉控制器的新固件工程，使用 PlatformIO + Arduino framework。

当前阶段只建立基础环境和应用层骨架：

```text
platformio.ini
  ESP32 Dev Module 默认环境，引用 ../../Esp32Base，使用 ESP32BASE_PROFILE_FULL。

src/main.cpp
  固件入口，初始化 Esp32Base 和 IrrigationApp，并进入 loop。

src/irrigation/
  灌溉业务模块 stub。当前只定义职责边界、初始化顺序和集中默认值。

src/irrigation/IrrigationPinMap.h
  推荐 ESP32 引脚对应关系。PCB 定稿后优先只改这里。
```

当前明确不实现：

```text
GPIO/PWM 实际控制
完整运行状态机
计划调度行为
流量异常判断
记录文件落盘
正式 Web 页面和 API 行为
```

编译：

```bash
pio run
```

串口烧录：

```bash
pio run -t upload
```

首次串口烧录和 WiFi 配网完成后，可以使用 Esp32Base 提供的 Web OTA target：

```bash
pio run -t webota
```

`webota` 复用设备 Web Basic Auth。当前开发固件默认认证为：

```text
user = admin
password = admin
```

设备 IP 以串口启动日志或 `/esp32base` 状态页显示为准。
