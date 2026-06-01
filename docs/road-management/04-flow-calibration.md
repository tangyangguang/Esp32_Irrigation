# 流量计现场校准方案

## 目标

流量计校准必须按现场环境执行。水压、管径、喷头、过滤器、阀门开启速度和管路长度都会影响启动阶段和稳定阶段的脉冲表现，不能只使用传感器标称值或其他场地实验结果。

校准完成后，每一路生成 3 个运行参数：

```text
startupPulseLimit     启动阶段脉冲数，0 表示不启用启动补偿
startupEstimatedMl    启动阶段全部脉冲对应的估算水量，单位 ml
stablePulsePerLiter   稳定阶段每升水产生的脉冲数
```

运行时水量估算公式：

```text
if startupPulseLimit == 0:
    totalMl = totalPulses * 1000 / stablePulsePerLiter

else if totalPulses <= startupPulseLimit:
    totalMl = totalPulses * startupEstimatedMl / startupPulseLimit

else:
    totalMl = startupEstimatedMl
            + (totalPulses - startupPulseLimit) * 1000 / stablePulsePerLiter
```

## 系统配置

校准配置是系统级参数，用于控制采样数量和内存上限：

```text
calibrationSampleTarget
范围：2-5
默认：5
说明：RAM 中最多保留的校准样本容量，只限制新增样本，不限制计算推荐参数。

calibrationMaxCaptureMin
范围：1-15 分钟
默认：10 分钟
说明：单次校准最长允许出水时间，超过后自动关阀，该样本无效。

calibrationDetailCaptureSec
范围：5-60 秒
默认：20 秒
说明：保存原始脉冲明细的最长时长，只用于启动阶段识别。

calibrationDetailPulseLimit
范围：100-5000
默认：2000
说明：保存原始脉冲明细的最大条数，达到后停止保存明细，但总脉冲继续统计。
```

稳定识别算法参数先作为固定默认值，不开放普通配置：

```text
stableWindowMs = 2000
stableStepMs = 200
stableConsecutiveWindows = 5
stableRateTolerancePercent = 10
minStableStartMs = 1000
```

## 单次样本采集

校准模式每次出水记录：

```text
zoneId
startedMs
endedMs
durationMs
totalPulses
actualMl
detailPulseDeltas[]
detailCapturedPulses
detailCaptureEndedReason
valid
invalidReason
```

`totalPulses` 是全程总脉冲，必须完整统计。`detailPulseDeltas[]` 是启动阶段原始脉冲时间差，单位 ms，用于还原启动阶段脉冲时间线。明细保存到 `calibrationDetailCaptureSec` 或 `calibrationDetailPulseLimit` 任一上限为止，之后只继续统计总脉冲。

有效样本条件：

```text
actualMl > 0
totalPulses > 0
durationMs <= calibrationMaxCaptureMin * 60 * 1000
停止原因是用户停止
没有水流异常、漏水保护、恢复出厂保护
```

超过最大采集时间的样本自动关阀并标记无效，不参与计算。

## 稳定点识别

系统从 `detailPulseDeltas[]` 还原：

```text
pulseTimes = [t1, t2, t3, ...]
```

然后用滑动窗口计算脉冲频率：

```text
窗口长度：2000 ms
窗口步进：200 ms
rate = 窗口内脉冲数 / 2 秒
```

找到第一组连续稳定窗口：

```text
连续 5 个窗口都有脉冲
max(rate) - min(rate) <= avg(rate) * 10%
窗口起点 >= 1000 ms
```

识别结果：

```text
stableStartMs
startupPulseInSample = stableStartMs 前累计脉冲数
stableRatePerMinuteX1000
rateVariationPermille
```

如果样本明细内找不到稳定点，该样本仍可保存，但不参与 `startupPulseLimit` 初始估计。

## 多样本拟合

页面允许用当前所有有效样本计算推荐参数；1 条有效样本也可以计算，但不能做误差交叉验证。

计算流程：

```text
1. 收集所有成功识别稳定点的样本 startupPulseInSample。
2. 取中位数作为 startupPulseLimit 初始值。
3. 在初始值附近搜索候选值，范围为 ±30%。
4. 对每个候选 startupPulseLimit，用所有有效样本拟合 startupEstimatedMl 和 stablePulsePerLiter。
5. 选择平均误差和最大误差最小的一组参数。
```

候选值确定后，样本模型为：

```text
actualMl = a * min(totalPulses, candidate) / candidate
         + b * max(0, totalPulses - candidate)

a = startupEstimatedMl
b = 每个稳定脉冲对应的 ml
stablePulsePerLiter = 1000 / b
```

推荐结果不按样本数量直接评级，而展示客观诊断指标：

```text
有效样本数
稳定点识别数量
水量跨度
脉冲跨度
样本内平均误差
样本内最大误差
建议
```

当只有 1 个有效样本时，误差评估显示“无法评估”，页面提示单条样本只能生成单点估计，建议补采不同水量样本。

## 校准页面流程

导航增加“流量校准”。页面流程：

```text
1. 选择水路。
2. 点击开始接水，系统打开该路阀门并开始记录。
3. 用户接水到量杯或容器。
4. 点击停止，系统关闭阀门。
5. 用户输入实际水量 ml 并保存本次样本。
6. 重复采集到需要的样本数量，最多不超过样本容量。
7. 点击计算推荐参数。
8. 页面展示推荐参数、每次样本误差、平均误差、最大误差和客观诊断建议。
9. 用户确认后应用到该水路配置。
```

页面应展示最近样本图表：

```text
X 轴：时间秒
折线 1：滑动窗口脉冲频率（覆盖本次有效样本的完整采集时长）
折线 2：累计脉冲数
竖线：stableStartMs
左侧阴影：启动阶段
```

图表数据保存在 RAM 中，样本清空或设备重启后消失。`detailPulseDeltas[]` 按明细脉冲上限保存，滑动窗口按实际采集时长保存，每 200 ms 一个点。

## 可靠性边界

- 校准模式同一时间只允许一个水路出水。
- 进入校准采集前必须确认没有普通浇水任务正在运行。
- 校准采集期间漏水检测不参与判断。
- 任意超时、停止全部、恢复出厂、系统维护操作都必须默认关阀。
- 样本默认保存在 RAM 中；断电或重启后需要重新采集。
- 只有用户确认“应用推荐参数”后，才写入水路配置。
