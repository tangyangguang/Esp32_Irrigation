# 专题 02：新烧录设备平台不可见（MQTT 从未配置）

## 需求

新设备 192.168.2.155（esp32-irr-28562f795e60）烧录后小程序中不可见，需判定问题在设备侧、平台侧还是小程序侧；本轮只查设备侧与平台侧，小程序不改。

## 方案与证据链

- 网络、WiFi（IOTHOME，RSSI 约 −40）、NTP、broker（z84e9fd1.ala.cn-hangzhou.emqxsl.cn:8883 TLS）、Mac 侧连通、服务端 `/health/ready` 均正常；Postgres 无该设备任何记录。
- 设备启动日志固定为 `mqtt not_configured enabled=true`，且固件 ELF 中搜不到 broker host、CA 与 `platform_configured` 字符串。
- 预处理输出（-E）host/CA 完整，但编译产物（-Os）中整个 MQTT 配置路径被消除，无编译警告。
- 加临时标记日志并逐段二分后定位：`IrrigationPlatform::buildDeviceId()` 用 `written == 31` 校验 `snprintf` 结果，而 `"esp32-irr-"`（10 字符）+ 12 位 MAC 十六进制 = **22**，校验恒假。`configure()` 在 `Esp32Base::begin()` 之前被调用，必在设备 ID 处失败返回；-Os 在编译期算出 snprintf 长度常量后，把其后的 MQTT host/CA/用户名与 `g_port.configure()` 整段当作不可达代码消除（O0 不做该常量传播，字符串保留但运行时同样失败）。
- 附带时序问题：`configure()` 早于 WiFi 初始化，`esp_read_mac(ESP_MAC_WIFI_STA)` 在该阶段不可靠；改为 `esp_efuse_mac_get_default()`（ESP32 单 MAC，即 STA MAC，与预期设备 ID 一致）。

## 具体措施（提交 c8cddbb）

- `written == 31` 改为 `written == 22`，并加长度推导注释。
- MAC 改读 eFuse 基址，注释说明 pre-begin 时序。
- `configure()` 三个失败点缓存原因（device_id_unavailable / secrets_empty / port_configure_rejected），`begin()` 在日志系统就绪后落盘 `platform_configure_failed reason=...`；pre-begin 失败此前完全无线索。
- README 更新实测连接证据与 Flash 占用。

## 结果

- Web OTA 烧录 192.168.2.155 后启动日志：`mqtt configured host=z84e9fd1.ala.cn-hangzhou.emqxsl.cn port=8883 client_id=esp32-irr-28562f795e60 tls=true`，10 秒内 `connected`。
- 干净全量构建：Flash 1,571,593 B（88.8%，headroom 11.2%，较修复前 +28,322 B，为恢复被误消除的 MQTT 配置代码与 CA）；RAM 103,132 B（31.5%）不变；native 81 项测试通过。
- 设备侧根因闭环。

## 未覆盖边界（平台侧，待处理）

- 本机 dev 服务（iot-dev.tttabc.top → 127.0.0.1:17832）数据库 `iot_home` 仅 10 张表、schema_migrations 止于 6，缺 `device_discovery_candidates`、`product_models`、`device_types` 等整套产品目录/发现表；设备 availability 无法入发现候选库。库重建/迁移属真实数据库变更，需用户审批后执行。
- 服务端长期对未注册设备 state 报 `state_rejected`（属预期拒绝）；availability 的端到端收录待 dev 库补齐后复测。
- 小程序发现/绑定页面走查、设备命令落盘联调仍按原计划后续进行。
- 设备文件日志级别已恢复 WARN；admin/admin 仅实验使用，未入库。
