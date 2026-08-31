# 会话交接

## 目标

在 `Esp32Base` 中实施用户已经确认的破坏式首版定型重构，使基础库面向一类 ESP32 智能终端复用，而不是只服务灌溉项目：

1. 将 7 个 Profile 收敛为 `MINIMAL`、`OFFLINE`、`LOCAL`、`IOT`。
2. `LOCAL` 提供可靠本地维护平面：WiFi、配网、NTP、mDNS、认证 Web、Web OTA，以及通用运行/诊断基础。
3. `IOT` 在 `LOCAL` 上增加 `Esp32BaseMqtt`；设备 Topic、payload、命令、幂等和业务 Schema 仍留在应用层。
4. 删除 ArduinoOTA/espota，保留并继续优化 Esp32Base 高速 HTTP raw Web OTA：`pio run -t webota`。
5. 删除失效或重复设计，缩小构建矩阵，针对 LOCAL 和 IOT 做 Flash、RAM、连接生命周期和稳定性优化。
6. 基础库完成后回到本项目，将灌溉固件从旧 `FULL` 切换到新 `LOCAL`，最终平台阶段再切到 `IOT`。

## 约束

- 当前项目规则见 `AGENTS.md`。跨项目修改默认禁止；用户必须在当次任务明确授权具体目标项目和范围。上一会话已修改该规则，但新会话仍应让用户明确说出对 `../Esp32Base` 本次修改的授权，或直接在 `Esp32Base` 工作区启动。
- 用户允许本次基础库首版定型采用破坏式更新：不保留旧 Profile、旧宏、旧 API 的兼容别名或迁移层。
- 设计必须面向可复用的设备类别，不能把灌溉硬件、业务协议或当前项目便利逻辑放入基础库。
- 变更 `Esp32Base` 前必须读取其 `AGENTS.md`、README、相关 docs、实现、示例和测试。其现有 `AGENTS.md` 仍写着“固定保留 7 个 Profile”，实施时应按已确认的新最终方案同步修改该规则和全部契约文档。
- 不覆盖或回滚 `Esp32Base` 现有本地提交；该仓库当前比远端超前 2 个提交。
- 不执行烧录、OTA、清 NVS、格式化 LittleFS 或其它实机写入，除非用户再次明确授权相应设备操作。
- 不记录或提交 WiFi、Web Auth、MQTT 等真实凭据。

## 已完成

### 灌溉项目本地定稿整改

提交 `adacc9a refactor: finalize local irrigation firmware` 已推送，主要包括：

- 永久取消 LCD2004、本地菜单和四按钮业务功能。
- 实现 `LED1 / GPIO13` 低电平点亮的启动、就绪、活动和关键故障状态灯。
- 删除已经漂移的旧 MQTT/平台协议、生成契约、私有配置模板及对应测试；平台接入延后到本地产品定稿后。
- 将 10 段大型业务 Web 静态内容改为可读源文件、确定性 zlib 生成物和 ESP32 ROM miniz 有界解压发送。
- 当前灌溉固件显式关闭 ArduinoOTA，只使用 Web OTA。
- 业务 route 容量固定为实际 15 条；本地固件 OTA slot 余量硬门槛为 15%。

### 跨项目规则更新

提交 `88612ef docs: allow explicitly authorized cross-project changes` 已推送：

- `AGENTS.md` 已从绝对禁止修改 `../Esp32Base` 改为：每次跨项目任务必须由用户明确授权具体目标和范围。

### 基础库方案复审

已读取并核对：

- `../Esp32Base/AGENTS.md`
- `../Esp32Base/README.md`
- `docs/01_architecture.md`
- `docs/02_profiles.md`
- `docs/05_ota.md`
- `docs/06_memory_budget.md`
- `docs/10_known_limitations.md`
- `src/Esp32BaseProfile.h`
- `src/Esp32Base.cpp`、`src/Esp32Base.h`
- `src/update/Esp32BaseOta.*`
- `src/web/internal/WebAssets.cpp`
- MQTT、Web、示例和裁剪检查的相关引用

确认事实：

- 当前共有 7 个 Profile，Profile 展开在 `Esp32BaseProfile.h` 中大量重复。
- `ESP32BASE_ENABLE_WEB_OTA` 只出现在宏展开和依赖检查，实际 Web OTA 源码由 `WEB && OTA` 控制，是无实际裁剪作用的伪开关。
- `Esp32BaseOta::beginNetworkServices()` 的实际网络职责是启动 ArduinoOTA；删除 ArduinoOTA 后可删除该公开入口并简化 facade。
- `Esp32BaseWeb::authPassword()` 的当前唯一仓库内消费者是 ArduinoOTA密码同步；删除 ArduinoOTA 后应复核并优先删除该明文密码读取 API。`authUser()`也没有仓库内消费者，应一并判断是否删除。
- 业务项目中速度较快的命令是 `pio run -t webota`，使用 `scripts/esp32base_webota.py` 向 `/esp32base/ota/raw` 发送 HTTP raw binary，不是 ArduinoOTA/espota。
- raw Web OTA 默认使用 64 KiB 客户端块、`TCP_NODELAY`、SHA256、认证/容量预检、RSSI弱网降速和 `X-Firmware-Size`；删除 ArduinoOTA不能影响这条路径。
- 风扇、水龙头、FarmAuto 等项目中的 `ArduinoOTA` 依赖/包含主要由旧 FULL Profile 和 LDF要求带入；现有操作记录主要使用 `webota`，未找到正在生效的 `upload_protocol = espota`。

## 进行中

- `Esp32Base` 代码尚未修改。
- 四 Profile、OTA统一和基础 Web 资源优化已完成设计确认，但尚未实现、构建或实机验证。
- 灌溉项目仍使用旧 `ESP32BASE_PROFILE_FULL`，等待基础库新 Profile 落地后再切换。

## 阻塞

- 当前会话启动时加载的是修改前的绝对跨项目限制，因此上一 Agent 没有直接修改 `../Esp32Base`。
- 新会话读取更新后的 `AGENTS.md` 后，用户需要在该次任务明确授权修改 `/Users/tyg/dir/claude_dir/Esp32Base`，或者直接从该目录启动会话。
- LOCAL/IOT 的实机 Web、OTA、MQTTS 和长稳验证需要后续硬件维护窗口；不能用编译结果替代。

## 关键决策

### 最终 Profile

建议并已获用户同意的目标为：

| Profile | 默认能力 |
| --- | --- |
| `MINIMAL` | Log、Config、System |
| `OFFLINE` | MINIMAL、Watchdog、Health、FS、FileLog |
| `LOCAL` | OFFLINE、WiFi、恢复配网、DNS、NTP、mDNS、认证 Web、Web OTA |
| `IOT` | LOCAL、MQTT/MQTTS |

默认不随 Profile 启用的正交能力包括 Bus、Sleep、RTC、RS485、Record Store、App Events、App Config；项目按真实需求显式开启。实施前仍应在 Esp32Base 工作区按第一性原理复核这些默认值及依赖成本，但不得重新扩张为组合枚举。

### OTA

- 删除 ArduinoOTA/espota 全部实现、依赖、端口3232、密码同步、示例、文档和测试。
- 保留浏览器 multipart Web OTA 与高速命令行 raw Web OTA。
- 删除 `ESP32BASE_ENABLE_WEB_OTA`；统一由 `ESP32BASE_ENABLE_OTA` 表示 OTA核心、Web上传、分区诊断、SHA256、mark-valid和回滚。
- 删除失去职责的 `beginNetworkServices()`。
- 保留 `esp32base_webota.py`、`/esp32base/ota/raw`、`/esp32base/api/ota`、弱网策略、双槽串口恢复和MQTT/Watchdog/WiFi生命周期联动。

### 模块边界

- MQTT、Web多编译单元、Runtime各独立能力的总体分层合理，不因文件行数盲目重构。
- 建议把 `Esp32BaseProfile.h` 中的 Profile展开与能力默认值/容量/依赖检查拆成清晰的编译期配置文件。
- Bus先从所有典型 Profile 默认关闭；它是否彻底删除尚未最终确认，应先检查真实订阅价值、零成本裁剪和替代 API，不能借本轮顺手扩大删除范围。
- Sleep、RTC、RS485、Record Store、App Events、App Config、FS/FileLog、mDNS、DNS等仍有跨设备复用价值，不因灌溉项目暂时不用而删除。
- 基础 Web 固定 CSS/JS应评估确定性预压缩和缓存；正常浏览器优先直接接收压缩表示，兼容策略必须覆盖 ESP32/S3/C3、Core 2/3，不能照搬灌溉项目的 classic ESP32 ROM解压实现。

## 相关文件或数据

### 灌溉项目

- `AGENTS.md`
- `firmware/platformio.ini`
- `firmware/README.md`
- `docs/当前方案/03-软件设计原则与边界.md`
- `firmware/scripts/generate_web_assets.py`
- `firmware/scripts/check_ota_image_size.py`

### Esp32Base

- `/Users/tyg/dir/claude_dir/Esp32Base/AGENTS.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/README.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/src/Esp32BaseProfile.h`
- `/Users/tyg/dir/claude_dir/Esp32Base/src/Esp32Base.cpp`
- `/Users/tyg/dir/claude_dir/Esp32Base/src/Esp32Base.h`
- `/Users/tyg/dir/claude_dir/Esp32Base/src/update/Esp32BaseOta.h`
- `/Users/tyg/dir/claude_dir/Esp32Base/src/update/Esp32BaseOta.inc`
- `/Users/tyg/dir/claude_dir/Esp32Base/src/web/Esp32BaseWeb.h`
- `/Users/tyg/dir/claude_dir/Esp32Base/src/web/Esp32BaseWeb.cpp`
- `/Users/tyg/dir/claude_dir/Esp32Base/src/web/internal/WebAssets.cpp`
- `/Users/tyg/dir/claude_dir/Esp32Base/scripts/esp32base_webota.py`
- `/Users/tyg/dir/claude_dir/Esp32Base/scripts/check_trim_symbols.py`
- `/Users/tyg/dir/claude_dir/Esp32Base/examples/basic/platformio.ini`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/01_architecture.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/02_profiles.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/03_api.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/04_web.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/05_ota.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/06_memory_budget.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/08_arduino_core_compat.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/09_release_checklist.md`
- `/Users/tyg/dir/claude_dir/Esp32Base/docs/10_known_limitations.md`

## 已运行的验证及结果

灌溉项目在 `adacc9a` 对应代码上实际执行：

- `cd firmware && python3 scripts/generate_web_assets.py --check`：通过，生成物一致。
- `cd firmware && pio test -e native`：87/87通过。
- `cd firmware && pio test -e esp32_record_test --without-uploading --without-testing`：设备测试固件编译通过，未上传、未在板卡执行。
- `cd firmware && pio run -e esp32_irrigation`：通过。
  - RAM：91,596 B / 28.0%
  - Flash：1,248,549 B / 79.4%
  - `firmware.bin`：1,255,120 B
  - 1.5 MiB OTA slot剩余：317,744 B / 20.20%
- `git diff --check`：通过。
- `adacc9a` 和 `88612ef` 均已推送到灌溉项目 `origin/main`。

基础库四 Profile/OTA重构尚未运行任何对应验证，因为实现尚未开始。

## 未验证部分

- 灌溉最终固件尚未烧录；状态灯电平/节奏、页面视觉及并发请求、Web OTA、六路阀、泵、流量计、校准、断电、RTC和真实水路均未做当前版本实机回归。
- Esp32Base新Profile的ESP32/S3/C3与Arduino Core 2/3构建矩阵未运行。
- 删除ArduinoOTA后的raw Web OTA回归未运行。
- LOCAL和IOT的map裁剪、Flash/RAM差值、MQTTS TLS握手峰值、稳定heap、重连和OTA并发长稳未验证。
- 基础Web资源预压缩的浏览器兼容、缓存、`Accept-Encoding`及无压缩客户端行为尚未定稿和验证。

## 分支与 HEAD

### 当前灌溉项目

- 路径：`/Users/tyg/dir/claude_dir/Esp32_Irrigation`
- 分支：`main`
- HEAD：包含本 `HANDOFF.md` 的当前交接提交；其父提交为 `88612ef2406d6f527876dcb9f67c4ed1a58cb9a5`。准确提交号以 `git rev-parse HEAD` 为准。
- 远端：交接提交按项目规则推送后，`origin/main`应与HEAD一致。

### Esp32Base

- 路径：`/Users/tyg/dir/claude_dir/Esp32Base`
- 分支：`main`
- HEAD：`59b3d0bb301790d754af26525fa9fb74c8adf0fd`
- 状态：工作树干净，但比`origin/main`超前2个本地提交：
  - `cd28ee6 optimize runtime memory and web request handling`
  - `59b3d0b fix: reject unsafe uploads before body parsing`
- 不得覆盖、丢弃或回滚这两个提交；实施前先审查并在其基础上继续。

## 工作区和未提交改动

- 写入本交接文件前，灌溉项目工作树干净；交接提交并推送完成后也必须保持干净。
- `Esp32Base`工作树干净，但有上述2个尚未推送的本地提交。
- 没有其它已知未落盘或未提交的灌溉项目改动。

## 风险与未知信息

- `Esp32Base/AGENTS.md`当前把7 Profile写成固定规则；下一会话必须把已确认的新四Profile方案同步到规则、README、docs、示例和测试，不能只改宏。
- 删除公开Profile/API会影响其它项目，但用户明确接受其它产品后续按最新方案重新接入；不要保留兼容别名。
- ArduinoOTA技术上仍存在于Arduino Core，但在当前Esp32Base维护平面中被raw Web OTA覆盖；删除依据是职责重复和维护/攻击面，不应误删`Update`、Web OTA或`webota`脚本。
- MQTTS在当前官方预编译Arduino Core中缺少证书日期检查；Esp32Base现有fail-closed/显式接受限制不能因IOT Profile便利性而取消。
- Web静态资源压缩可能节省Flash和传输量，但必须先测量基础库实际资源占比，并设计跨芯片/Core的响应契约；不能未经验证强制所有HTTP客户端行为。
- Profile默认是否包含Bus/Sleep已倾向为否；是否包含FS/FileLog、Watchdog、Health需按目标设备类别和实测成本在实现前最后核算，但不能重新引入大量组合Profile。

## Next Action

在干净新会话中执行以下具体动作：

1. 最稳妥方式是在基础库目录启动：
   ```sh
   cd /Users/tyg/dir/claude_dir/Esp32Base
   pi
   ```
2. 用户在该会话明确发送：
   ```text
   我明确授权本次修改 Esp32Base，范围是实施 HANDOFF.md 中确认的四 Profile、删除 ArduinoOTA、统一并优化 Web OTA，以及同步文档、示例、测试和构建验证；请保留现有两个本地提交并直接开始。
   ```
3. 新 Agent 先完整读取 `Esp32Base/AGENTS.md`、README及其中列出的相关docs，检查HEAD和两个未推送提交，然后建立分阶段任务。
4. 第一实施层先完成四Profile与OTA契约清理：替换Profile、删除ArduinoOTA和伪`WEB_OTA`宏、删除失去职责的API、更新基础示例和symbol裁剪测试；该层全量构建/测试通过后，再进入基础Web资源压缩和LOCAL/IOT专项资源优化。
