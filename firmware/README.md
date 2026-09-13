# ESP32 灌溉控制器固件

本轮本地 Web 与设备核心重构已完成源码实现和约定的本机验证。基础库 59/59、灌溉核心 72/72、独立执行任务及存储集成检查通过；最终 LOCAL 固件构建、静态资源一致性和 OTA 容量门禁通过。未进行浏览器验收、实机升级或真实水路测试。

## 当前待办（2026-09-13）

- [x] 本地执行/记录边界：来源与目标方式分开，持久任务标记、未知中断结果、本地滚动历史、RAM Conditions。
- [x] 页面结构：每日水路卡片、次级手动、计划编辑、记录详情、设备维护；同步测试源码和当前契约。
- [x] 源码人工联动审阅及必要问题修复。
- [x] 获批本机测试、存储集成及最终构建通过；更新验证结论。配套提交以两个责任仓的 Git 历史为准。

约定的本机验证无剩余项。浏览器体验、实机升级与真实水路验证尚未执行，需单独明确目标及授权；MQTT、IoT 平台和小程序适配属于后续独立任务，不在本轮继续实施。

## 本地运行契约

业务规则及状态边界以 [业务规则](../docs/当前方案/02-业务规则与安全.md) 和 [软件设计](../docs/当前方案/03-软件设计原则与边界.md) 为准。硬件依据仍为 pcb_irrigation 下 2026-07-11 的 BOM/网表；不改变阀泵顺序、独立控制任务、TWDT、无流量保护、学习时限和维护门禁。

主目标显式 LOCAL Profile，启用 RTC、RecordStore、Conditions、AppConfig；SDK 不在 lib_deps，IrrigationIot*.cpp 与 IrrigationCommandJournal.cpp 从目标排除。旧适配文件供下一任务审阅，不提供本轮兼容保证。文件日志不改。

配置 schema 5 保持不变。watering v8：210 B payload + 24 B Base 槽；audit v4：20 B payload + 24 B 槽。预算 160/48 KiB，均 RotateOldest，保留条数动态推导；不因未同步阻止轮转。任务标记 234 B；真正的写入/校验失败保持明确故障，输出关闭后结果不能虚报成功。旧历史不迁移、不自动清理，升级维护只能处理明确的旧测试历史，不能格式化来掩盖容量问题。

最终受控 Core 3 LOCAL 构建：镜像 1,344,752 B，链接 Flash 1,344,340 B，PlatformIO 静态 RAM 75,860 B；每个 OTA 槽剩余 424,720 B（24.00%）。运行堆/栈峰值未测量。分区不变：4 MiB 布局、双 1728 KiB OTA、512 KiB LittleFS、64 KiB coredump；历史预算 208 KiB、文件日志 128 KiB、FS 安全空间及配置余量保留。控制任务 4096 B 栈来自运行堆，不能用静态 RAM 值冒充运行峰值。新增的记录 payload、任务 marker、统计上下文均有固定上限，无持久日汇总、无新消息队列。

## 验证入口与结果（再次执行需遵守工作区授权）

在工作区根目录：

```sh
python3 foundation/Esp32Base/scripts/pio_arduino.py 2 test -d foundation/Esp32Base -e native_record_store_harness -e native_config_harness
python3 foundation/Esp32Base/scripts/pio_arduino.py 2 test -d devices/Esp32_Irrigation/firmware -e native
python3 devices/Esp32_Irrigation/firmware/scripts/test_storage_views.py
python3 devices/Esp32_Irrigation/firmware/scripts/test_executor.py
python3 foundation/Esp32Base/scripts/pio_arduino.py 3 --tls-toolchain run -d devices/Esp32_Irrigation/firmware -e esp32_irrigation_arduino3
```

本轮上述入口均通过。验证中修复了旧事件上下文接口的遗留引用、宿主时间接口，以及审计待存事实在 OTA 写入暂停期间应保留原时间并延后写入的判断；只重测相关存储集成和主目标，未重复未受影响的已通过批次。

覆盖 codec/调度/控制器、本地滚动/重启记账/未知统计、Base Conditions 和 blob 读取错误、原执行任务隔离。构建同时通过静态资源漂移与 OTA 容量门禁。全部操作仅生成本机产物，未烧录、OTA、启动服务或浏览器，也未触发真实水路。编译器仅提示 LTO 串行执行，不影响构建通过。

`test_storage_views.py` 使用真实 Base 存储引擎与内存文件系统，NVS 标记用受控夹具；不代表物理掉电验证。`esp32_record_test` 会格式化设备文件系统，不能随本机批次运行。测试失败后按工作区规则先只读定位及修复，再申请有限重测。

## Web 源与生成物

`web-src/` 为 CSS/JS 权威源，`python3 scripts/generate_web_assets.py` 只生成 gzip 固件静态源。构建执行 `--check` 漂移检查；本轮生成物一致性检查通过，浏览器视觉与交互验收未执行。样式不使用卡片左侧彩色竖条。后续视觉调整应只影响此展示层。

## 7. HTTP Web OTA

项目只使用 Esp32Base HTTP Web OTA。复制本地模板：

```sh
cp platformio.example.ini platformio.local.ini
```

在 Git 忽略的 `platformio.local.ini` 中填写设备地址和当前 Web Auth：

```ini
[env:esp32_irrigation_arduino3]
custom_esp32base_webota_host = irrigation-controller.local
custom_esp32base_webota_user = <current-web-auth-user>
custom_esp32base_webota_password = <current-web-auth-password>
```

操作者确认设备空闲和维护窗口后显式执行：

```sh
python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 3 --tls-toolchain run -e esp32_irrigation_arduino3 -t webota
```

普通构建和测试不会触发 OTA。不得提交真实设备地址、账号或密码。不得配置 espota、ArduinoOTA 或 3232 端口。


## 历史验证证据（以下均非本轮验收）

以下保留先前版本的实机与平台证据，其中旧 API、尺寸、通过状态及“本轮”均指原日期的任务，不能用于当前 LOCAL 重构验收或扩大本次测试授权。

## 8. 实机验证边界

514e149 版本已串口烧录、校验并启动，双 Store 就绪、业务无存储故障；NTP 同步、MQTTS 连接（含证书日期校验），平台收到完整业务状态和诊断。实机发现 Core 3 初始 GPIO 电平预置失效及 OTA 退出预算不足，两项修复已进入 f76f37b / Base cdd1923，两项已通过实机与 Web OTA。

本机服务通过 `./server restart` 更新至当前源码，入口 `http://127.0.0.1:17832`；未更新 NUC 或公开发布小程序。目标编译、主机测试、真实 MQTT、OTA 与真实水路分别记录。核心板缺少 DS3231 和水路部件，不能把其 RTC/流量故障当作平台接入失败，也不能以核心板通过代替现场验收。

### 2026-09-10 当前固件实机闭环

- 修复 MQTT 参数执行时 loopTask 栈溢出：完整学习状态快照不再叠加在无关的暂停/恢复分支；计划配置、命令判断和活动快照保持独立栈帧（LTO 下也不内联）。22 处仅查询 active 的调用改为直接读取控制器标志，保留完整状态 API，不新增常驻缓冲、不调大栈。
- 控制器/调度定向测试 50/50，当前 Core 3 TLS 目标编译及 8% OTA 余量门禁通过，静态 RAM 不变。最终 Web OTA 耗时 26.76 秒，当前运行 app0 / valid，镜像校验 SHA256 `cb96281227d520d2275e52a04a77194308f6950aa6f0f82c07dd5d49ce9d484d`；原 NVS、配置和记录保留。
- 自动总控 `9ed17407-d72b-4fc6-a42a-7faf151b6a04`、最终计划保存 `30be985e-2f0e-4e6a-97e7-7c17a3fa7746` 均 accepted → succeeded。计划测试保留空计划，仅按正常保存递增 revision。
- 单次出水 `e16e6d3f-f406-4b4b-b700-8c90de754d2f` accepted → running → failed / flow_start_timeout；核心板没有流量计，20 秒启动保护安全结束，非实际水路成功验收。平台已收到 watering.failed，流 `e20eebc1-e2da-40f5-83ac-43eb85c58296` 序号 1 的累计业务 ACK，原生小程序显示失败结果与浇水事实。设备仍 ready、空闲、无存储故障。
- Base API 文档宽限上限漏改已在 `10505fe` 修正推送；本次设备源码构建依赖与 `cdd1923` 相同。旧 panic 证据不能冒充新版复位原因；新版 OTA 后为 software，已验证命令期间无重启。

- 最终本机 server 21b9eb9 已构建重启、ready=true；原生微信暂停 `09347a1a-06d9-408d-99cb-0917a05eb67c` 和恢复 `8e415aa1-40b2-4eab-98f3-64ceecb0102f` 均约 2 秒 succeeded，相应审计事实显示正常。结束时同一 bootCount 90、reset=software、在线/空闲、自动总控 enabled、空计划 revision 3，无存储故障。旧无回执命令已过期并退出活动区。
- 已核对存储重构遗留：SDK 序号/世代/ACK 接管、首次启动与当前格式恢复、未确认保护、维护安全及资源容量均已实现并完成相关快速检查；旧方案中 NVS 命令账本、临时 RecordSync 和“不拦截维护”描述已删除并同步当前设计。剩余仅上表现场试运行及正式发布边界。

本地顶层导航为首页、计划、水路、记录；记录页提供事件与当前异常入口，水路页直达系统参数，移除仅转跳的设置路由。手动/单次出水只要求浇水流可写，自动计划仍要求浇水与审计流均可写，修改计划/自动总控要求审计流可写。MQTT 命令证据优先；状态组轮转并让出记录发送机会，保留原有重发周期与业务 ACK 语义。

### 本轮审查结论

| 范围 | 当前决定及依据 |
|---|---|
| 计量 | 只消费 P/L，删除本地样本、拟合、启动补偿和校准专用页面；不预建未来接口。 |
| 配置 | 分区/计划 JSON 与标量 NVS 各有唯一责任；保存时继续验证运行约束与写入完整性。 |
| 记录 | 本地与平台同一事实、两个独立流与业务 ACK；保留断网补传、未确认保护和历史完整字段。 |
| 故障准入 | 按操作所需记录流检查；通用 ready 表示业务核心可接收请求，操作仍独立检查存储与安全条件。 |
| 本地交互 | 四个日常导航；事件与维护入口仍可达，保留计划编辑、手动控制、实时趋势及分区学习。 |
| 时间与安全 | 保留可信时间检查点、RTC 防倒退、泵阀时序、无流量及异常保护，不把安全机制当作冗余。 |
| MQTT | 命令证据优先，状态组轮转，状态与历史发送交替取得机会；不改重发周期和确认契约。 |
| 基础库 | App Config/OTA/文件上传脚本无损预压缩，功能宏裁剪，无运行时解压或平台业务依赖。 |
| 体积归因 | ELF/map 中合并字符串池约 259 KiB 曾归到 GPIO archive，不能据此认定 GPIO 驱动膨胀；WiFi、lwIP、TLS 和运行时占有必要成本。本轮没有关闭 TLS、OTA、日志或缩减历史预算来换体积。 |

配置 v5 和审计 v3 只支持当前格式。后续更新旧样机时需核对并初始化指定旧业务配置/审计，不写迁移程序、不清凭据、不默认格式化整个文件系统。本轮首次代码交付未刷机；用户追加授权后的 OTA 与定向配置初始化见下节。

最终镜像 SHA256：`68feb33a42d93b7f9ba42a50a418088c8f970128288ed41cb3fc47e4830ba8c1`。相对本轮起点减少 47,328 B Flash（2.93%）和 2,088 B 静态 RAM；水量、控制、安全和记录预算保持。基础库新增页面脚本的 gzip 无损还原、原内联脚本逐字比对、JavaScript 语法与协商检查通过。当前目标使用 Base 本地责任仓库的真实源码，未改缓存副本。

### 完整硬件短时验证（2026-09-10）

用户授权使用完整硬件、尚未接水路的实验设备；物理 Flash 为 8 MiB，仍按 4 MiB 布局使用。串口仅监视，未使用串口烧录。通过 Base 正式 Web OTA 上传当前 1,567,776 B 镜像，设备校验 SHA256 与文中最终镜像一致，33.39 秒完成；重启运行 app1，状态 valid，槽容量 1,769,472 B。

旧配置 schema 4 且无已配置计划；新版按设计安全保持未就绪。仅删除指定 `/app/irrigation/config.json` 并正常重启，初始化当前 schema 5。未格式化、未清 NVS/凭据或 watering 历史；旧 audit v2 控制文件受 Base 保护未删除，新版创建独立 audit v3，不读取旧格式。两条当前记录流就绪，容量 666/675，RTC 可用，WiFi/MQTTS 连接正常。

合并检查首页、水路、记录、事件和系统参数响应；三项 Base 页面脚本均与源码 gzip 解压结果一致。每升脉冲数增加 0.01 后保存并读回，再恢复原值，其他参数未改变。一次水路 1 单次出水因没有水流，在 20 秒启动超时后进入空闲，页面生成“启动后未检测到水流，整次任务已安全停止”的失败记录；未出现存储故障。最终 ready=true、active=false，串口监视已结束。

以上是 HTTP、串口及无水流闭环证据，不代替用户视觉确认、实际水量精度、带水泵阀性能或长期实验。不追加全量、多芯片、长期及重复测试。

本机 `iot-home-server` 已使用当前 `dc9e694` 源码/manifest 构建重启，health/ready 中 database、lease、MQTT 全部正常。未部署 NUC，未上传/发布小程序，未将连接成功冒充本轮平台记录 ACK 或原生微信验收。

### 2026-09-10 架构重构第一阶段

- `WateringExecutor` 复用原控制器，完成结果保持到服务任务显式确认清除；维护先取得输出关闭确认。未增加公共任务框架。
- `IrrigationParameters` 只含六组标量参数；参数默认值、校验及临时副本不再携带水路/计划。计划保存只更新计划和水路，硬件频率按目标值与已应用值协调；当前操作保留启动快照。
- Base App Config 新增只读 `ApplyStatusCallback`，参数页独立显示持久化和应用结果；无变更保存也重新检查应用状态。硬件故障保持安全未就绪，必要时重启恢复，不伪装为保存成功即硬件成功。
- 检查：`test_executor.py` 通过服务线程停顿 1150 ms 时吸合转维持/无流量停止、连续改频率、结果保留、停止、维护与硬件失败；生产控制器/配置 native 46 项通过；既有 host GPIO 和存储检查通过。目标受控 Core 3 构建通过；随后停止/自动结束竞态修正通过同一 executor 检查，最终镜像将在双 Store 阶段合并构建。阶段构建 bin 1567680 B，静态 RAM 95172 B，OTA 余量 201792 B（11.40%）。新增任务栈 4096 B 及任务控制块来自运行堆，不能把静态 RAM 下降当作总运行内存下降。
- 这是主机逻辑与目标编译证据；当前阶段尚未 OTA，不代表实机时序测量。

### 2026-09-10 双 Store 故障隔离

watering 与 audit 分别登记、检查底层及 SDK 就绪；加载失败的 Store 定义仍交给 Base 维护受管路径和容量，显式格式化可恢复原对象。手动/单次出水仅要求 watering，自动计划额外要求 audit；两流全局 ready 只作为汇总，不再作为健康单流的全局阻塞。运行中完成结果未提交仍阻止新出水。

`python3 scripts/test_storage_views.py` 使用真实 Base/SDK 和内存 FS，通过逐一损坏两流 control.bin 的初始化失败、健康流继续追加、两 Store 仍登记，以及显式 format/reload 后双流恢复。既有记录时间、CRC/损坏、ACK 隔离、Conditions 检查同时通过。未运行物理格式化或清空设备数据。

最后增加启动早期未登记 Store 的格式化恢复检查，并让控制任务在首次启动/明确恢复时自行配置 PWM。重新执行 executor 与存储宿主检查通过；最终目标构建 bin 1567936 B / 静态 RAM 95196 B，SHA256 `21558897109187c134c86e8cb03751a0f0a216e330d38ccc06eac331c18ce382`。配置 JSON/NVS 与记录格式未变，无迁移或数据清除。


### 2026-09-10 架构重构最终 OTA 与合并检查

最终镜像及 SHA256 见上一节。首次上传前设备失联，客户端发送 0 B，串口仅有不完整启动信息，原因未确认；用户手动重启后恢复。重试未打开串口，通过 Base 正式 Web OTA 在 29.21 秒内完成上传和响应；重启后 HTTP 核对运行镜像 1,567,936 B、app0 / valid、槽容量 1,769,472 B，业务就绪且空闲。全程未使用串口烧录、未格式化或清除历史。

一次合并的实机检查覆盖：运行时修改 PWM 频率，随后保存无关计量参数仍显示 pending；主动停止后 pending 消失且无应用失败提示；还原全部测试参数并逐字段核对；再次启动无水任务，23 秒后已停止、业务仍就绪、两流无存储故障且记录页面可见水流故障结果。PWM 实际波形未用仪器测量，不以页面状态代替波形证据。检查结束可用堆 76,320 B、启动以来最低 52,004 B，仅作为该次短时运行观测。

相对本次架构调整前镜像 1,567,776 B / 静态 RAM 98,308 B，最终镜像增加 160 B、静态 RAM 减少 3,112 B；新增独立控制任务还占运行堆中的 4096 B 栈及任务控制块，因此不声称总运行内存降低。此次取舍用于隔离控制与阻塞服务，未削弱 TLS、OTA、存储完整性或执行器保护。本轮编码及必要验证已完成；不追加重复测试或长期实验，专业校准小程序不在本轮范围。


### 2026-09-10 平台与小程序配套收尾

该次配套误把本地水路基准学习当作专业流量计校准，删除了仍被固件使用的 `learning` 声明与维护保护；此结论已撤回并在后续联调恢复。维护页移除过期参数数量的修改保留，`state.calibration` 仍只表示每升脉冲数，没有新增专业校准接口。

配套提交均已推送：lab `3fcda07`、server `3d7cf2a`、wx `3d099ba`。当前定义 checksum 为 `2c1392bacb0310f25d168af041759a2614bca28dd38898fc71fe65bb377cb6e6`；服务端 manifest 与固件 `IrrigationModel.h` 通过各自正式脚本同步。lab 类型/定义检查与协议/模拟器 32 项、小程序类型检查与灌溉/首页呈现 39 项、服务端定义/统计 4 项定向测试通过。小程序构建及产物检查通过，产物 API 为 `https://iot-dev.tttabc.top`；本机服务构建重启后 config/database/lease/MQTT 均 ready。未上传或发布小程序、未部署 NUC、未重复手机扫码验收。

固件只更新生成模型，原控制/存储检查复用；按上文受控 Core 3 命令增量构建通过，bin 1,567,920 B、静态 RAM 95,196 B、OTA 槽余 201,552 B（11.39%）。bin SHA256 为 `5d61b576fc88f05f8c7e703bd393beaeabd3b39dff1a578e26020254e1249abb`。授权实验板空闲状态下经 Web OTA 在 28.74 秒完成；重启后核对运行新镜像、app1 / valid、业务就绪空闲、两流无存储故障。未打开串口、未改参数或清除记录、未重复水路实验。设备、基础库与配套开发任务至此关闭。


### 2026-09-10 测试小程序实际状态与命令闭环

用户反馈的“在线，等待灌溉状态同步”对应旧核心板 F9D108（192.168.2.141），并非完整硬件板 AA8C（192.168.2.127）。旧板仍上报启动补偿和校准窗口旧字段，当前严格定义拒绝计量/系统参数，状态集不完整；未放宽校验或迁移旧状态。完整硬件板此前只是发现候选，现已通过正式 API 添加为“灌溉完整硬件测试板 AA8C”（平台 ID `fcfac6fc-dbd4-4eed-8b5b-2ce2eefe6b8a`），七项状态与两项参数完整，fresh/known。

纠正前次配套判断：水路基准学习是仍在使用的本地业务，与已抽出的专业流量计校准不同。恢复 learning 状态及 maintenance_activity 远程停止保护，当前定义 checksum 为 `c844cae4e5e2e18c4cb186e1b0b17b9ac48302450774b8d8bd96f0c95dbf13af`；lab `ca039aa`、server `8779256`、wx `84209be` 已提交推送。类型/定义检查、lab 32 项和 wx 39 项定向检查通过（lab 一项并行编译期间超时，单独重验通过）；服务端定义加载 3 项检查通过并完成本机构建重启，小程序构建/产物检查通过。

实链路同时发现远程浇水完成记录误标 local_web：控制任务在服务轮次中间结束时，IoT 可先清除活动命令，应用下一轮落盘再读取活动命令就丢失来源。现从 IoT 启动入口显式传入 commandId，启动成功时与开始时间一并快照；直到完成记录提交后才清除，不依赖 MQTT 会话/活动生命周期。记录格式与正常本地调用保持不变，不回填已写入的旧测试记录。

修复镜像经受控 Core 3 构建通过，bin 1,568,160 B、静态 RAM 95,228 B、OTA 余量 11.38%，SHA256 `a5235324f3ac8ec71d6fa5ba7390916c1b056fc95755451fc0769b5203396f7b`。授权实验板 Web OTA 27.00 秒完成，重启核对新镜像、app0 / valid。没有串口烧录、配置清除或数据迁移。

微信开发者工具运行本项目 dist/iot-home，通过 https://iot-dev.tttabc.top 实际显示“当前空闲／设备已就绪”；暂停/恢复均 succeeded，单次出水显示运行进度并得到无水保护 flow_start_timeout，停止有 succeeded 终态，维护页可见 250 脉冲/升与全部当前参数。最终镜像经同一测试服务 API 再验证启动/及时停止：启动命令 canceled、停止 succeeded、watering.stopped 记录 sourceKey=wechat_miniprogram 且 relatedCommandId 精确对应启动命令；随后当前状态 fresh/known/idle、无在途命令。另用当前 revision 原样保存空计划成功，计划内容不变，自动总控保持 enabled。终态/记录可以先于状态刷新到达，验证等待当前状态更新后再确认空闲。

这是测试服务、真实 MQTT、授权完整硬件与开发者工具的闭环证据；未冒称手机扫码、真实水路精度或长期稳定性验证。旧核心板保留原设备和历史，未将其标为正常或已升级。

## 2026-09-10 接入仓库整合验证

SDK 和契约统一消费 `platform/iot-device`（结构提交 `824d68a`），定义内容与 SDK 运行代码未变。本轮从新路径运行 `python3 scripts/test_storage_views.py`、`IOT_DEVICE_CONTRACTS_ROOT=/Users/tyg/workspace/iot/platform/iot-device/contracts python3 ../../../foundation/Esp32Base/scripts/pio_arduino.py 2 test -e native_iot_vectors` 和本页受控 Core 3 TLS 构建命令，均通过。共享向量覆盖 33 个输入案例；目标 RAM 95,228 B，Flash 1,567,751 B，OTA 镜像 1,568,160 B，1728 KiB 槽剩余 201,312 B（11.38%）。本轮未烧录、OTA或操作真实负载，既有实机证据仍只对应原验证版本。
