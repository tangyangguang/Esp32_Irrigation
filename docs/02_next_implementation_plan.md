# 当前实现与下一阶段计划

当前代码仍包含旧 4 路实现。本轮目标不是在旧架构上兼容扩展，而是按新模型重构到最多 2 个 Flow、最多 6 个 Zone。详细任务以 [实施计划](superpowers/plans/2026-06-05-two-flow-six-zone-irrigation-redesign.md) 为准。

## 下一阶段任务

1. 替换硬件常量：2 路 Flow 输入、6 路 Valve 输出、最多 5 个本地按钮、I2C 屏幕。
2. 替换配置模型：FlowConfigStore、ZoneConfigStore、SystemConfigStore、PlanStore 使用新 namespace 和拆 key 存储。
3. 重写 FlowMeter：固定点 K+Offset、有效频率范围、低于计量下限、样本无效。
4. 重写 Zone 运行：Flow 占用、同 Flow 互斥、不同 Flow 并行、无脉冲停机。
5. 增加 LocalControl 和 DisplayService：enabled Zone 选择、同键二次确认、状态显示。
6. 重写计划队列：按 Zone 触发，同 Flow 冲突进入 RAM 队列。
7. 重写校准和学习：Flow K+Offset 校准，Zone 正常流量学习。
8. 重写记录和异常：RecordStore 固定环形文件，FaultStateStore 管理 ZoneFault 和 FlowLeakFault。
9. 重写 Web/API：新页面、新路径、新字段，不保留旧 API 语义。
10. 更新 README、PROJECT_PLAN、验证清单和旧 road-management 文档。

## 软件验证

```bash
node scripts/check-web-structure.mjs
pio run
```

## 台架验收

1. 2 路 Flow 输入能计数。
2. 6 路 Valve PWM 输出正常。
3. 同一 Flow 下 Zone 互斥。
4. 不同 Flow 下 Zone 可并行。
5. 手动启动 Flow 忙时拒绝且不入队。
6. 计划冲突按队列顺序执行并能过期。
7. K+Offset 单点和多点校准计算正确。
8. 有脉冲但低于计量下限时不触发无水停机，也不累计低流量停机确认。
9. 无脉冲超时能关闭对应 Zone。
10. 待机 Flow 脉冲触发漏水保护。
11. 本地启动、单路停止、Stop All 都需要同键二次确认。
12. 记录文件掉电恢复逻辑可从 committed 记录恢复元数据。

## 实机验收

- 阀门实际吸合和 PWM 保持可靠。
- MOSFET、继电器或驱动模块温升正常。
- Flow 输入在真实线缆和上拉下计数稳定。
- 用户实际喷头组合下 K+Offset 误差可接受。
- `pressurizeSec` 和 `noPulseTimeoutSec` 匹配真实水路。
- 增压泵缺水场景能安全关阀。
- GPIO5 作为 Valve6 输出不影响 ESP32 启动采样。
- 72 小时连续运行无异常重启、记录损坏或阀门误动作。
