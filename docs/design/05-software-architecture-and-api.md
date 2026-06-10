# 软件架构、存储和接口边界

## 目标

本文件定义新系统的应用层模块边界、持久化方式、Web 页面和 API 入口。原则：

```text
应用层只做灌溉业务
基础能力优先使用 Esp32Base
运行控制保持单一所有者
实时 loop 不做阻塞文件、NVS 或 Web 操作
配置、记录、事件三类数据分开
页面和 API 只暴露必要操作
```

## Esp32Base 依赖边界

项目默认使用 `ESP32BASE_PROFILE_FULL`。应用层依赖以下基础能力：

```text
Esp32BaseConfig：小配置、POD 配置、故障锁定状态
Esp32BaseFs：浇水记录等业务二进制定长文件
Esp32BaseAppEventLog：轻量业务事件日志
Esp32BaseWeb：业务 Web 页面、API、CSV/JSON 导出
Esp32BaseNtp：计划调度所需的可信时间
Esp32BaseSystem：统一重启入口
Esp32BaseWatchdog / Health：由基础库承担运行健康和看门狗
```

应用层不直接 include `LittleFS.h`，不直接访问 Arduino `WebServer`，不直接调用底层 restart/deep sleep API。

## 模块划分

应用层建议按以下模块实现：

```text
IrrigationApp
  应用入口，初始化模块，注册 Web/API，驱动 service loop。

IrrigationConfig
  读取、保存和校验系统配置、Zone 配置、计划组、流量计参数和故障策略。

IrrigationHardware
  GPIO、PWM、流量计脉冲输入、低液位输入、阀门输出、泵启动输出的薄封装。

FlowMeterService
  维护脉冲计数、首脉冲时间、滑动窗口流速、待机漏水检测输入。

IrrigationRuntime
  唯一运行状态机。负责启动校验、串行运行、停止、故障停机、输出关闭和运行记录生成。

ScheduleService
  每分钟检查自动计划，生成计划实例，维护自动计划等待队列。

RecordsStore
  浇水记录定长二进制环形文件，支持追加、分页读取、CSV 导出。

EventService
  对 Esp32BaseAppEventLog 的业务封装，只写故障、警告和重要状态变化。

IrrigationWeb
  Web 页面和表单处理。

IrrigationApi
  JSON API 和导出接口。

LocalUi
  本地按键和屏幕的轻量交互。只做选择、启动、停止和状态显示。
```

## 运行控制所有权

`IrrigationRuntime` 是泵和阀门输出的唯一业务所有者。

```text
Web/API/本地按键/计划调度不能直接开阀、关阀或开泵
它们只能提交 start/stop/config 请求
Runtime 统一执行启动校验、状态转换、输出控制和记录落盘
故障停机和用户停止共用同一套输出关闭路径
```

这样可以避免“页面操作开了阀、计划调度又以为 Idle”的状态分裂。

## 中断和 loop 规则

流量计脉冲输入使用中断或硬件计数能力时，中断侧只做最小工作：

```text
增加脉冲计数
记录最近脉冲时间
必要时设置轻量标志
```

中断中禁止：

```text
写 NVS
写文件
调用 Web/API
分配动态内存
做复杂浮点计算
写事件日志
```

主 loop 中按短周期 service：

```text
FlowMeterService::handle()
IrrigationRuntime::handle()
ScheduleService::handle()
RecordsStore::handle()
Esp32Base::handle()
```

文件、NVS 和事件日志写入由非中断上下文完成。长响应和导出使用 `Esp32BaseWeb` 分块输出。

## 持久化模型

### NVS 配置

小配置使用 `Esp32BaseConfig`。业务 namespace 不使用 `eb_` 前缀，且控制在 15 字符以内。

建议 namespace：

```text
irr_sys：自动浇水状态、自吸泵启用、低液位启用、排队超时
irr_zone：6 路 Zone 配置
irr_plan：4 个计划组配置
irr_flow：流量计参数、流速窗口、首脉冲超时、稳定等待、阀门 PWM 参数
irr_fault：故障策略和持久化锁定状态
irr_store：浇水记录文件元数据
```

配置结构体必须是固定布局 POD，并带 `version` 字段。未来结构变化由应用层自己迁移，不能依赖基础库解释 blob 内容。

### 浇水记录

浇水记录是长期业务主记录，使用应用自己的定长二进制环形文件。

建议路径：

```text
/irrigation/records.bin
```

记录文件使用 `Esp32BaseFs::createFixedFile()` 预分配，使用 `readBytesAt()` / `writeBytesAt()` 按槽位读写。元数据保存在 `irr_store`：

```text
recordHead
recordCount
recordNextId
recordCapacity
recordSize
```

记录追加必须是低频操作，只在一次 Zone 实际开阀运行结束后写入。计划到点但未开阀，不生成浇水记录。

### 轻量事件日志

事件日志使用 `Esp32BaseAppEventLog`。只记录业务层故障、警告和重要状态变化，不重复写基础库系统诊断事件。

`Esp32BaseAppEventLog` 的容量由基础库编译期宏控制。本项目建议在构建参数中设置：

```text
ESP32BASE_APP_EVENT_LOG_CAPACITY = 512
```

基础库允许范围为 64..2048；若未来需要现场 Web 配置容量，不在本项目里另写事件日志，应转交 `Esp32Base` 增加公开能力。

事件字段映射建议：

```text
source = irrigation
type = fault / warning / state / maintenance
reason = no_water / high_flow / low_flow / low_level / idle_leak / auto_disabled / fault_cleared ...
object = zone:1 / plan:2 / flow / auto / system
value1..3 = 流速、水量、持续秒数、计划 id 等短数值
text = 短说明，可选
```

事件日志是近期窗口，不作为长期报表。浇水执行历史以浇水记录为准。

## Web 页面

Web 页面采用“列表页和编辑页分离”的结构。

建议页面：

```text
/                 应用首页或灌溉总览
/zones            Zone 列表：启用状态、锁定状态、正常流量状态、编辑入口
/zones/edit       单个 Zone 编辑页
/plans            计划组列表
/plans/edit       单个计划组编辑页
/run              手动运行页：选择 enabled Zone、时长、启动/停止当前运行
/flow             流量计校准和正常流量测定入口
/settings         系统设置：自动浇水、自吸泵、低液位、故障策略、阀门参数
/records          浇水记录列表
/events           业务事件入口可链接到基础库 App Events，或做轻量业务筛选页
```

页面规则：

```text
GET 只展示
改变状态必须 POST
危险操作必须 Basic Auth、同源校验和 JavaScript confirm
编辑页处理完当前对象后 sendFooter() 并 return
列表页不铺开多条复杂表单
用户输入和配置输出必须 HTML escape
```

## API 入口

API 面向本地自动化和未来外部系统，不承载复杂业务编排。建议提供原子接口：

```text
GET  /api/status
GET  /api/config
POST /api/auto
POST /api/manual/start
POST /api/manual/stop
POST /api/fault/clear
POST /api/zone/save
POST /api/plan/save
POST /api/flow/calibrate
POST /api/flow/measure-zone
GET  /api/records
GET  /api/records.csv
GET  /api/events
```

接口原则：

```text
GET 不改变状态
POST 统一走鉴权和同源检查
启动接口只接受一个 Zone 和一个时长
停止接口只停止当前运行，不存在 Stop All
自动计划状态只支持 enabled / disabled / disabled_until
外部系统可以通过原子 API 做天气联动，但控制器核心不内置天气逻辑
```

## 出厂重置和维护

完整整机出厂重置由应用层先清理业务数据，再调用基础库出厂重置：

```text
清理 irr_* namespace
清理 /irrigation/records.bin
清理业务事件日志
调用 Esp32BaseConfig::factoryReset()
调用 Esp32BaseConfig::flushAll()
调用 Esp32BaseSystem::restart("factory reset")
```

普通故障清除只清除锁定状态，不删除浇水记录和事件日志。

## 测试边界

后续实现至少要覆盖：

```text
配置默认值和非法值校验
Zone enabled/disabled 启动规则
一次只运行一个 Zone
用户停止取消当前自动计划剩余 Zone 并清空队列
自动计划周期和多开始时间触发
流量计 K-factor 水量和流速公式
首脉冲超时、运行中无脉冲、低流量、高流量
低液位保护
故障锁定和清除
浇水记录环形覆盖
Web/API 的 GET/POST 语义
```
