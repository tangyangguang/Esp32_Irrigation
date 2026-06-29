# Web/API 功能验证清单

本清单用于当前 2 Flow / 6 Zone 固件的软件验证。实机验证必须另行记录。

## 页面路由

- `/irrigation`：只展示 enabled Zone 的手动启动/停止入口，disabled Zone 不出现在首页操作区。
- `/irrigation/zones`：显示 Zone 1..6，可启用/禁用、改名、配置 Flow 归属，Zone 基线保存为 pending 后再 apply。
- `/irrigation/flows`：显示 Flow 1/2 的 GPIO、active/pending K+Offset 参数，手工编辑只保存 pending。
- `/irrigation/plans`：显示 Zone 1..6、每 Zone 6 个计划槽，可保存或删除计划。
- `/irrigation/calibration`：Flow K+Offset 采样、停止、提交实际水量、保存推荐 pending。
- `/irrigation/settings`：系统级时长、队列、待机漏水窗口和阈值。
- `/irrigation/records`：显示最近浇水记录。
- `/irrigation/events`：显示业务事件，并链接到 Esp32Base App Events。

## API 路由

- `GET /api/v1/status`
- `GET /api/v1/plans`
- `POST /api/v1/plans/save`
- `POST /api/v1/plans/delete`
- `GET /api/v1/records`
- `GET /api/v1/events`
- `POST /api/v1/manual/start`
- `POST /api/v1/manual/stop`
- `POST /api/v1/manual/stop-all`
- `POST /api/v1/zones/config`
- `POST /api/v1/zones/baseline/pending/save`
- `POST /api/v1/zones/baseline/pending/apply`
- `POST /api/v1/zones/baseline/rollback/restore`
- `POST /api/v1/flows/config`
- `POST /api/v1/calibration/start`
- `POST /api/v1/calibration/stop`
- `POST /api/v1/calibration/sample`
- `POST /api/v1/calibration/pending/save`
- `POST /api/v1/calibration/pending/apply`
- `POST /api/v1/calibration/rollback/restore`
- `GET /api/v1/config`
- `POST /api/v1/config`

## 服务端约束

- 所有状态改变页面表单必须为 POST，并包含 JavaScript `confirm`。
- disabled Zone 不能启动。
- enabled Zone 必须归属 enabled Flow。
- running Zone 不能改配置、保存基线、apply/rollback 基线。
- 禁用 Flow 时，如果仍有 enabled Zone 归属该 Flow，必须拒绝。
- Flow K+Offset apply/rollback 时，目标 Flow 必须空闲。
- 同一 Flow 下 Zone 互斥；不同 Flow 下可并行。
- Flow 待机漏水保护只阻断归属该 Flow 的 Zone。
- 计划启用时，如果 Zone disabled 或 Flow disabled，必须拒绝。
- JSON 输出做 JSON escape；HTML 输出做 HTML escape。

## 软件验证命令

```bash
node scripts/check-web-structure.mjs
pio run
```

## 未实机验证

- 真实浏览器逐页点击和表单提交。
- 真实 ESP32 上的登录、鉴权、POST 确认和路由容量。
- 真实流量计、电磁阀、LCD、本地按钮和 72 小时稳定运行。
