# Web 原型设计

## 目标

本文定义第一阶段 Web 原型的信息架构、页面内容、操作流和 API 草案。它是设计文档，不是正式前端或业务代码。

第一阶段 Web/API 需要覆盖：

```text
总览
手动浇水
Zone 管理
计划管理
流量计校准和 Zone 正常流量
天气自动跳过计划
系统设置
浇水记录
业务事件
故障/锁定清除
```

Web 原型必须保持工具型、清晰、稳定，面向设备管理而不是营销展示。首页只展示状态和必要操作，不堆叠所有配置。

本原型采用三层信息复杂度：

```text
日常使用层：
  总览、手动浇水、计划、记录。
  只回答“现在怎样、下一次怎样、我要不要操作”。

设备维护层：
  水路管理、流量计校准、水路标准流速学习、水源与外设状态。
  使用用户能理解的维护语言，不把内部参数作为主视觉。

高级参数层：
  天气判断时机、流量检测窗口、阀门 PWM、故障策略细项。
  放在独立高级区域，不嵌套在普通分组里。
```

页面不得把 `pulsesPerLiter`、`normalFlowMlPerMin`、百分比阈值、确认时间等内部参数作为普通用户的主要操作对象。需要显示时，应同时给出用户语言解释，例如“已校准”“已学习标准流速”“偏高停机线”。
原型页面不使用折叠组件隐藏信息；需要展示的字段直接展示，需要降权的信息放到独立次级卡片。

## 复审结论

现有 `01-system-model.md` 到 `05-software-architecture-and-api.md`、`hardware/01-board-interfaces.md` 的核心模型一致，可以直接作为 Web 原型边界：

```text
1 个必配流量计
最多 6 路 Zone
任意时刻只允许 1 路 Zone 运行
可选泵启动输出，默认关闭
可选低液位输入，默认关闭
浇水只按时间停止，水量只用于观察、记录和异常判断
自动计划最多 4 个计划组，每组最多 4 个开始时间
自动计划和普通手动浇水必须在流量计校准后才能运行
```

本轮 Web 原型不推翻这些约束，不增加以下能力：

```text
Stop All
软件急停
暂停/恢复
多个 Zone 同时运行
按水量浇水
按天气自动调节水量或运行时长
天气触发后补跑错过计划
第一阶段本地按键/屏幕操作
```

天气能力仅限“天气自动跳过计划”：系统定时更新天气，并在自动计划开始前判断是否跳过本次计划。它只影响单次自动计划，不改变自动总控状态，不影响手动浇水，不改变每路运行时长。

需要补齐或在后续实现前明确的缺口：

```text
1. Web 鉴权、同源校验和 CSRF token 的具体接口由 Esp32BaseWeb 提供，本文只定义业务要求。
2. 事件 reason 枚举需要在实现计划中固化，避免页面、API 和记录导出命名不一致。
3. disabled_until 的时间输入需要依赖设备可信时间；系统时间无效时应禁止设置“暂停到指定时间”。
4. 维护运行中的“停止校准/停止学习”和普通“停止当前运行”语义不同，需要独立接口和页面提示。
5. CSV 字段顺序应在记录存储结构确定后冻结，避免后续外部系统解析不稳定。
```

## 全局 Web 规则

页面结构：

```text
/                 总览页
/run              手动浇水页
/zones            Zone 列表页
/zones/edit       Zone 编辑页
/plans            计划组列表页
/plans/edit       计划组编辑页
/flow             流量页
/settings         设置页
/records          浇水记录列表页
/records/detail   浇水记录详情页
/events           业务事件页
```

导航建议：

```text
总览
手动
Zone
计划
流量
设置
记录
事件
```

页面原则：

```text
GET 只展示，不改变状态。
所有改变状态的操作必须使用 POST。
POST 必须鉴权、同源检查，并携带确认字段或确认 token。
页面表单输出必须 HTML escape。
列表页只展示摘要和入口，不铺开多条复杂编辑表单。
编辑页一次只编辑一个对象。
运行类、清锁类、自动总控类、硬件参数类操作必须显示二次确认摘要。
```

二次确认建议分级：

```text
普通保存：
  点击保存后弹出确认，POST 携带 confirm=true。

运行/停止/清除锁定/启用自动浇水/修改阀门参数：
  弹出确认框或确认页面，列出将改变的对象、风险和结果，确认后 POST。

维护校准保存：
  先展示样本、偏差和建议值，用户确认应用后 POST 保存。
```

统一状态标签：

```text
Idle：空闲
Starting：启动中
WaitingForFirstPulse：等待出水
FlowStabilizing：水流稳定中
Running：运行中
Stopping：停止中
FaultStopping：故障停机中
```

统一错误提示：

```text
系统正忙，当前已有 Zone 运行
目标 Zone 未启用
目标 Zone 已锁定
存在全局故障锁定
流量计尚未校准
低液位输入处于低液位状态
durationMin 超出 1..360 分钟
计划运行时长超出 0..360 分钟
系统时间无效，无法启用自动计划或设置暂停到指定时间
确认字段缺失，操作未执行
```

## 信息架构

### 总览层

总览页回答三个问题：

```text
设备现在安全吗
现在有没有在浇水
下一步最可能要去哪里操作
```

总览页只放状态卡片和入口，不放复杂配置表单。

### 操作层

操作层包含：

```text
手动浇水
停止当前运行
故障/锁定清除入口
自动浇水总控
```

所有操作都必须经过 Runtime 或配置服务，不允许页面直接控制阀门、泵或底层 GPIO。

### 配置层

配置层包含：

```text
Zone 配置
计划组配置
流量计参数
故障策略
泵和低液位启用
流速窗口和阀门保持参数
```

Zone 列表和编辑页分离；计划列表和编辑页分离。

### 记录层

记录层包含：

```text
浇水记录列表
浇水记录详情
CSV 导出
业务事件筛选
```

浇水记录是长期历史，事件日志是近期故障、警告和状态变化窗口。不要用事件重复记录正常计划开始和完成。

## 页面设计

## 1. 总览页 `/`

### 内容

顶部状态区：

```text
当前运行状态
当前 Zone 名称和编号
来源：手动 / 自动计划 / 维护
当前阶段：等待出水 / 水流稳定中 / 运行中等
已运行时间 / 目标时长
累计水量
实时流速
```

自动浇水总控：

```text
当前 autoMode：enabled / disabled / disabled_until
disabled_until 时显示 autoResumeAt
系统时间无效时显示“自动计划不可用”
```

故障/锁定摘要：

```text
全局锁定原因
被锁定 Zone 数量和列表
最近一条 fault / warning 事件
清除入口
```

今日/最近浇水摘要：

```text
今日已完成 Zone 次数
今日估算总水量
今日总运行分钟数
最近一次浇水：Zone、停止原因、运行时长、水量
下一次计划：计划组、开始时间、将运行的 Zone 摘要
等待队列：仅显示自动计划队列数量和最早一条
```

快速入口：

```text
手动浇水
编辑 Zone
编辑计划
流量校准
查看记录
查看事件
```

### 操作流

自动浇水总控：

```text
1. 用户点击开启、关闭或暂停到指定时间。
2. 页面显示确认摘要。
3. POST /api/auto。
4. 成功后回到总览并显示当前 autoMode。
```

停止当前运行：

```text
1. 仅当前有普通手动或自动运行时显示“停止当前运行”。
2. 用户点击后显示二次确认。
3. 确认文案说明：会关闭泵和当前 Zone，取消当前自动计划剩余 Zone，并清空等待队列。
4. POST /api/manual/stop，confirm=true。
5. 成功后显示停止结果和记录入口。
```

总览页不提供 Stop All，不提供暂停/恢复。

## 2. 手动浇水页 `/run`

### 内容

当前运行面板：

```text
状态机状态
当前 Zone
来源
开始时间
目标时长
已运行分钟和百分比
累计脉冲
估算水量
实时流速
当前告警
```

手动启动表单：

```text
Zone 选择：只显示 enabled Zone
durationMin：1..360 分钟
默认值：选中 Zone 的 defaultManualDurationMin
```

Zone 选择项需要显示：

```text
Zone 名称
是否锁定
normalFlowMlPerMin 是否已设置
```

被锁定 Zone 不应可启动；可以显示但置灰并给出原因。禁用 Zone 不显示在手动选择列表。

### 操作流

启动当前 Zone：

```text
1. 用户选择 enabled 且未锁定 Zone。
2. 输入 durationMin。
3. 点击启动。
4. 页面显示确认摘要：Zone、时长、泵是否启用、低液位是否启用、是否已校准流量计。
5. POST /api/manual/start，confirm=true。
6. 启动成功后进入当前运行面板。
```

停止当前运行：

```text
1. 运行中显示“停止当前运行”。
2. 点击后必须二次确认。
3. POST /api/manual/stop，confirm=true。
4. 返回 stopReason=user_stop 的结果。
```

启动拒绝时页面直接显示 Runtime 返回的明确原因，不做前端猜测。

## 3. Zone 管理

## 3.1 Zone 列表页 `/zones`

### 内容

列表列：

```text
Zone
名称
启用状态
锁定状态
默认手动时长
流量保护状态
最近运行摘要
操作
```

流量保护状态：

```text
未设置：normalFlowMlPerMin = 0
已学习：显示“已学习标准流速”，必要时可在详情中显示 L/min 和 normalFlowMeasuredAt
手工设置：显示“手工设置”，详情中显示 L/min
锁定中：显示锁定原因，不在列表中展开阈值
```

操作入口：

```text
编辑
测定正常流量
查看记录筛选
清除 Zone 锁定
```

### 操作流

清除 Zone 锁定：

```text
1. 仅锁定 Zone 显示清除入口。
2. 二次确认显示锁定原因和时间。
3. POST /api/fault/clear，scope=zone，zoneId=N，confirm=true。
4. 成功后写 fault_cleared 事件。
```

Zone 列表页不直接展开配置编辑表单。

## 3.2 Zone 编辑页 `/zones/edit?zoneId=N`

### 字段

```text
zoneId：只读
enabled：启用/禁用
name：显示名称
defaultManualDurationMin：1..360
normalFlowMlPerMin：只读摘要，编辑入口在流量页或本页跳转
normalFlowMeasuredAt：只读
锁定状态：只读摘要和清除入口
```

字段提示：

```text
禁用 Zone 不出现在手动选择列表，计划执行时会跳过。
normalFlowMlPerMin 为 0 时，低流量/高流量判断不启用，但无水检测仍工作。
页面显示按当前系统“流量异常判断”设置换算出的低流量线、高流量线和异常持续时间；这些值不在水路编辑页单独配置。
```

### 操作流

保存：

```text
1. 用户修改字段。
2. 页面做基础范围校验。
3. 点击保存后显示确认摘要。
4. POST /api/zone/save，confirm=true。
5. 保存成功后回到 Zone 列表页。
```

禁用 Zone 时不清除历史记录，不自动清除锁定；只影响后续启动和计划执行。

## 4. 计划管理

## 4.1 计划列表页 `/plans`

### 内容

最多显示 4 个计划组卡片或表格行。

摘要字段：

```text
计划组编号和名称
启用状态
开始时间摘要，最多 4 个
周期摘要：每天 / 每周 / 自定义周期
Zone 运行摘要：只列出 duration > 0 的 Zone
下一次预计触发时间
最近一次跳过原因或运行摘要
```

操作入口：

```text
编辑
启用/禁用
```

### 操作流

启用/禁用：

```text
1. 用户点击启用或禁用。
2. 页面显示计划摘要和影响。
3. POST /api/plan/save，confirm=true。
4. 成功后返回列表。
```

计划列表页不编辑开始时间、周期和 Zone 时长。

## 4.2 计划编辑页 `/plans/edit?planGroupId=N`

### 字段

基础字段：

```text
planGroupId：1..4，只读
enabled
name
```

开始时间：

```text
startTimes[4]
每项：启用状态 + HH:MM
未启用项不触发
```

周期 UI：

```text
每天：
  cycleLengthDays = 1
  activeDayMask = day 1
  cycleAnchorDate = 当前日期或用户指定日期

每周：
  cycleLengthDays = 7
  activeDayMask = 周一到周日的选择
  cycleAnchorDate = 某个周一，页面可自动换算但需显示

自定义周期：
  cycleLengthDays = 1..31
  activeDayMask = 周期内第 1..N 天选择
  cycleAnchorDate = 用户选择
```

Zone 时长：

```text
只显示 enabled Zone。
0 表示跳过
1..360 表示运行分钟数
disabled Zone 不显示在计划编辑页；硬件安装后未启用的水路不应反复出现在日常计划配置中。
```

计划编辑页布局原则：

```text
控件紧凑，不把单选项和时间输入铺满整行。
周期规则使用紧凑 segmented 控件：每天 / 每周 / 自定义周期。
每周运行日和自定义周期运行日使用 chip 形式，可多选，按内容宽度排列。
开始时间使用短输入组，最多 4 个。
已启用水路时长使用紧凑列表，每行一个水路和一个分钟输入。
```

保存前摘要：

```text
计划组名称
启用状态
开始时间
周期解释
将运行的 enabled Zone 和分钟数
被跳过 Zone 的原因：时长 0 或 Zone disabled
```

### 操作流

保存：

```text
1. 用户进入单个计划组编辑页。
2. 选择每天 / 每周 / 自定义周期。
3. 编辑开始时间和 Zone 运行时长。
4. 页面校验最多 4 个开始时间，时长范围 0..360。
5. 点击保存后二次确认。
6. POST /api/plan/save，confirm=true。
7. 成功后返回计划列表页。
```

页面不得提供按水量停止、多个 Zone 同时运行、暂停/恢复计划。

## 5. 流量页面组

流量页面组必须明确区分两件事：

```text
流量计校准：
  得到全局 pulsesPerLiter，决定累计水量和实时流速是否可信。

Zone 正常流量：
  得到某个 Zone 的 normalFlowMlPerMin，决定低流量/高流量判断是否可用。
```

### 5.1 流量入口页 `/flow`

入口页只做状态总览和任务分流，不承载完整维护流程，也不把校准样本和脉冲参数作为主视觉。

内容：

全局流量计状态：

```text
已校准 / 未校准 / 建议重新校准
当前每升脉冲数
最近维护时间
当前是否允许普通手动/自动浇水
```

参数详情区直接展示：

```text
当前 pulsesPerLiter
calibratedAt
calibrationSampleCount
calibrationTotalPulseCount
calibrationTotalActualMl
参数来源：校准样本 / 手动调整
```

水路标准流速状态：

```text
只展示 enabled Zone
已学习 / 未学习 / 锁定中
必要时显示 normalFlowMlPerMin，单位用 L/min
低/高流量阈值换算结果只在详情或高级区域显示
```

入口操作：

```text
进入流量计校准
进入水路标准流速学习
查看参数明细
导出校准记录
```

### 5.2 流量计校准页 `/flow/calibration`

校准页使用单独流程，不和 Zone 标准流速学习混在同一表单中。

流程内容：

```text
准备量桶
选择 enabled Zone 作为放水水路
开始校准维护运行
停止校准维护运行
输入实际接水量 actualMl
确认加入样本，或放弃本次记录
显示样本列表、偏差和加权合并建议
样本 actualMl 可编辑，样本行提供详情入口
确认保存全局 pulsesPerLiter
```

保护提示：

```text
校准的是唯一流量计，不是某一路水路；选择水路只是为了打开阀门放水。
维护运行最长 maintenanceMaxDurationSec 秒，超时会自动关闭泵和阀，本次样本失败。
建议使用至少 2L，推荐总样本水量不低于 5L。
样本偏差明显时不建议保存。
```

页面必须展示公式：

```text
pulsesPerLiter = totalPulseCount * 1000 / actualMl
mergedPulsesPerLiter = sum(totalPulseCount) * 1000 / sum(actualMl)
```

### 5.3 水路标准流速学习页 `/flow/baseline`

页面标题对用户使用“学习水路标准流速”或“水路正常流速学习”，内部仍保存 `normalFlowMlPerMin`。它要求全局流量计已经校准。

流程内容：

```text
选择 enabled 且未锁定 Zone
确认该水路当前管路和喷头状态正常
开始维护运行
等待首个有效脉冲
等待 flowStabilizeSec
稳定阶段采样，建议 30 秒
显示平均流速、最低流速、最高流速
确认保存为该水路标准流速
```

手工输入：

```text
手工输入 normalFlowMlPerMin 是次要入口，但在原型中直接展示。
适用于用户已用外部工具测得某路正常流速。
```

### 流量计校准操作流

```text
1. 用户选择 enabled Zone。
2. 页面确认当前操作是“流量计校准”，不是普通浇水。
3. POST /api/flow/calibration/start，confirm=true。
4. 维护运行开始后页面显示运行时长、累计脉冲、实时脉冲状态。
5. 用户停止接水。
6. POST /api/flow/calibration/stop，confirm=true。
7. 页面要求输入实际接水量 actualMl。
8. 用户选择“确认加入样本”或“放弃本次”。
9. 确认加入样本时，POST /api/flow/calibration/sample，confirm=true。
10. 放弃本次时，不写入样本列表，只关闭本次维护记录。
11. 页面显示样本列表、偏差和合并建议。
12. 样本列表允许修改 actualMl，修改后重新计算该样本每升脉冲数、偏差和合并建议。
13. 用户确认应用。
14. POST /api/flow/calibration/apply，confirm=true。
```

校准维护运行可以在 `pulsesPerLiter = 0` 时执行。

### Zone 正常流量测定操作流

```text
1. 用户选择 enabled Zone。
2. 系统要求 pulsesPerLiter 已校准。
3. 页面确认当前 Zone 管路和喷头处于正常状态。
4. POST /api/flow/zone-baseline/start，confirm=true。
5. 系统等待首脉冲，经过 flowStabilizeSec 后采样。
6. 页面显示平均流量、最小流量、最大流量和采样时长。
7. 用户确认保存。
8. POST /api/flow/zone-baseline/apply，confirm=true。
```

手工输入 normalFlowMlPerMin：

```text
1. 用户选择 Zone。
2. 输入 normalFlowMlPerMin。
3. 页面显示对应低/高流量阈值。
4. POST /api/flow/zone-baseline/manual，confirm=true。
```

## 6. 设置页 `/settings`

设置页按分组呈现，不把无关配置混在同一卡片里。

设置页分组：

```text
天气自动跳过计划
天气信息获取
水源与外设
计划执行
流量异常判断
故障策略
高级：流量检测参数
高级：阀门保持参数
```

所有分组都是同级卡片，不做卡片内折叠。页面级“保存设置”按钮放在设置页底部，不属于任何单一卡片。所有数字输入必须在标签中体现单位，并在说明中写出支持范围。

### 天气自动跳过计划

字段：

```text
weatherAutoSkipEnabled
rainProbabilityThresholdPercent：0..100 %
rainAmountThresholdMm：0..100 mm
```

### 天气信息获取

字段：

```text
weatherForecastWindowHours：1..72 小时
weatherReviewTimeLocal
weatherPreRunCheckMin：0..360 分钟
weatherCacheMaxAgeHours：1..24 小时
```

提示：

```text
天气自动跳过只影响单次自动计划；手动浇水仍可使用。
触发后本次计划跳过，不补跑，不关闭自动总控。
weatherReviewTimeLocal 是每天刷新天气状态的时间，不是某个浇水计划开始时间，也不代表到点一定恢复浇水。
weatherPreRunCheckMin 是计划开始前判断天气的提前量，默认 60 分钟。
weatherForecastWindowHours 是预计降雨判断窗口，默认看未来 24 小时累计降雨。
weatherCacheMaxAgeHours 是自动计划判断可使用的天气数据最大有效时间，默认 6 小时。
天气不可用或数据过期时，不因为未知天气跳过计划。
首页仍可显示最近一次天气和更新时间；超过判断有效期时标记为“天气数据已过期，仅供参考”。
降雨概率和预计降雨量同时达到阈值时，才跳过本次自动计划。
天气策略不会改变每个 Zone 的运行分钟数，也不会按天气增减水量。
```

操作：

```text
本次照常执行：只覆盖当前这一次天气跳过判断，后续计划仍按天气判断。
关闭天气自动跳过：关闭 weatherAutoSkipEnabled，不再由天气自动跳过计划。
```

首页天气卡只提供与当前天气跳过状态直接相关的操作，例如“本次照常执行”。天气规则修改属于设置页，不在首页重复放快捷跳转按钮。

默认策略：

```text
预报判断窗口：24 小时
首页展示：今天、明天、后天 3 天摘要
计划前天气判断：计划开始前 60 分钟
每天更新天气时间：08:00
计划判断数据有效期：6 小时
降雨触发：降雨概率 >= 70% 且预计雨量 >= 5 mm
```

天气更新时机：

```text
设备启动后，如果网络可用，应尝试拉取一次天气。
每天 weatherReviewTimeLocal 拉取天气，即使当天没有浇水计划也更新首页状态。
自动计划开始前 weatherPreRunCheckMin 分钟再次尝试拉取天气，用于本次是否跳过。
用户打开总览页时，如果天气数据较旧，可以触发一次后台刷新；刷新失败不阻塞页面。
```

预计雨量来源：

```text
优先使用天气服务的逐小时预报，累计未来 weatherForecastWindowHours 内的降雨量。
如果天气服务只提供按天预报，则使用今天/明天摘要近似，页面需标记为“按日预报估算”。
```

### 水源与外设

字段：

```text
pumpStartEnabled
lowLevelEnabled
```

提示：

```text
泵启动输出只控制外部泵启动回路，不是泵电源输出。
低液位启用后，低液位会阻止启动并触发运行中停机。
```

### 计划执行

字段：

```text
queuedPlanMaxDelayMin：0..360 分钟
```

提示：

```text
queuedPlanMaxDelayMin = 0 表示冲突时不排队，直接跳过。
任意时刻仍只允许一路水路运行；排队只是在当前运行结束后继续执行等待中的计划。
```

### 流量异常判断

字段：

```text
lowFlowPercent：1..100 %
highFlowPercent：101..500 %
flowFaultConfirmSec：1..300 秒
```

提示：

```text
这些是全局判断规则。
每一路仍使用自己的 normalFlowMlPerMin，因此会换算出不同的实际 L/min 阈值。
低/高流量必须持续超过 flowFaultConfirmSec 后才成立。
```

### 故障策略

字段：

```text
noWaterLockZone
highFlowAction：warn / stop
highFlowLockZone
lowFlowAction：warn / stop
lowFlowLockZone
```

默认展示方式：

```text
无水保护：一定停机，摘要显示是否锁定当前水路。
高流量保护：摘要显示“停机”或“只提醒”。
低流量保护：摘要显示“提醒”或“停机”。
```

具体策略开关放入“可调整策略”区域。普通用户不应在摘要区直接面对 `warn`、`stop`、`lockZone` 等内部枚举。

固定规则以只读提示显示：

```text
无水一定停机。
低液位一定停机但不锁 Zone。
待机漏水一定全局锁定。
流量计异常一定全局锁定。
```

### 流量检测参数

字段：

```text
flowSampleWindowSec：1..60 秒
flowUpdateIntervalMs：200..10000 毫秒
firstPulseTimeoutSec：1..300 秒
runningNoPulseTimeoutSec：1..300 秒
flowStabilizeSec：0..120 秒
maintenanceMaxDurationSec：60..600 秒
idleLeakConfirmSec：5..600 秒
```

提示：

```text
flowStabilizeSec 只屏蔽低/高流量判断，不做水量补偿。
maintenanceMaxDurationSec 用于流量计校准和 Zone 正常流量测定。
```

### 阀门保持参数

字段：

```text
valvePullInMs：100..10000 毫秒
valveHoldDutyPercent：1..100 %
valvePwmFrequencyHz：100..40000 Hz
```

提示：

```text
valveHoldDutyPercent = 100 表示禁用保持 PWM，阀门全程全功率输出。
这些参数是系统级参数，不按 Zone 单独设置。
修改后需要结合目标阀门做实机验证。
```

### 操作流

```text
1. 用户在设置页修改一组配置。
2. 页面做基础范围校验。
3. 点击保存后显示二次确认摘要。
4. POST /api/settings/save，confirm=true。
5. 成功后显示“已保存，部分参数将在下一次运行生效”。
```

阀门保持参数保存时必须额外提示：

```text
不合适的吸合/保持参数可能导致阀门打不开、运行中掉阀、噪声或发热异常。
```

## 7. 浇水记录和事件

## 7.1 浇水记录列表 `/records`

### 内容

筛选：

```text
时间范围
Zone
来源：manual / schedule
停止原因
```

列表列：

```text
开始时间
Zone
来源
计划组
目标时长
实际运行时长
估算水量
平均流量
最高流量
最低流量
停止原因
详情
```

导出：

```text
CSV 导出当前筛选
CSV 导出全部记录
```

### 操作流

CSV 导出使用 GET：

```text
GET /api/records.csv
```

导出不改变状态，可以 GET；仍应要求鉴权。

## 7.2 浇水记录详情 `/records/detail?id=N`

详情字段：

```text
recordId
zoneId
zoneNameSnapshot
source：manual / schedule
planGroupId
planNameSnapshot
scheduledAt
startedAt
endedAt
targetDurationMin
actualDurationSec
pulseCount
estimatedVolumeMl
averageFlowMlPerMin
maxFlowMlPerMin
minFlowMlPerMin
lastFlowMlPerMin
stopReason
faultCode
warningFlags
lowFlowWarningWritten
highFlowWarningWritten
userStop
queueExpiredRelated
```

记录详情只展示事实，不提供重跑、补跑、暂停恢复或按水量复制。

## 7.3 业务事件页 `/events`

### 内容

筛选：

```text
类型：fault / warning / state / maintenance
对象：system / auto / flow / zone:N / plan:N
reason
时间范围
```

列表列：

```text
时间
类型
对象
reason
摘要
value1..3
```

事件写入规则：

```text
记录故障、警告和重要状态变化。
不重复记录正常计划开始。
不重复记录正常计划完成。
同一次 Zone 运行中，同类 warning 事件最多写一次。
自动浇水 disabled 期间，不为每次计划跳过写事件。
```

可记录事件示例：

```text
auto_mode_changed
queued_plan_expired
no_water_stop
low_flow_warning
low_flow_stop
high_flow_warning
high_flow_stop
low_level_stop
idle_leak_lock
flow_meter_fault_lock
fault_cleared
calibration_applied
zone_baseline_saved
maintenance_timeout
system_restarted_during_run
```

## API 草案

API 用于 Web 页面和本地自动化。API 是原子操作，不提供复杂编排，不内置天气联动策略。

### 状态和配置

```text
GET  /api/status
GET  /api/config
GET  /api/zones
GET  /api/plans
GET  /api/settings
```

### 自动总控

```text
POST /api/auto

body:
  mode: enabled | disabled | disabled_until
  autoResumeAt: ISO 时间，仅 disabled_until 使用
  confirm: true
```

说明：

```text
自动总控只表示用户主动开启、关闭或人工暂停自动浇水。
天气自动跳过不应修改 autoMode，不应把天气原因写成 disabled_until。
天气达到阈值时，只跳过本次计划并记录 weather_skip 原因。
```

### 手动运行

```text
POST /api/manual/start

body:
  zoneId: 1..6
  durationMin: 1..360
  confirm: true
```

```text
POST /api/manual/stop

body:
  confirm: true
```

停止只停止当前运行，不存在 Stop All。

### Zone

```text
POST /api/zone/save

body:
  zoneId
  enabled
  name
  defaultManualDurationMin
  confirm: true
```

### 计划

```text
POST /api/plan/save

body:
  planGroupId
  enabled
  name
  startTimes[4]
  cycleLengthDays
  activeDayMask
  cycleAnchorDate
  zoneDurationsMin[6]
  confirm: true
```

### 流量计校准

```text
POST /api/flow/calibration/start
POST /api/flow/calibration/stop
POST /api/flow/calibration/sample
POST /api/flow/calibration/apply
POST /api/flow/calibration/discard
```

所有 POST 都需要 `confirm=true`。

### Zone 正常流量

```text
POST /api/flow/zone-baseline/start
POST /api/flow/zone-baseline/stop
POST /api/flow/zone-baseline/apply
POST /api/flow/zone-baseline/manual
```

### 设置

```text
POST /api/settings/save

body:
  pumpStartEnabled
  lowLevelEnabled
  queuedPlanMaxDelayMin
  lowFlowPercent
  highFlowPercent
  flowFaultConfirmSec
  noWaterLockZone
  highFlowAction
  highFlowLockZone
  lowFlowAction
  lowFlowLockZone
  flowSampleWindowSec
  flowUpdateIntervalMs
  firstPulseTimeoutSec
  runningNoPulseTimeoutSec
  flowStabilizeSec
  maintenanceMaxDurationSec
  idleLeakConfirmSec
  valvePullInMs
  valveHoldDutyPercent
  valvePwmFrequencyHz
  confirm: true
```

### 故障清除

```text
POST /api/fault/clear

body:
  scope: global | zone
  zoneId: 1..6，仅 scope=zone 使用
  confirm: true
```

清除故障只清除锁定状态，不删除浇水记录和事件。

### 记录和事件

```text
GET /api/records
GET /api/records.csv
GET /api/events
```

这些 GET 不改变状态，但仍需要鉴权。

## 原型状态和空状态

常见空状态：

```text
没有 enabled Zone：
  手动页提示先启用至少一个 Zone。

pulsesPerLiter = 0：
  总览和手动页提示先完成流量计校准。
  流量页突出显示校准入口。

normalFlowMlPerMin = 0：
  Zone 列表提示低/高流量判断未启用。
  不阻止手动或计划浇水。

没有计划组启用：
  总览提示自动浇水无可执行计划。

系统时间无效：
  自动计划相关页面提示需要等待 NTP 或基础库时间有效。

全局锁定：
  手动和自动启动都被阻止，只保留查看、配置和清锁入口。
```

## 第一阶段不做

```text
本地按键/屏幕操作页面设计
天气联动策略页面
按水量浇水
多 Zone 并发
Stop All
暂停/恢复
运行中编辑当前计划实例
运行中保存剩余进度
旧 API 或旧存储兼容
```

## 后续实现检查点

进入正式实现前，建议先确认：

```text
1. Esp32BaseWeb 的鉴权、同源检查、CSRF token 和表单 helper 能力。
2. API JSON 错误格式和页面错误展示格式。
3. 记录结构字段和 CSV 字段顺序。
4. 事件 reason 枚举。
5. 维护运行是否复用 Runtime 状态机，或在 Runtime 内作为独立 maintenance source。
6. disabled_until 的时间输入格式和系统时间无效时的降级行为。
```
