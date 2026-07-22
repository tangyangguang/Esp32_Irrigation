# N1 + Cloudflare Tunnel 无人值守部署方案

## 1. 目标与边界

本方案用于把老家局域网内的 ESP32 智能浇水网页，通过自有域名安全地提供给手机远程访问。

必须达到以下目标：

- 不要求家庭宽带具备公网 IPv4、固定 IP 或端口映射。
- 老人不需要登录 N1、输入命令或修改路由器。
- 断电重启后，路由器和 N1 恢复供电，Tunnel 应自动恢复。
- N1、Tunnel 或外网故障时，ESP32 的本地自动浇水、保护和记录继续独立运行。
- 家庭用户只看到日常控制页面，不接触校准、OTA、日志和系统设置。
- 管理员在外地能够判断是宽带、N1、Tunnel 还是 ESP32 故障。

无法承诺的事项：

- N1、电源、路由器、eMMC 或 ESP32 的永久性硬件损坏无法靠软件恢复。
- 运营商、Cloudflare 或家庭宽带长时间故障时，远程访问不可用。
- 没有经过断电和断网测试的普通 Armbian 安装，不能视为无人值守系统。

## 2. 总体架构

```text
手机浏览器 / 主屏幕图标
          │ HTTPS
          ▼
Cloudflare Access（家庭成员身份验证）
          │
          ▼
Cloudflare Tunnel
          │ 出站连接，无需开放入站端口
          ▼
N1（Armbian + cloudflared）
          │ 家庭局域网 HTTP
          ▼
ESP32 智能浇水控制器
```

关键原则：

1. ESP32 是唯一的浇水业务核心和安全控制器。
2. N1 只提供远程访问，不保存或执行浇水计划。
3. Cloudflare 故障只能影响远程访问，不能影响自动浇水。
4. 远程页面不得成为系统设置、OTA 或维护接口的公开入口。

Cloudflare Tunnel 通过 N1 主动建立出站连接，无需公网 IP 和路由器端口映射。官方说明见：

- <https://developers.cloudflare.com/tunnel/>
- <https://developers.cloudflare.com/tunnel/advanced/local-management/as-a-service/linux/>

## 3. 上线前必须具备的硬件与信息

### 3.1 硬件

- 斐讯 N1，硬件状态正常。
- 与 N1 匹配、质量可靠的电源适配器。
- 有线网线；正式部署不使用 Wi-Fi 连接 N1。
- 运行正常的家用路由器或光猫路由。
- ESP32 浇水控制器使用独立、稳定的电源。
- 散热和通风良好的安装位置，避免密闭、潮湿和阳光直射。

建议将设备分成两个有明确标签的电源回路：

- “网络网关”：路由器/光猫、N1。
- “浇水控制器”：ESP32 和浇水控制板。

远程访问故障时，老人只重启“网络网关”，避免正在浇水时误断 ESP32 电源。

可选恢复手段：

- 给 N1 单独使用支持“来电自动通电”的合规智能插座，用于管理员远程断电重启。
- 准备一台已配置好的备用 N1；硬件损坏时直接整机替换，比指导老人修 Linux 更可靠。

### 3.2 账号和网络信息

准备并离线记录：

- 已接入 Cloudflare DNS 的自有域名。
- 计划使用的子域名，例如 `water.example.com`。
- Cloudflare 管理员账号及双因素认证恢复码。
- 允许访问的家庭成员邮箱列表。
- ESP32 当前局域网 IP、MAC 地址和主机名。
- N1 当前局域网 IP 和 MAC 地址。
- 路由器管理地址和管理员凭据。

不得把 Tunnel Token、Cloudflare 恢复码、路由器密码或 ESP32 密码写入本仓库。

## 4. N1 系统要求

### 4.1 Armbian 镜像

N1 使用的是 Amlogic S905D，常见 Armbian 镜像属于社区适配，不同镜像的启动、设备树和写入 eMMC 命令可能不同。

因此：

- 只使用来源明确、维护记录清楚、已确认支持 N1 的 ARM64 镜像。
- 在 U 盘或 TF 卡完成网卡、重启和稳定性测试后，再考虑写入 eMMC。
- 写入 eMMC 前必须备份原系统和重要分区。
- 必须严格使用所选镜像维护者给出的安装命令。
- 禁止照抄其他 N1 教程中的 `install-aml.sh`、`armbian-install` 或设备节点。

写入 eMMC 会覆盖设备数据，属于不可逆高风险操作。执行前必须单独确认镜像、设备树、目标磁盘和备份可恢复性。本文件不提供通用写盘命令。

### 4.2 正式部署必须从 eMMC 启动

不建议使用 TF 卡作为长期无人值守系统盘。正式验收前运行：

```bash
findmnt -no SOURCE,FSTYPE,OPTIONS /
lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINTS,MODEL,TRAN
```

必须人工确认根文件系统确实位于 N1 的 eMMC，而不是安装介质。不能仅凭 `/dev/mmcblkX` 名称猜测，因为不同镜像的编号可能不同。

### 4.3 基础系统设置

首次启动后：

```bash
sudo apt update
sudo apt install -y ca-certificates curl jq
sudo timedatectl set-timezone Asia/Shanghai
sudo timedatectl set-ntp true
timedatectl status
```

要求：

- 使用普通管理员账号，禁止日常直接使用 root。
- 管理员账号使用 SSH 公钥。
- 时间同步正常；错误系统时间可能导致 HTTPS/TLS 连接失败。
- 主机名固定，例如 `irrigation-gateway`。
- 不安装桌面环境、Docker、数据库、Node-RED 等与本方案无关的服务。
- 不开启自动大版本系统升级；升级必须由管理员远程执行并验证。

查看交换空间：

```bash
swapon --show
```

Armbian 使用 zram 属正常情况；如果发现持续写入 eMMC 的传统 swap 分区或 swapfile，应评估后关闭，不能盲目删除。

### 4.4 降低断电损坏和写盘

建议让 systemd journal 使用易失内存并限制大小：

```bash
sudo mkdir -p /etc/systemd/journald.conf.d
sudoedit /etc/systemd/journald.conf.d/volatile.conf
```

写入：

```ini
[Journal]
Storage=volatile
RuntimeMaxUse=32M
RuntimeKeepFree=64M
```

应用：

```bash
sudo systemctl restart systemd-journald
journalctl --disk-usage
```

影响：重启后系统日志会消失。因此发生故障时应在重启前尽量远程查看；本方案用可靠启动和低写盘换取无人值守稳定性。

只在所选 Armbian 镜像明确支持时，才进一步考虑只读根文件系统或 overlay 模式。启用前必须验证升级、时间同步、SSH 密钥和 cloudflared 配置仍能持久保存。

## 5. 局域网配置

### 5.1 使用 DHCP 保留地址

在路由器中按 MAC 地址保留：

- N1，例如 `192.168.2.10`。
- ESP32，例如 `192.168.2.50`。

地址仅为示例，必须按现场网段修改。

推荐由路由器 DHCP 保留地址，而不是在 N1 内写死网关和 DNS。这样路由器重启后恢复更可靠，也能避免地址冲突。

验证：

```bash
ip address
ip route
ping -c 4 192.168.2.1
curl --silent --show-error --output /dev/null --max-time 5 http://192.168.2.50/irrigation
echo $?
```

ESP32 页面需要认证时返回 401 仍能证明 HTTP 服务存活；`curl` 无法连接或超时才表示局域网不可达。

### 5.2 网络恢复要求

N1 必须能够处理以下启动顺序：

1. N1 先通电。
2. 路由器数分钟后才获得外网。
3. DNS 和时间同步随后恢复。
4. cloudflared 自动重试并最终上线。

不能依赖“安装时路由器已经在线”这一种理想顺序。

## 6. 安装 cloudflared

### 6.1 确认架构

```bash
dpkg --print-architecture
uname -m
```

N1 的 Armbian 正常应为 ARM64，常见输出是 `arm64` 和 `aarch64`。输出不一致时停止安装，先确认镜像。

### 6.2 安装官方 ARM64 包

```bash
cd /tmp
curl --fail --location --output cloudflared.deb \
  https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64.deb
sudo dpkg -i cloudflared.deb
cloudflared --version
```

如果 `dpkg` 报依赖错误，先停止并记录完整输出，不要通过安装大量无关包规避。

官方更新说明：

<https://developers.cloudflare.com/tunnel/downloads/update-cloudflared/>

## 7. 创建和安装 Tunnel

优先使用 Cloudflare 控制台创建“远程管理 Tunnel”，便于在 N1 损坏后重新绑定备用设备。

### 7.1 在 Cloudflare 控制台创建

1. 登录 Cloudflare Zero Trust。
2. 进入 **Networks / Tunnels**。
3. 创建 Cloudflared Tunnel，例如 `home-irrigation`。
4. 选择 Linux ARM64。
5. 控制台会生成带 Tunnel Token 的安装命令。

Tunnel Token 等同设备接入凭据：

- 不粘贴到聊天、截图或仓库。
- 不写入普通用户脚本。
- 泄漏后立即在 Cloudflare 控制台轮换。

### 7.2 安装为系统服务

按控制台生成的命令安装，形式通常为：

```bash
sudo cloudflared service install <TUNNEL_TOKEN>
```

随后检查：

```bash
sudo systemctl enable cloudflared
sudo systemctl start cloudflared
systemctl status cloudflared --no-pager
journalctl -u cloudflared -b --no-pager -n 100
```

Cloudflare 官方建议在 Linux 上以 systemd 服务运行，以确保开机启动和持续运行：

<https://developers.cloudflare.com/tunnel/advanced/local-management/as-a-service/linux/>

### 7.3 加强 systemd 自动恢复

```bash
sudo systemctl edit cloudflared
```

写入：

```ini
[Unit]
Wants=network-online.target
After=network-online.target
StartLimitIntervalSec=0

[Service]
Restart=always
RestartSec=10s
```

应用并验证：

```bash
sudo systemctl daemon-reload
sudo systemctl restart cloudflared
systemctl show cloudflared \
  -p UnitFileState -p ActiveState -p SubState -p Restart -p RestartUSec
```

预期：

- `UnitFileState=enabled`
- `ActiveState=active`
- `SubState=running`
- `Restart=always`

## 8. 配置域名转发

在 Tunnel 的 Public Hostname 中配置：

- Hostname：`water.example.com`
- Service：`HTTP`
- URL：`http://192.168.2.50:80`

不要把示例 IP 原样使用。

Cloudflare 会为该主机名建立 Tunnel DNS 记录。官方路由说明：

<https://developers.cloudflare.com/tunnel/routing/>

建议为整个远程主机名设置 Cache Rule：**Bypass cache**。浇水状态和操作响应均为动态内容，不应由 CDN 缓存。

## 9. 必须先配置 Cloudflare Access

在允许手机使用前，为 `water.example.com` 创建 Self-hosted Access Application。

建议策略：

- Decision：Allow。
- Include：指定家庭成员邮箱。
- 登录方式：Email one-time PIN，或家庭已有的受保护身份提供商。
- Session duration：根据使用习惯设置，例如 30 天。
- 其余用户默认拒绝。

管理员账号必须启用双因素认证。

验证要求：

- 未登录的无痕浏览器无法看到 ESP32 页面。
- 非白名单邮箱不能登录。
- 手机流量下白名单用户能够登录。
- 删除共享成员后，其会话在预期时间内失效。

Access 只解决“谁能进入域名”，不能自动区分 ESP32 内部的管理员设置权限。

## 10. 家庭控制页面与权限边界

当前项目还没有专门的远程家庭角色。正式交付给家人前，必须完成以下一种方案：

### 推荐方案

在 ESP32 项目中增加独立的家庭远程页面和受限接口，只提供：

- 在线和业务就绪状态。
- 待机/正在浇水/异常。
- 当前水路、剩余时间和流量摘要。
- 六路手动浇水时长。
- 开始浇水。
- 紧急停止。
- 自动计划启用/暂停。
- 下一次计划和最近一次结果。

不得提供：

- OTA、重启和恢复出厂。
- 校准和流量学习。
- 系统参数。
- Wi-Fi、账号和认证设置。
- 日志、事件维护和文件系统格式化。

### 临时测试方案

在远程家庭页面完成前，只允许管理员邮箱进入 Tunnel，不向家人开放。不能因为页面“没有明显链接”就认为系统设置已经被安全隔离。

所有远程写操作仍必须由 ESP32 执行现有业务校验，包括设备就绪、任务冲突、参数范围、配置修订号、流量保护和安全停机。

## 11. N1 自检服务

systemd 已负责 cloudflared 进程重启。额外自检只负责记录 ESP32 局域网可达性，不应因为外网或 ESP32 暂时离线就不断重启 N1。

### 11.1 自检脚本

```bash
sudoedit /usr/local/sbin/irrigation-gateway-health.sh
```

内容：

```sh
#!/bin/sh
set -eu

ESP_ORIGIN="${ESP_ORIGIN:-http://192.168.2.50}"

if ! systemctl is-active --quiet cloudflared; then
    logger -t irrigation-gateway "cloudflared inactive; requesting restart"
    systemctl restart cloudflared
fi

if curl --silent --show-error --output /dev/null \
        --connect-timeout 3 --max-time 5 \
        "${ESP_ORIGIN}/irrigation"; then
    touch /run/irrigation-esp-reachable
else
    rm -f /run/irrigation-esp-reachable
    logger -t irrigation-gateway "ESP32 origin unreachable: ${ESP_ORIGIN}"
fi
```

安装权限：

```bash
sudo chmod 0755 /usr/local/sbin/irrigation-gateway-health.sh
```

### 11.2 systemd service

```bash
sudoedit /etc/systemd/system/irrigation-gateway-health.service
```

```ini
[Unit]
Description=Irrigation gateway health check
After=network-online.target cloudflared.service

[Service]
Type=oneshot
Environment=ESP_ORIGIN=http://192.168.2.50
ExecStart=/usr/local/sbin/irrigation-gateway-health.sh
```

### 11.3 systemd timer

```bash
sudoedit /etc/systemd/system/irrigation-gateway-health.timer
```

```ini
[Unit]
Description=Run irrigation gateway health check periodically

[Timer]
OnBootSec=2min
OnUnitActiveSec=1min
AccuracySec=10s
Persistent=false

[Install]
WantedBy=timers.target
```

启用：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now irrigation-gateway-health.timer
systemctl list-timers irrigation-gateway-health.timer --no-pager
```

检查：

```bash
sudo systemctl start irrigation-gateway-health.service
systemctl status irrigation-gateway-health.service --no-pager
test -e /run/irrigation-esp-reachable && echo reachable || echo unreachable
journalctl -t irrigation-gateway -b --no-pager
```

注意：脚本中的 ESP32 IP 必须与路由器 DHCP 保留地址一致。

## 12. 远程维护

至少保留一种管理员远程维护方式：

- 通过第二个受 Cloudflare Access 保护的 SSH Tunnel。
- 或使用仅管理员设备可访问的独立 VPN。

SSH 要求：

- 仅允许公钥认证。
- 禁止 root 远程登录。
- 不向家庭成员开放。
- SSH Tunnel 和浇水网页使用不同的主机名及 Access 策略。

常用诊断命令：

```bash
uptime
free -h
df -h
ip address
ip route
timedatectl status
systemctl status cloudflared --no-pager
journalctl -u cloudflared -b --no-pager -n 200
curl --silent --show-error --output /dev/null --max-time 5 \
  http://192.168.2.50/irrigation
```

使用本文件的直接安装方式时，升级 cloudflared 必须在维护窗口重新下载官方 ARM64 包：

```bash
cd /tmp
curl --fail --location --output cloudflared.deb \
  https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64.deb
sudo dpkg -i cloudflared.deb
sudo systemctl restart cloudflared
cloudflared --version
```

升级后必须重新执行本文件第 14 节的关键测试。

## 13. 故障分层与老人操作

### 13.1 手机打不开，但本地自动浇水正常

老人操作：

1. 不操作浇水控制器。
2. 关闭“网络网关”电源。
3. 等待 10 秒。
4. 重新通电。
5. 等待 5 分钟。
6. 通知管理员再次测试。

管理员远程判断：

- Cloudflare Tunnel 是否在线。
- N1 是否可 SSH。
- ESP32 局域网是否可达。
- 家庭宽带是否恢复。

### 13.2 ESP32 本地也异常

按浇水控制器自己的安全和维护流程处理，不能把重启 N1 当成 ESP32 故障处理方式。

### 13.3 N1 重插电仍不恢复

可能原因：

- 电源适配器损坏。
- eMMC 或文件系统损坏。
- N1 硬件损坏。
- 路由器 LAN 口或网线故障。
- Cloudflare Token 被撤销。

此时应启用备用 N1，而不是让老人执行 Linux 修复命令。

## 14. 上线前验收

### 14.1 基础功能

- [ ] N1 确认从 eMMC 启动。
- [ ] N1 通过网线联网。
- [ ] ESP32 和 N1 均有 DHCP 保留地址。
- [ ] cloudflared 已启用并运行。
- [ ] 手机 Wi-Fi 和手机流量均可访问。
- [ ] 未授权邮箱被拒绝。
- [ ] 管理员远程维护入口可用。
- [ ] N1 故障不影响 ESP32 自动计划。

### 14.2 恢复测试

- [ ] 连续执行至少 20 次完整断电冷启动。
- [ ] N1 先启动、路由器 5 分钟后启动，Tunnel 能自动恢复。
- [ ] 宽带断开 1 小时后恢复，Tunnel 能自动恢复。
- [ ] 拔掉 N1 网线 10 分钟后插回，Tunnel 能自动恢复。
- [ ] 手动结束 cloudflared 进程，systemd 能自动拉起。
- [ ] ESP32 断电后恢复，N1 无需重启即可重新访问。
- [ ] N1 运行中直接断电，重新通电后文件系统和 Tunnel 正常。
- [ ] 连续运行至少 72 小时，无异常重启、内存持续增长或磁盘写满。

### 14.3 通过标准

- 正常网络恢复后 5 分钟内，无需登录和输入命令，域名重新可访问。
- 断网和重启期间，ESP32 自动计划与安全保护不依赖 N1。
- 系统没有重启循环。
- eMMC 无文件系统错误，日志和临时文件不会持续占满空间。
- 家庭成员无法进入系统设置和维护功能。

未通过任何一项关键恢复测试，不得部署到老家。

## 15. 备份与恢复

必须保存：

- Cloudflare Tunnel 名称和 UUID。
- 域名、Public Hostname 和 Access 策略说明。
- N1 软件包列表和 cloudflared 版本。
- systemd override、自检 service/timer 和脚本的无密钥副本。
- DHCP 保留地址和设备 MAC 地址。
- 可重新制作 N1 的镜像来源、设备树和安装说明。

不得保存到仓库：

- Tunnel Token。
- Tunnel 凭据 JSON。
- Cloudflare API Token。
- SSH 私钥。
- 路由器、ESP32 或家庭账号密码。

恢复优先顺序：

1. 更换已预配置的备用 N1。
2. 在新 N1 上安装同版本系统和 cloudflared。
3. 从 Cloudflare 控制台重新生成或轮换 Token。
4. 恢复 systemd 和自检配置。
5. 完成基础功能和断网恢复测试。

## 16. 实施阶段

### 阶段 A：桌面验证

- N1 从临时介质启动。
- 验证有线网卡、重启、时间同步和 cloudflared。
- 只允许管理员访问。

### 阶段 B：无人值守固化

- 确认镜像后写入 eMMC。
- 配置低写盘、systemd 自动恢复、自检和远程维护。
- 完成 20 次冷启动和 72 小时稳定性测试。

### 阶段 C：家庭页面

- ESP32 增加受限家庭页面和接口。
- Cloudflare Access 加入家庭成员。
- 用真实手机流量验证。

### 阶段 D：老家部署

- 配置现场 DHCP 保留地址。
- 固定网线、电源和散热。
- 张贴唯一的老人操作说明。
- 在现场完成断电、断网和恢复验收。

只有前一阶段验收通过后才能进入下一阶段。
