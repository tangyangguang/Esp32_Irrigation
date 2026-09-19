# ESP32 灌溉控制器

六路 12V DC 电磁阀互斥、可选外部泵、流量保护、RTC 自动计划、手动浇水、本地 Web 与 IoT 平台/微信小程序接入。

## 文档

- [产品与软件文档](docs/README.md)
  - [01 产品与硬件边界](docs/01-产品与硬件边界.md)
  - [02 业务规则与安全](docs/02-业务规则与安全.md)
  - [03 软件设计原则](docs/03-软件设计原则.md)
  - [04 设备维护操作手册](docs/04-设备维护操作手册.md)
- [固件说明](firmware/README.md)：实现结构、构建与测试命令、存储/平台契约、当前验证基线。
- 通用能力：[Esp32Base](../../foundation/Esp32Base/README.md)。

## 当前状态

主固件固定使用 Esp32Base **IOT Profile**：本地 Web、灌溉业务核心与 MQTT 平台接入并存于同一固件，共用同一执行链路；平台对接按 `platform/iot-device` 的 irrigation-controller 契约实现。浇水与审计两个本地滚动 Store 同时服务本地历史与平台可靠补发。

源码与本机定向检查（native 测试、主目标链接、Web 资产一致性）已完成；真机 TLS/MQTT 联调、小程序联调、物理水路动作、长稳与断电验证为待授权的现场验证项，详见固件说明。

公共诊断已迁移到 `platform/iot-device/sdk` 专题01 后的新接口（`observe` / `readFull` / `readDynamic` / `commitFull` / `commitDynamic`）：采样在 `poll()` 每轮主循环推进一次窗口，发布只在连接首帧、值/网络变化或 60 秒周期触发；`read` 不再改变窗口，`commit` 只在 `state()` 已入队后执行，因此发布失败不会丢失窗口极值。旧的静态 `Esp32Diagnostics::collect()` 已由 SDK 删除，本项目不再引用。

## 目录

```text
docs/       产品、业务、软件设计与维护手册（当前唯一有效版本）
firmware/   PlatformIO 固件：src 业务源码、test 本机测试、web-src 页面源、scripts、partitions
pcb_irrigation/  2026-07-11 定稿 BOM 与网表
```
