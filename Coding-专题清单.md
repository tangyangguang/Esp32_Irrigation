# 灌溉设备 Coding 专题清单

记录跨轮次排队专题；当前进行专题在本文置顶并标注状态。规则与方法见工作区 `AGENTS.md` 与《专题驱动工作法》。

每个专题按「需求 → 方案 → 具体措施 → 结果」组织：需求只写问题与期望，方案写总体做法与取舍，措施写分仓具体改动，结果在实施后补。

---

## 专题 1：小程序完整配置管理（水路 / 计划 / 系统参数）— 已定案，待开工授权

### 一、需求

- 设备安装后，不依赖局域网本地网页，在微信小程序内就能完成全部**日常使用和维护配置**；只有工程师事项才保留在本地 Web。
- 小程序分两级权限：普通用户（use）负责日常使用——浇水、定计划（几点浇、浇不浇本来就是正常使用）；维护权限（maintain）负责设备配置——水路信息、基准流量、系统参数。
- 系统参数必须只有一个地方持有、一个地方管理；网页和小程序都只是界面，同一份存储、同一套校验，不允许同一参数多处存储。
- 配置修改要尽量小：小程序里全部参数都能看到，但**一次只改一个字段**，不做整表批量提交。
- 基准流量希望也能在小程序维护：可手工填；更希望从一次真实浇水记录里直接采纳当时测得的稳定流量，不用跑专门的现场学习流程。

### 二、方案

- **一份存储、多个界面**：22 个灌溉系统参数权威仍只在 NVS（App Config），水路和计划权威仍只在 config.json；小程序命令到达设备后，复用与本地网页完全相同的校验和保存函数，不建第二套。WiFi、Web 密码、MQTT 连接等局域网/Base 级配置不上小程序。
- **计划改为单条操作**：现在契约是“一次下发全部计划、数组里缺哪个等于删哪个”，界面只改一个计划也要重发全量，旧页面会误删别人新建的计划。改为查询看全部、操作只针对单个计划（保存一条 / 删除一条 / 切换自动执行）。
- **系统参数单字段修改**：小程序展示全部 22 项，每个字段独立提交；设备端按固定字段白名单逐字段校验，写之前与当前计划做联合校验（例如调低单路上限会让现有计划超长时拒绝），不合法不落盘。
- **计划时间可能重叠只提示不拦截**：设备端继续只硬性禁止同一分钟重复启动；小程序保存前自行估算时段重叠，提示“可能与计划 X 冲突”，用户坚持则允许保存。
- **禁用水路即清时长**：禁用一路时，把该路在所有计划里的时长清零（不再隐式保留、重新启用后复活）。
- **从记录采纳稳定流量**：设备在每次普通浇水收尾时，已经用和现场自学习同一把尺子（停水前连续 5 个 5 秒窗口、每窗有脉冲、极差 ≤10%）判定末段流量是否稳定。稳定才在记录里给出建议基准，不稳定或浇水太短就没有值。小程序记录详情里有水路建议值时才显示一个**次要按钮**“设为基准流量”，低频操作、不显眼；点击后**二次确认**，文案说明是哪条水路、采纳什么值、会替换当前基准并影响高低流量保护。基准用脉冲率而非换算流量，以后修正流量计系数不会污染历史基准。现场自学习流程仍只在本地网页。

### 三、具体措施

**platform/iot-device（契约 irrigation-controller/v1，跨仓，需授权）**

1. 废弃 `parameter.plans` 全量数组能力，新增 `parameter.plan`（权限 use）：
   - `upsert`：`{revision, action, plan:{id,name,automaticEnabled,startMinutes,zones}}`
   - `delete`：`{revision, action, id}`
   - `set-enabled`：`{revision, action, id, automaticEnabled}`
   - 查询快照不变，仍返回全部计划。
2. 新增 `parameter.zone`（maintain）：`{revision, zoneId, name?, enabled?}`。
3. 新增 `parameter.zone-baseline`（maintain）：`{revision, zoneId, baselinePulseRateX10000}`，0/null 为清除。
4. 新增 `parameter.system-field`（maintain）：`{field, value}`，field 为 22 个参数固定枚举（如 `valve.pull_ms`、`meter.pulse_l`、`flow.low_action`），设备端白名单校验。
5. 命令失败 reason 细化：`revision_conflict`、`invalid_name`、`zone_disabled` 等，替换笼统 `invalid_request`。
6. 同步 definition.json、protocol.md、preflight 工具、契约测试。

**devices/Esp32_Irrigation（固件，本仓）**

- 新增上述命令处理，全部走 `IrrigationApp` 既有保存入口与校验；config.json 类命令带 revision 乐观并发；禁用一路时统一清零所有计划中该路时长，本地网页同行为。
- 系统参数字段命令：用现有 `Esp32BaseConfig::setInt/setBool/setStr` 直写单个 NVS 字段，再读全量组装 `IrrigationParameters`，过 `validateParameters` + `validateRuntimeConstraints`，失败不写；不改 Esp32Base。
- 硬件即时参数（阀时序/PWM/水泵延时）浇水进行中只落 NVS、空闲时协调应用，小程序沿用“待应用/已应用”状态展示，不新增机制。
- 记录链路：末段稳定时计算 X10000 建议基准脉冲率，写入水路记录二进制（记录格式升版、零兼容）与平台记录 JSON；不稳定则字段缺失。删除全量替换与禁用路时长隐式保留代码；config schema 无字段增删，不升版。

**platform/iot-wx-apps（小程序，跨仓，需授权）**

- 计划页：改单条 upsert/delete/set-enabled；保存前重叠估算与提示，用户确认后提交。
- 维护页：22 项参数全部可见、逐项单字段编辑；水路改名、启停；基准流量手工录入/清除；展示待应用/已应用。
- 记录详情页：有建议值才显示次要样式“设为基准流量”，点击二次确认（含水路、数值、影响说明），发 `parameter.zone-baseline`。
- use 角色只见日常使用；maintain 角色额外可见配置维护。

**检查边界**：契约测试；固件定向单测 + 编译 + 链接产物 Flash/RAM 核对；小程序类型检查 + 隔离页面检查。MQTT 实机命令、服务端部署、手机实测需另行授权。

### 四、结果

**契约仓 platform/iot-device**
- `322b923` 协议 1.4.0：新增 `parameter.plan`（use，upsert/delete/set-enabled 单条操作）、`parameter.zone`、`parameter.zone-baseline`、`parameter.system-field`（maintain，22 字段白名单）；浇水记录每水路带可空 `suggestedBaselinePulseRateX10000`；新增审计 `configuration.zone-changed`、`configuration.system-field-changed`。
- `b938b04` 修正：`state.zone-maintenance` 带必填 revision；两条审计记录数据按固定 20B 收敛（zone：revision+zoneId+enabled；field：fieldIndex 1..22，不存名称/新旧值）。
- 证据：`npm run typecheck`、`definitions:check` 通过，16 个测试文件 126 项全部通过（既有 3 个无关文件 prettier 告警为历史状态）。

**固件仓 devices/Esp32_Irrigation**
- `735ee66`：`IrrigationApp` 新增 savePlanSlot/saveZoneInfo/setZoneBaseline/applyRemoteSystemField，本地网页保存改走同一入口；参数 22 项字段描述表与远端候选/联合校验；记录 codec v3→v4（kZoneSize 32→36、payload 246→270），普通浇水末段稳定时计算建议基准脉冲率；审计新增 ZoneChanged/SystemFieldChanged。
- `1de2b96`：README 构建基线与 codec v4 文档更新。
- 证据：native 81 项测试通过；主目标编译通过 Flash 1,542,843 B（87.2%）/ RAM 103,132 B。未做实机烧录与 MQTT 命令验收（需另行授权）。

**小程序仓 platform/iot-wx-apps**
- `c5ee513`：计划页改单条 upsert/delete/set-enabled，保存前对启用计划做时段重叠提示确认；维护页接入水路改名/启停（停用时联动清空计划时长由设备执行并在确认文案说明）、基准脉冲率手工录入/清除、流量计系数独立入口、22 项系统参数逐字段弹层编辑（整数/布尔/枚举三类，按设备同范围校验）；config.json 类命令提交前现取 revision 乐观并发；浇水记录详情有建议值时显示次要“设为基准”按钮并二次确认。
- 证据：`tsc --noEmit` 通过；改动 5 个文件定向 ESLint 无错误。未跑全量 vitest、未做开发者工具页面渲染与手机实测（按用户要求本轮控制测试范围，页面层后续还会调整）。

**未覆盖边界**：服务端未部署新契约产物；小程序未做隔离页面走查与手机实测；固件未实机验证命令落盘、revision 冲突、参数待应用和记录建议值展示；禁用水路真实名称当前依赖 config 快照（state.zones 只投影启用路），禁用路改名回退显示“水路 N”，是否需要扩展投影待后续评估。
