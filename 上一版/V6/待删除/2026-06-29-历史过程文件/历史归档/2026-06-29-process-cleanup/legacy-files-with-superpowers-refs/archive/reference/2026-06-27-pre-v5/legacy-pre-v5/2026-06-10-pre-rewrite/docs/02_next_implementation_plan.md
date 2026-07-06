# 当前实现状态与后续验收

当前固件已按 2 Flow / 6 Zone 新模型完成软件闭环重构，不保留旧 4 路、每路一个流量计、启动补偿校准、旧 API 或旧存储格式兼容。

详细设计仍以 [2 Flow / 6 Zone 设计文档](superpowers/specs/2026-06-05-two-flow-six-zone-irrigation-design.md) 为准；实施轨迹见 [实施计划](superpowers/plans/2026-06-05-two-flow-six-zone-irrigation-redesign.md)。

## 已落地的软件能力

1. 硬件模型：最多 2 个 Flow 输入、最多 6 个 Valve PWM 输出、最多 5 个本地按钮、I2C LCD1602 状态屏。
2. 配置模型：`FlowConfigStore`、`ZoneConfigStore`、`SystemConfigStore`、`PlanStore`、`FlowAlertStore` 使用新 namespace 和新字段。
3. Flow 计量：固定点 K+Offset，按 Flow 1/2 独立计数、计算流量和估算总水量。
4. Zone 运行：每个 Zone 可任意归属 Flow；同 Flow 互斥，不同 Flow 可并行；无脉冲、低流量、高流量均可保护停机。
5. 本地交互：本地按钮只操作 enabled Zone；启动、单路停止、Stop All 都需要同键二次确认；本地不做校准输入。
6. 计划调度：每个 Zone 最多 6 条计划；同 Flow 冲突进入内存队列；超过 `queuedPlanMaxDelaySec` 后过期。
7. 校准：Flow 参数使用 K+Offset pending/apply/rollback；Zone 正常流量基线也使用 pending/apply/rollback。
8. 记录与事件：浇水记录写入 `/irr/records_v1.bin`；业务事件写入 Esp32Base App Events；待机漏水按 Flow 记录和阻断。
9. Web/API：提供总览、Flows、Zones、Plans、Calibration、Settings、Records、Events 页面和对应核心 API。

## 软件验证

```bash
node scripts/check-web-structure.mjs
pio run
```

## 尚未实机验证

1. 2 路 Flow 输入在真实线缆、上拉、电磁干扰和喷头组合下计数稳定。
2. 6 路 Valve PWM 输出、电磁阀吸合、保持占空比和驱动温升可靠。
3. 同 Flow 互斥、不同 Flow 并行在真实供水和增压泵条件下符合预期。
4. K+Offset 校准在实际 ZJ-S201C/透明流量计上的误差可接受。
5. Zone 学习得到的正常流量基线能适配少量喷头和大量喷头两种工况。
6. `noPulseTimeoutSec`、低流量/高流量阈值、待机漏水窗口在真实水塔/增压泵场景下安全。
7. GPIO5 作为 Valve6 输出不影响 ESP32 启动采样。
8. 72 小时连续运行无异常重启、记录损坏或阀门误动作。
