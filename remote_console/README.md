# 灌溉远程控制台

这是运行在 Mac 上的本地网页控制台。浏览器只访问本机 `127.0.0.1`，本地服务通过 TLS 连接公网 MQTT Broker，不需要 Home Assistant。

## 使用

1. 首次使用双击 `首次安装.command`。
2. 双击 `启动灌溉控制台.command`。
3. 浏览器会自动打开 `http://127.0.0.1:8787/`。
4. 保持启动控制台的终端窗口开启；关闭终端即停止本地服务。

也可以在终端运行：

```sh
cd remote_console
npm install
npm start
```

## 配置

非敏感配置在 `config.json`：

- `deviceId`：ESP32 的设备 ID。
- `listenPort`：本地网页端口，默认 `8787`。
- `commandTimeoutMs`：等待设备确认命令的最长时间。

MQTT 地址、TLS CA 和凭据继续读取 `firmware/platformio.local.ini`，不会复制到网页或仓库。当前可临时复用设备账号；正式长期使用建议在该文件增加：

```ini
custom_irrigation_console_mqtt_username = 控制台专用用户名
custom_irrigation_console_mqtt_password = 控制台专用密码
```

控制台专用账号只应具有目标设备主题的订阅权限，以及 `irrigation/{deviceId}/command` 的发布权限。私密配置与证书已由项目 `.gitignore` 排除。

## 安全边界

- HTTP 服务只监听 `127.0.0.1`，同一局域网的其他设备不能直接访问。
- 每次启动生成随机页面会话令牌，写操作还校验来源。
- MQTT 使用 TLS 并校验服务器证书。
- 控制命令使用 QoS 1，并以设备 `result` 应答为最终结果。
- 设备离线或业务未就绪时，控制操作会被锁定。

## 测试

```sh
npm test
node --check src/server.js
node --check web/app.js
```
