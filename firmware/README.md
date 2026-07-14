# ESP32 灌溉控制器固件

本目录是固件实现位置，代码只依据当前确认方案。

后续实现只依据：

- `../docs/当前方案/` 中确认的软件设计。
- `../pcb_irrigation/` 下的定稿 BOM 和网表。
- `../../Esp32Base` 已核实的公共能力。

当前已建立 PlatformIO 工程、业务配置 JSON 编解码与掉电安全保存、完整规则校验、定稿 PCB 引脚定义和硬件安全层。普通浇水控制已实现阀吸合与维持、泵启停时序、多区域顺序、用户停止、启动无流量和运行中无流量保护；结束结果使用固定 88 字节 payload 接入 `Esp32BaseRecordStore`，异常业务影响接入 `Esp32BaseAppEvents`，并提供最新分页、按 ID 读取、存储状态和确认后清空的业务入口。尚未接入 Web 命令、低/高流量和非计划流量报警。启动时只有加载到有效配置或成功创建默认配置后才继续；存在配置文件但所有副本均无效时保持输出关闭，不用默认值覆盖。后续按 `../docs/当前方案/03-软件设计原则与边界.md` 分层实现。

常用验证命令：

```sh
pio test -e native
pio run -e esp32_irrigation
pio test -e esp32_record_test --upload-port <serial-port> --test-port <serial-port>
```

以上命令已在 2026-07-14 通过：native 共 24 项，已连接的 ESP32 记录测试共 6 项。`esp32_record_test` 使用独立测试 Store 验证实际 LittleFS 容量、完成记录、最新分页、按 ID 读取、reload、分段轮换、CRC 损坏降级、逻辑清空和业务事件字段映射；测试结束删除专用 Store，不格式化文件系统。硬件输出、实阀、流量计、突然断电写入和 LittleFS 真实写失败仍需在完整 PCB 上验证。
