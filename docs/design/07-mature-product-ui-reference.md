# 成熟灌溉控制器 Web/App 交互参考

## 目标

本文记录对成熟灌溉控制器产品和开源控制器的界面/交互调研结果，用于修正本项目 Web 原型。本文不是功能扩展清单；所有结论都必须服从本项目既定模型：

```text
1 个必配流量计
最多 6 路水路
任意时刻只运行 1 路
自动计划按时间停止
水量只用于观察、记录和异常判断
天气暂停只影响自动计划是否执行，不按天气自动调节水量或时长
```

## 参考对象

本轮主要参考：

```text
B-hyve / Orbit
Rachio
Rain Bird
OpenSprinkler
Hunter Hydrawise
```

主要资料：

```text
B-hyve App Guide
https://support.orbitonline.com/b-hyve-smart-indoor-irrigation-controller/B-hyve-App-Guide-cae

B-hyve WeatherSense Weather Delays
https://support.orbitonline.com/b-hyve-smart-indoor-outdoor-irrigation-controller/Smart-Watering-VS-WeatherSense-Weather-Delays-6f6

B-hyve Rain Delay
https://support.orbitonline.com/b-hyve-smart-indoor-irrigation-controller/How-To-Create-a-Rain-Delay-from-the-B-hyve-app-dab9

Rain Bird 2.0 Mobile App Quick Start Guide
https://wifi.rainbird.com/quick-start-guide-rain-bird-2-0-mobile-app/

Rain Bird ESP manual
https://www.rainbird.com/sites/default/files/media/documents/2018-02/man_ESP.pdf

OpenSprinkler User Manual 2.2.1
https://raysfiles.com/os_compiled_firmware/docs/2.2.1/OSUserManual221_1.pdf

OpenSprinkler App repository
https://github.com/OpenSprinkler/OpenSprinkler-App

Rachio Quick Start Guide
https://support.rachio.com/en_us/rachio-app-quick-start-guide-HJ_mNilgK

Rachio Irrigation Schedule Overview
https://support.rachio.com/en_us/rachio-irrigation-schedule-overview-HJrFP8yYD

Rachio Flow Meter Settings, Calibration, and Flow Alerts
https://support.rachio.com/en_us/flow-meter-settings-calibration-and-flow-alerts-HyaxZc04x

Rachio Weather Intelligence
https://support.rachio.com/en_us/rachio-weather-intelligence-B153wIJKD

Hydrawise downloads and flow meter documentation
https://www.hydrawise.com/content/downloads
https://www.hunterirrigation.com/support/hydrawise-app-custom-sensormeter

Hydrawise dashboard/weather example
https://www.hydrawise.com/content/use-water-wisely
```

## 共性设计模式

### 1. 首页是设备驾驶舱，不是配置页

成熟产品首页通常优先显示：

```text
设备在线/离线
当前是否正在浇水
下一次计划
天气或雨延迟状态
手动浇水入口
雨延迟/暂停入口
```

B-hyve 的首页明确展示设备在线状态、当天预报和下一个计划，并把 Rain Delay 和 Water Manually 作为高频入口。Rain Bird App 也把控制器状态、手动延迟浇水和手动浇水作为容易触达的操作。

对本项目的启示：

```text
总览页应保留：
  当前运行
  自动浇水总控
  下一次计划
  天气参考
  故障/锁定摘要
  手动浇水入口

总览页不应放：
  每路详细配置
  全部高级参数
  校准表单
  复杂计划编辑
```

### 2. 手动浇水常见有两类入口

成熟产品一般同时支持：

```text
单站/单水路快速运行
一次性程序/全部水路按顺序运行
```

OpenSprinkler 的 Run-Once Program 可以从既有程序加载各站时长，也可以手工调整每个站的运行时间；如果控制器正在运行，会先提示停止当前程序。B-hyve 语音和 App 资料中也有“运行全部区域/运行某个区域/运行某个程序”的概念。

对本项目的启示：

```text
手动页应以“手动顺序浇水”为主：
  列出已启用且未锁定水路
  每路填写本次运行分钟
  0 表示跳过
  点击开始后按水路编号依次运行
  运行中仍然一次只打开一路

可以考虑保留“快速只浇一路”的轻量入口，但它本质上也可看作只有一路时长大于 0 的手动顺序任务。
```

当前原型已改为手动顺序浇水，方向符合成熟产品的 Run-Once / Manual Program 思路。

### 3. 计划模型通常是“开始时间 + 每站时长”

Rain Bird ESP 手册使用 Program A/B、开始时间和每站运行时间；未使用的站运行时间设为 0。OpenSprinkler 也以 program / station water time 组织。Rachio 和 B-hyve 在智能模式上更复杂，但用户仍然围绕 zones、schedules、start times、duration 理解浇水。

对本项目的启示：

```text
计划列表页只展示摘要：
  名称
  启用状态
  开始时间
  周期
  哪些水路会运行
  下一次执行

计划编辑页编辑一个计划组：
  最多 4 个开始时间
  周期表达：每天 / 每周 / 自定义周期
  每路分钟数
  0 表示跳过
```

这与本项目当前计划模型一致。

### 4. 雨延迟/暂停是首页高频操作，但语义必须明确

B-hyve 和 Rain Bird 都把 Rain Delay 放在显眼位置。Rain Bird 控制器手册中的 Delay Watering 会显示剩余天数，并在延迟结束后恢复自动浇水。OpenSprinkler 社区说明中，雨延迟期间的计划直接跳过，不做复杂缓存和补跑。

对本项目的启示：

```text
自动浇水总控应放在首页：
  开启
  关闭
  暂停到指定时间

暂停期间：
  到点计划跳过
  不补跑
  不为每次跳过刷事件
```

这与当前 `disabled_until` 模型一致。

### 5. 天气暂停是常见能力，但范围要收窄

B-hyve 和 Rachio 都有天气驱动或天气延迟能力。它们会把 weather / WeatherSense / Smart Watering 作为独立能力开关，并明确区别 weather delay 和 smart watering program。Rain Bird 和 B-hyve 也把雨延迟作为高频入口。

结合成熟产品和当前产品诉求，本项目可以把天气能力收窄为“天气暂停自动计划”：

对本项目的启示：

```text
天气卡片应明确：
  当前是否触发天气暂停
  暂停原因：降雨概率 / 预计降雨量 / 强风 / 低温
  哪个自动计划会跳过
  什么时候恢复
  手动浇水不受影响

天气暂停只允许：
  自动计划触发前做天气复核
  达到阈值时跳过本次自动计划
  在短期窗口内暂停后续自动计划
  用户允许本次计划执行
  用户手动延长自动暂停
  用户清除当前天气暂停
  用户关闭天气自动暂停功能

不要出现：
  智能天气调节
  自动减少水量
  自动增加水量
  按天气改变每路运行时长
  天气触发后补跑错过计划
```

当前原型应从“天气参考”升级为“天气暂停”，但仍不能扩展到智能天气浇水或按天气调节水量。

#### 成熟产品规则对比

调研到的成熟做法可以分成三类：

```text
简单雨延迟：
  Rain Bird 允许启用 Forecasted Rain Delay，按降雨概率、雨量阈值和延迟天数决定是否暂停。
  暂停到期后恢复常规计划。

计划前天气跳过：
  Rachio Weather Intelligence 会在计划运行前复核天气，用观察雨量和预报雨量判断是否跳过。
  Hydrawise Water Triggers 支持“降雨概率高于阈值不浇水”“风速高于阈值不浇水”，第二天继续正常检查。
  B-hyve WeatherSense 把降雨、强风、低温作为天气延迟条件，并且可独立于 Smart Watering 启用。

智能调节水量：
  Hydrawise、OpenSprinkler、RainMachine 等可以按温度、湿度、降雨或 ETo 调整运行时长。
  这类能力需要植物、土壤、喷头、日照、区域耗水等模型，不适合本项目第一阶段。
```

因此，本项目采用“计划前天气跳过 + 短期自动暂停”的组合，而不是 ET 智能浇水。

#### 本项目建议策略

```text
1. 天气自动暂停是自动计划的 gate，不是计划生成器。
2. 手动浇水不受天气自动暂停影响。
3. 每个自动计划开始前，应使用最新可用天气数据做一次复核。
4. 如果设备无法获取新天气，允许使用未过期缓存；缓存过期时不因为未知天气而跳过计划，只显示天气不可用。
5. 达到天气阈值时，跳过本次计划，并设置 weatherPauseUntil 到下一个复核时间或最少延迟窗口。
6. 暂停期间到点计划跳过，不补跑，不移动到后面执行。
7. 复核时如果天气低于阈值，清除天气暂停；如果仍达到阈值，继续暂停到下一次复核。
8. 用户可以允许下一次计划执行，但不关闭后续天气自动暂停。
9. 用户可以关闭天气自动暂停；关闭后只保留天气展示，不自动跳过计划。
```

默认阈值建议：

```text
预报判断窗口：未来 24 小时为主，首页可同时展示未来 3 天摘要
复核时间：每天 08:00
计划前复核：计划开始前 60 分钟；若实现允许，也可在开始前再做一次轻量复核
降雨触发：预计雨量 >= 5 mm，或降雨概率 >= 70% 且预计雨量 >= 2 mm
强风触发：默认关闭；启用后建议 10 m/s
低温触发：默认关闭；启用后建议 3°C
天气缓存有效期：建议 6 小时；过期后标记天气不可用
```

首页天气展示粒度：

```text
首页展示未来 3 天摘要：
  今天
  明天
  后天
  每天展示降雨概率和预计雨量

天气暂停判断使用配置的预报判断窗口：
  降雨概率阈值
  预计降雨量阈值
  强风阈值
  低温阈值

更长周期天气预报不放首页：
  避免挤占当前运行、下一次计划和流速趋势
  后续如需要，可放入天气详情页或外部天气服务说明
```

依据：

```text
Hydrawise 的仪表盘示例突出当前天气、本地降雨和 3 天预报。
Rachio Weather Intelligence 的雨跳过规则围绕未来窗口内的降雨阈值判断。
B-hyve WeatherSense 把降雨、强风、低温作为可调天气延迟条件。
```

因此，本项目原型采用：

```text
首页：
  未来 3 天天气摘要
  当前天气暂停原因
  影响的下一次计划
  下次复核时间

设置页：
  常用项只保留启用开关、判断窗口、复核时间、降雨概率、预计雨量
  强风和低温放入“高级天气保护”
```

### 6. Zone / 水路管理应是列表摘要 + 单项详情

成熟产品一般把 zone 作为一个可命名对象，详情页包含植物、土壤、喷头、智能参数或启用状态等。Rachio 的 zone setup 会收集 plant type、soil、sun exposure、sprinkler type 等，用于智能计划。

本项目不做智能 ET 模型，所以不需要植物/土壤/日照配置。但成熟产品的页面结构可以借鉴：

```text
列表页：
  名称
  启用状态
  锁定/故障状态
  最近运行
  流量基准状态

编辑页：
  名称
  启用/禁用
  默认手动时长
  低/高流量阈值
  异常确认时间
```

### 7. 流量相关通常拆成“传感器配置”和“报告/告警”

Rachio 有 flow meter settings、calibration、flow alerts。Hydrawise 资料中也把 pulse flow meter 的配置、读数和 dashboard reports 分开；其自定义传感器资料强调第三方流量计需要是脉冲型或 reed switch，并会在 dashboard reports 中反映流量数据。

对本项目的启示：

```text
流量页必须拆清楚：

流量计校准：
  全局 K-factor
  选择一条水路只是为了放水
  多样本
  输入实际接水量
  计算每升脉冲数

水路标准流速：
  每一路单独测定
  用于低/高流量判断
  不改变全局流量计校准

流量报告：
  当前运行流速
  最近运行趋势
  记录详情中的水量/平均流速
```

当前原型中“校准放水通道”的文案是正确方向。

### 8. 设置项要分层，默认隐藏高级风险

成熟产品通常把设备、账户、网络、天气、传感器、区域、计划拆成不同入口。高级参数不会和首页混在一起。

对本项目的启示：

```text
设置页应分组：
  自动和外设
  故障策略
  流量检测参数
  阀门保持参数

每个数值项必须显示单位：
  秒
  毫秒
  分钟
  百分比
  赫兹

每个高风险项必须有解释：
  影响什么
  什么时候生效
  是否需要实机验证
```

当前原型已补单位说明，但后续可以继续优化为折叠高级区，避免普通用户误改。

## 对当前原型的调整建议

### 应立即保留的方向

```text
1. 首页显示天气暂停状态，明确只影响自动计划，不影响手动浇水。
2. 首页显示当前运行概况和来源。
3. 手动页改为手动顺序浇水，支持每路分钟数。
4. 流量页拆成“流量计校准”和“水路标准流速”。
5. 设置页按组分层，并给数值项加单位说明。
```

### 还需要继续打磨

```text
1. 首页当前运行卡片应显示“来源：手动顺序浇水 / 自动计划 / 维护”。
2. 手动页可增加“从计划载入时长”的入口，借鉴 OpenSprinkler Run-Once，但第一阶段可先不实现。
3. 手动页可增加“快速测试 1 分钟”的维护入口，但要确认是否会扩大功能范围。
4. 设置页高级参数建议折叠，默认只展开常用项。
5. 流量页建议用分步向导方式表达校准流程，减少误操作。
6. 事件页可以把技术 reason 映射为用户语言，详情里再保留内部 reason。
```

### 不建议照搬的成熟产品能力

```text
1. 不照搬智能天气调节水量或运行时长。
2. 不照搬多水路并发或复杂分组并行。
3. 不照搬按水量停止。
4. 不照搬植物/土壤/日照智能模型。
5. 不照搬云账户、语音助手和多设备账号体系。
```

## 结论

成熟产品的共同点不是页面炫，而是把高频操作和安全状态放在首页，把计划、区域、传感器、记录和高级设置分层管理。

本项目应该采用以下产品骨架：

```text
总览：
  当前运行、下一次计划、自动总控、未来 3 天天气摘要、流速趋势、最近浇水
  故障/锁定只在存在时以醒目横幅提醒，不占常态固定卡片

手动：
  手动顺序浇水，按水路填写分钟数，0 跳过，二次确认启动

水路：
  列表摘要 + 单路编辑

计划：
  计划列表摘要 + 单计划组编辑

流量：
  全局流量计校准 + 每路标准流速 + 样本/偏差提示

设置：
  分组配置 + 单位 + 小字说明 + 高风险提示

记录/事件：
  浇水记录长期保存，事件只记录故障、警告和重要状态变化
```

当前 Web 原型整体方向已经接近成熟产品的信息架构，但后续仍应继续优化设置页折叠、高级参数保护、流量校准向导和运行中状态表达。
