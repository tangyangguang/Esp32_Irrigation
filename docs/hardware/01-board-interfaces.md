# 软件可见硬件接口边界

## 目标

本文件只描述会影响整体方案和软件设计的硬件能力边界，不展开 PCB 设计、电路保护、器件选型或布线细节。

当前控制板对软件暴露的能力：

```text
ESP32 主控
1 路外部 RTC，带备用电池
1 路流量计输入
6 路 12V DC 电磁阀控制输出
1 路可选自吸泵启动输出
1 路可选低液位输入
```

硬件实现细节如 MOS 管型号、续流/钳位、EMI、保险、端子、布线和散热，属于电路板设计范畴，不在本软件方案中深入设计。

## 推荐 ESP32 引脚对应表

当前推荐表面向 classic ESP32 Dev Module / ESP32-D0WD 系列早期开发和 PCB 初版走线。它不是最终 PCB 定稿；后续如果 PCB 引脚变化，固件优先只修改 `firmware/src/irrigation/IrrigationPinMap.h`。

设计依据：

```text
1. 避开 GPIO6..11：这些脚通常连接外部 SPI Flash，不作为业务 IO。
2. 避开下载串口 GPIO1/GPIO3：保留串口烧录和启动日志。
3. 避开启动绑带脚 GPIO0/GPIO2/GPIO4/GPIO5/GPIO12/GPIO15：避免外部负载影响启动模式。
4. GPIO34..39 只作为输入；不能用于阀门、泵等输出。
5. 阀门保持 PWM 使用 ESP32 LEDC；输出脚选择普通输出 GPIO，后续可统一接 LEDC 通道。
6. FLOW_IN 和 LOW_LEVEL_IN 使用输入专用脚时，PCB 必须提供外部上拉/滤波，不能依赖内部上拉。
```

推荐引脚：

| 软件信号 | 推荐 GPIO | 方向 | 默认电气语义 | 说明 |
| --- | ---: | --- | --- | --- |
| `FLOW_IN` | GPIO34 | 输入 | 脉冲输入，高/低按流量计输出定义 | 输入专用脚；建议外部上拉到 3.3V，并做必要限流/滤波/电平保护。 |
| `LOW_LEVEL_IN` | GPIO35 | 输入 | 低液位或断线为高电平 | 输入专用脚；按常闭浮球到 GND 设计，PCB 外部上拉到 3.3V。 |
| `VALVE_OUT_1` | GPIO25 | 输出 PWM | 高电平驱动对应 MOS 管输入 | Zone 1 默认启用。 |
| `VALVE_OUT_2` | GPIO26 | 输出 PWM | 高电平驱动对应 MOS 管输入 | Zone 2 默认禁用。 |
| `VALVE_OUT_3` | GPIO27 | 输出 PWM | 高电平驱动对应 MOS 管输入 | Zone 3 默认禁用。 |
| `VALVE_OUT_4` | GPIO14 | 输出 PWM | 高电平驱动对应 MOS 管输入 | Zone 4 默认禁用。 |
| `VALVE_OUT_5` | GPIO13 | 输出 PWM | 高电平驱动对应 MOS 管输入 | Zone 5 默认禁用。 |
| `VALVE_OUT_6` | GPIO23 | 输出 PWM | 高电平驱动对应 MOS 管输入 | Zone 6 默认禁用。 |
| `PUMP_START_OUT` | GPIO32 | 输出 | 高电平表示启动外部泵控制模块 | 可选，默认配置关闭。 |
| `RTC_SDA` | GPIO21 | I2C | 3.3V I2C | 外部 RTC 数据线，可与后续低速 I2C 外设共用总线。 |
| `RTC_SCL` | GPIO22 | I2C | 3.3V I2C | 外部 RTC 时钟线，可与后续低速 I2C 外设共用总线。 |
| `RTC_INT` | GPIO33 | 输入 | RTC 告警低有效 | 建议 PCB 连接并可通过 0 欧电阻或焊桥断开；第一版固件不依赖此信号。 |

暂时保留：

```text
GPIO16 / GPIO17：可作为后续本地 UI、扩展输出或调试 IO。
GPIO18 / GPIO19：优先保留给可能的 SPI 扩展。
GPIO21 / GPIO22：作为 I2C 总线使用；RTC 是默认挂载设备，后续 I2C 屏幕或扩展需共用地址不冲突。
GPIO36 / GPIO39：备用输入；同样需要外部上拉/下拉。
```

如果最终 PCB IO 紧张，可以重新评估 GPIO16、17、18、19、21、22、33 的用途。不要优先占用 Flash、下载串口和启动绑带脚。

## 外部 RTC

本项目把外部 RTC 作为控制板必配硬件，推荐 DS3231 / DS3231M 等带备用电池、I2C 接口和较高精度的 RTC。

原因：

```text
灌溉控制器停电期间不能浇水，但来电后必须知道真实日期时间。
ESP32 可通过 NTP 获取可信时间，但断网或路由器未恢复时不能依赖网络校时。
ESP32 内部 RTC timer 在 power-on reset 后会复位，不能替代带电池的外部 RTC。
```

软件语义：

```text
RTC 只负责保存真实日期时间。
RTC 不直接控制电磁阀、泵或运行状态机。
自动计划仍由 ESP32 软件每分钟检查系统时间和计划配置后触发。
```

推荐上电时间策略：

```text
1. ESP32 上电后先关闭所有阀门和泵输出。
2. 读取 RTC 时间；如果 RTC 时间有效，立即设置系统时间。
3. 如果网络可用，NTP 校时成功后更新系统时间并回写 RTC。
4. 如果网络不可用但 RTC 有效，自动计划继续按 RTC 提供的系统时间运行。
5. 如果 RTC 无效且 NTP 也不可用，系统时间无效，自动计划不可用，手动浇水仍可按安全条件执行。
6. 停电期间错过的计划不补跑；来电后只执行后续按当前时间应触发的计划。
```

`RTC_INT` 结论：

```text
PCB 建议接到 ESP32 一个普通输入 GPIO，默认推荐 GPIO33。
第一版固件不依赖 RTC_INT 做浇水定时，也不要求 RTC alarm 唤醒。
RTC_INT 仅作为后续低功耗睡眠唤醒、硬件告警或诊断扩展预留。
```

电气要求：

```text
RTC I2C 与 ESP32 之间使用 3.3V 电平。
RTC_INT / SQW 通常是开漏输出，应上拉到 3.3V，不能上拉到 5V 后直接接 ESP32。
建议 RTC_INT 通过 0 欧电阻、焊桥或测试点保留可断开能力。
不用 RTC_INT 时，固件把对应 GPIO 配置为输入，不启用内部强驱动。
```

参考：

```text
https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html
https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf
https://cdn.sparkfun.com/datasheets/Dev/Beagle/DS3231M.pdf
```

## 12V DC 电磁阀输出

软件假设电磁阀类型固定为：

```text
12V DC 常闭电磁阀
```

控制板提供 6 路电磁阀控制输出。软件按 6 个 Zone 建模，但现场不一定接满 6 路，因此每个 Zone 必须支持启用/禁用。

硬件层计划使用 N 沟道 MOS 管驱动，但具体驱动电路不在本文件展开。

## 吸合/保持 PWM

软件需要支持吸合/保持控制：

```text
吸合阶段：100% 全功率，保证阀可靠打开
保持阶段：PWM 降低平均电流，减少线圈发热和整机功耗
```

默认系统级参数：

```text
valvePullInMs = 3000
valveHoldDutyPercent = 60
valvePwmFrequencyHz = 20000
```

`valveHoldDutyPercent = 100` 表示禁用保持阶段降功率 PWM，阀门全程全功率输出。

这些参数不按 Zone 单独配置，避免过度设计。实际目标阀门确定后必须测试：

```text
最低可靠吸合时间
最低可靠保持占空比
不同频率下的噪声、温升和保持力
长时间运行后是否掉阀
断电/停止后关阀是否足够快
```

20kHz 的默认依据是避开人耳可闻范围，减少低频嗡鸣。TI 关于电磁阀驱动的资料也说明 PWM 可用于降低保持电流和发热，DRV103 这类驱动器支持 500Hz 到 100kHz 的频率范围；TI 工程支持中也提到通常建议高于 20kHz 以避开可闻噪声，但频率越高可能降低有效阀力，需要实测权衡。

如果目标阀门在 20kHz 下保持不稳，可降到 5kHz、1kHz 或按阀门厂家建议调整；如果低频噪声明显，再提高频率。

参考：

```text
https://www.ti.com/lit/slvae59
https://www.ti.com/lit/gpn/drv103
https://e2e.ti.com/support/motor-drivers-group/motor-drivers/f/motor-drivers-forum/1277752/drv8803-recommended-pwm-frequency-to-drive-solenoid-valve
```

## Zone 启用/禁用

虽然硬件固定提供 6 路输出，软件必须支持 Zone 启用/禁用。未接电磁阀的输出应保持禁用，避免误启动空水路。

默认：

```text
Zone 1 启用
Zone 2..6 禁用
```

## 泵启动输出

`PUMP_START_OUT` 只提供启动控制信号，不直接承载泵主电流。软件只把它作为可选开关输出：

```text
pumpStart.enabled = false by default
运行 Zone 时按策略打开
停止、故障、重启时关闭
```

泵主回路、继电器、接触器或外部泵控制模块属于硬件设计范畴。

## 低液位输入

`LOW_LEVEL_IN` 可选，推荐支持两线制浮球液位开关：

```text
水位正常：触点闭合，输入为低电平
低液位或线断：触点断开，输入为高电平
```

软件默认不启用低液位输入。启用后，低液位会阻止启动或触发停机。

## 流量计输入

`FLOW_IN` 是唯一且必配的脉冲输入。软件依赖它判断：

```text
启动后是否实际出水
运行中是否无水、低流量、高流量
当前 Zone 的估算浇水量
待机时是否存在异常流量
```

没有有效流量计时，设备不满足当前产品目标。

## 外部 FRAM

当前软件方案不要求外部 FRAM。

原因：

```text
配置写入低频，使用 Esp32BaseConfig / ESP32 NVS
浇水记录只在每次 Zone 结束后写入
事件日志只记录故障、警告和重要状态变化
流量计脉冲和实时流速运行中保存在 RAM
系统不支持掉电后恢复未完成浇水
```

FRAM 适合高频掉电安全写入，例如每秒记录、每个脉冲记录、断电前精确保存运行进度或长期传感器采样。本项目当前不做这些功能，因此不建议为了当前软件方案增加 FRAM。

如果 PCB 设计希望保留实验余量，可以预留 FRAM 焊盘或总线扩展位，但当前固件不把 FRAM 作为必需硬件，也不围绕 FRAM 设计数据模型。
