# MPU6050 / MPU6500-compatible 备用 IMU 硬件 Spike

状态：`bench_passed / pairwise_passed`。双 Profile、invalid 快照修复、
全量构建/烧录、两块 600 秒静止诊断、OLED 共享总线、软件故障恢复、`WHO_AM_I=0x70` 的
120 秒联合回归，以及 2026-07-16 的 27000 秒无人值守静态 soak 通过；三轴转动、算法补偿、
六面标定、物理断开恢复、示波器上升沿与 ICM42688 均 deferred。

本工作只验证备用 MPU6050 的总线、采样、静止偏置和低延迟数据链。它不是正式 Phase 2B，
也不改变正式底盘 IMU 仍为 ICM42688 的决定。验证后的接口只能在 Phase 2A 验收后语义移植，
不能直接把本分支当作正式后续阶段合入。

用户要求完成本次稳定性收尾后恢复独立算法工作树。本 spike 不创建阶段 tag、不合入正式 main；
算法阶段必须重新采集标准化数据，不直接用本次临时面包板数据冻结补偿参数。
checkpoint `ff207b5` 已推送到 `origin/codex/mpu6050-hardware-spike`。

## 1. 接线

| MPU6050 | 天猛星 MSPM0G3507 | 说明 |
| --- | --- | --- |
| VCC | 3V3 | 禁止使用 5 V 逻辑电平 |
| GND | GND | 与 MCU 共地 |
| SCL | PA1 / I2C0 SCL | 与 OLED 共用硬件 I2C0 |
| SDA | PA0 / I2C0 SDA | 与 OLED 共用硬件 I2C0 |

- 当前地址固定为 `0x68`，对应 AD0 为低。已实测两块模块：第一块 `WHO_AM_I=0x70`，按
  MPU6500-compatible 识别；第二块 `WHO_AM_I=0x68`，按标准 MPU6050 识别。2026-07-16
  无人值守稳定性测试接线为第一块 `WHO_AM_I=0x70`。
- `INT`、`XDA`、`XCL` 本次不接。正式闭环 IMU 应优先使用数据就绪中断验证采样时刻。
- 天猛星 `H8` 是 SPI-LCD 接口，本 spike 不使用 `PB8/PB9`，不会占用 Phase 2A 电机引脚。
- OLED 为 `0x3C/0x3D`，MPU6050 为 `0x68`，地址不冲突；并联上拉强度仍需示波器或上升沿实测。

## 2. 软件边界

```text
ServiceTask -> ImuService -> module/device/mpu6050 -> BSP I2C -> I2C0 PA0/PA1
SystemTask  -> 只读 ImuServiceSnapshot -> 100 Hz telemetry
DisplayTask -> SSD1306 -> 同一 BSP I2C mutex
```

- 不新增 IMU Task；`ServiceTask` 是 IMU 快照唯一写入者。
- `SystemTask` 不访问 I2C，只复制最近快照，控制周期不会等待传感器总线。
- ServiceTask 只在 ImuService 真正需要访问总线时非阻塞申请短优先 quiet window；失败时延后
  2 ms，不等待 UART。UART DMA 只由 ServiceTask 启动，避免 OLED backlog 抢在 IMU 前发送。
- BSP 新增两个都有明确 STOP、各有 3 ms 超时的硬件寄存器 `write-read`，最大读取 16 字节。
- 连续 3 次采样失败后回到 1 秒周期重新探测，不无限阻塞。
- App 不调用 `DL_*`，DriverLib 仍只位于 BSP。

## 3. MPU6050 配置

| 项目 | 配置 |
| --- | --- |
| 时钟 | X 轴陀螺 PLL |
| 采样率 | 100 Hz |
| 芯片 DLPF | 42 Hz |
| 陀螺量程 | +/-500 dps，65.5 LSB/dps |
| 加速度量程 | +/-4 g，8192 LSB/g |
| FIFO / DMP | 本 spike 禁用 |
| 软件滤波 | 100 Hz 下约 25 Hz 一阶低通，保留原始和补偿后数据 |

设备驱动只有一套，Profile version 为 1：

| `WHO_AM_I` | Profile | 温度换算 | 启动静止门限 |
| --- | --- | --- | --- |
| `0x68` | MPU6050 | `raw/340 + 36.53` | `1310 counts`，约 20 dps |
| `0x70` | MPU6500-COMPAT | `raw/333.87 + 21` | `655 counts`，约 10 dps |

DMP 不作为底盘或云台闭环的基础。正式控制链应使用带明确时间戳的原始六轴数据、经过验证的
偏置/温漂模型和独立姿态估计器。

## 4. 零漂处理

启动后按以下状态推进：

```text
PROBE -> RESET_WAIT(100 ms) -> SETTLING(500 ms)
-> CALIBRATING(连续静止 300 点，约 3 s) -> READY
```

校准使用当前 Profile 的陀螺绝对门限，并统一要求加速度模长在 0.85-1.15 g；检测到移动会
重新累计，避免把真实运动学成零偏。标准 MPU6050 样品的 X 轴原始静止零偏约 `12.715 dps`，
因此不能沿用 `0x70` 样品约 10 dps 的绝对门限。当前没有自动在线偏置跟踪，因为慢速真实偏航
与零漂无法仅靠六轴 IMU 可靠区分。温度和校准温度均保留在快照中，温漂模型必须由后续多温点
实测建立。

## 5. 遥测语义

Spike 期间保留 100 Hz Control 帧格式，但四个浮点量映射为：

| 字段 | 含义 |
| --- | --- |
| setpoint | 滤波后 X 轴角速度，dps |
| measurement | 滤波后 Y 轴角速度，dps |
| control_output | 滤波后 Z 轴角速度，dps |
| auxiliary | 三轴加速度模长，g |

flags 报告 MPU6050、数据有效、校准中和 READY。完整原始值、偏置、补偿值、温度和状态可从
`g_imu_service_snapshot` 与 `g_imu_service_diag` 读取。

## 6. 验收顺序

1. 确认开发板和 MPU6050 无异常发热，VCC=3.3 V，SCL/SDA 未接反。
2. 烧录后保持板静止至少 4 秒，确认 `WHO_AM_I=0x68` 或 `0x70` 命中受支持 Profile，状态进入 READY。
3. 运行 `tools/mpu6050_field_check.ps1 -Mode Static`，检查 100 Hz、CRC/gap/deadline、I2C error、
   三轴均值/标准差和加速度模长。
4. 运行 `-Mode Motion`，依次绕三个轴转动，检查符号、动态范围、响应和恢复。
5. OLED 同时在线运行，完成断开 MPU、SDA/SCL 故障和重新连接恢复测试。
6. 完成至少 120 秒无断点连续运行；本 spike 不创建阶段 tag，也不合入正式 main。

## 7. 当前证据

| 层级 | 结果 |
| --- | --- |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning |
| App size | Code=57220，RO=3068，RW=28，ZI=16236 |
| SysConfig | 0 error；1 条既有 ProjectConfig warning |
| 烧录 | passed；新 build `0x02F0` 已运行 |
| I2C/WHO_AM_I | `0x68` 地址 ACK，`WHO_AM_I=0x70`，MPU6500-compatible |
| OLED 共线短采集 | 6 秒 100/1 Hz，CRC/gap/deadline/I2C error 全部 0，OLED online |
| 六轴采样/READY | 100 Hz，20 秒 2036/2036 个 Control 帧均为 valid + READY |
| UART/I2C 联合回归 | 20 秒 Control/Health 2036/20，100/1 Hz，CRC/gap/deadline/I2C error 全 0 |
| Health/quiet | active/sticky 0/0，quiet acquire/release 4644/4644，active 0 |
| 候选静止 X/Y/Z | mean `0.004001/-0.002193/-0.000336 dps`；stddev `0.031910/0.033893/0.042057 dps` |
| 候选加速度模长 | mean `1.011909 g`；stddev `0.003862 g` |
| 600 秒连续运行 | Control/Health `60961/610`，100/1 Hz，CRC/gap/deadline/I2C error、active/sticky 全 0 |
| 600 秒 X/Y/Z | mean `-0.012977/0.005852/0.010664 dps`；stddev `0.030266/0.030946/0.033636 dps` |
| 600 秒加速度模长 | mean `1.019505 g`；stddev `0.003692 g` |
| 首分钟 -> 末分钟 Z 均值 | `0.000153 -> 0.023218 dps`，存在小幅上电热稳定漂移 |
| 第二块识别 | `0x68` 地址 ACK，`WHO_AM_I=0x68`，标准 MPU6050 Profile 1 |
| 第二块 600 秒连续运行 | 主机 `607.127 s`、设备 `609.600 s`；Control/Health `60961/609`，100/1 Hz，全部门禁为 0 |
| 第二块 600 秒 X/Y/Z | mean `-0.007652/0.006010/-0.002115 dps`；stddev `0.023553/0.023628/0.020675 dps` |
| 第二块加速度模长 | mean `1.083473 g`；stddev `0.002243 g`，存在约 `+8.35%` 比例偏差 |
| 第二块首 60 秒 -> 末 60 秒 Z 均值 | `-0.002876 -> -0.002605 dps`；纯积分总体约 `-1.3 deg/10 min` |
| 软件故障恢复 | READY 后 3 次强制采样失败；111 帧 INVALID 三轴为 0，重新探测/校准后恢复 READY |
| 软件故障状态序列 | CALIBRATING -> READY -> INVALID -> VALID_NOT_READY -> CALIBRATING -> READY |
| 软件故障链路 | 3052/31 Control/Health，100/1 Hz，CRC/gap/out-of-order/deadline 0，最终 active 0 |
| `0x70` 最终 120 秒回归 | 主机 122.587 s，12195/122 Control/Health，全部门禁为 0 |
| `0x70` 120 秒 X/Y/Z | mean `-0.004778/-0.000418/0.004142 dps`；stddev `0.029737/0.030829/0.033293 dps` |
| 2026-07-16 full rebuild | FreeRTOS/App 0 Error / 0 Warning；HEX SHA-256 未变化 |
| 初始 I2C 瞬态 | 两次 20 秒检查 `I2C error=270/61`；保留现场，后续 30/60 秒恢复全 0 |
| 五次 reset 回归 | 五次 20 秒 Static 均通过；Control `2035-2036`、Health `20-21`，全部门禁为 0 |
| 协议压力后复核 | 30 秒 `3053/31`，`ParameterErrorCount=7`、`ApplySequence=53`，链路/Health 全 0 |
| 15 分钟基线 | `91453/915` Control/Health，全部 Control READY，全部门禁为 0 |
| 27000 秒无人值守 soak | 30 个 900 秒段，`2743306/27432` Control/Health，全部 Control READY，全部门禁为 0 |
| soak 资源低水位 | stack 94 words、heap 3056 B、ring 464 B、quiet max 38777 us，OLED online |
| reset 后最终 20 秒 | `2032/20` Control/Health，100/1 Hz，参数 sequence/error 0/0，全部门禁为 0 |
| 11:14 再次 reset 后 20 秒 | `2038/20`，全部 READY，所有门禁 0，X/Y/Z mean `-0.001662/0.006787/-0.002731 dps` |
| 30 段加权 X/Y/Z | mean `-0.056074762/0.013271149/0.025727453 dps`；stddev `0.047037499/0.037879724/0.034651981 dps` |
| 30 段加速度模长 | `1.018024726 +/- 0.003618586 g` |
| 物理断开/恢复 | 面包板只能四针整体拔插，等待独立插头/跳帽/串联开关后执行 |
| 转动方向 | Motion 和轴方向冻结仍未执行 |

双 Profile 修改后换回第一块并执行 DAPLink `reset run`，20 秒短回归为 Control/Health
`2036/20`、100/1 Hz，CRC/gap/deadline/drop/I2C/Health/quiet 全 0；X/Y/Z mean
`-0.001053/0.001467/0.008661 dps`，stddev `0.030055/0.031623/0.034462 dps`。因此 `0x70`
Profile 短路径已回归，但尚不能替代至少 120 秒联合回归。

invalid 快照修复后的第一块 120 秒联合回归已通过，因此上述短回归限制已经关闭。最终正常固件
HEX SHA-256 为 `66A1263E853BB9217399A2FA605D0D48822C219EB276EB57060E34F72783A328`。

换模块但未复位 MCU 时还得到一份有效故障证据：I2C error 累计到 `295`，OLED 与 IMU offline，
Health active/sticky 为 `0x00009800`，但 Control 帧仍重复旧快照并保留 valid/READY flags。
离线后的 stale 快照现会立即替换为 invalid 快照。自动软件故障注入证明 READY 清除、三轴归零、
重新探测、重新校准和 READY 恢复均有效。该结果不等于物理断线通过；当前面包板只能四针整体
拔插，待安全的独立断开点准备后再补真实 I2C 断开恢复。

## 8. UART/I2C 并发根因

原始串口字节显示，失败帧不是随机 CRC 算错，而是 Control 帧尾部 1-2 个 CRC 字节被截断，
下一帧同步字 `A5 5A` 直接顶到 CRC 位置。关闭 IMU I2C 的 10 秒 A/B 为 100/1 Hz、CRC/gap 0，
证明 MPU 数据和 UART-only 路径正常。Phase 1E 已记录同一临时接线下 I2C/UART 并发问题，因此
本 spike 复用 quiet window，并增加以下约束：

- `DMA_DONE_TX` 后等待 `EOT_DONE` 才结算物理发送完成；
- UART DMA 启动只由 2 ms ServiceTask 执行；
- IMU 100 Hz 采样错开 Control 帧，并在 I2C 事务期间使用非阻塞短优先 quiet window；
- OLED 长 quiet 释放后，先允许到期 IMU 采样，再发送积压遥测。

该方案恢复了 OLED + IMU + 100 Hz UART 的 20 秒全 0 回归。正式整车仍应使用短线、可靠共地、
核实上拉和去耦，软件隔离不能替代硬件信号完整性。

两块 600 秒诊断都保持 CRC/gap/deadline/I2C error、Health active/sticky 全 0。第一块三轴噪声
标准差约 `0.03 dps`，Z 均值 `0.010664 dps` 对应纯积分约 `6.4 deg/10 min`；第二块噪声更低，
Z 均值 `-0.002115 dps` 对应约 `-1.3 deg/10 min`，且首末 60 秒 Z 均值只变化
`+0.000271 dps`。不过第二块加速度模长约 `1.083 g`，正式姿态使用前仍需六面标定。
正式航向闭环不能只依赖六轴陀螺积分，需要温度补偿、轮速/视觉/其他航向观测中的至少一种
长期校正。

## 9. 2026-07-16 无人值守稳定性结论

证据根目录为
`C:\Users\Auror\AppData\Local\Temp\echo-unattended-soak-20260716-overnight`。计划任务先运行
25200 秒主体并生成 28 个通过段；为满足 27000 秒目标，在不复位 MCU 的情况下追加独立 1800 秒、
2 个通过段。两部分累计 `2743306` 个 Control 和 `27432` 个 Health，所有 Control 均 valid+READY，
CRC、sequence gap、duplicate、out-of-order、deadline、I2C error、Health active/sticky 均为 0。

两次 runner 主机墙钟合计 `27573.452 s`。30 段跨段 sequence 与 uptime 单调，段 2、7、11、16、
21、25 和补充段 2 共 7 次 `uint32_t` 微秒时间戳回绕均保持周期连续。8 个段在重新打开串口时
跳过总计 212 B 前导非同步字节，单次最多 48 B；没有伴随 CRC 或 sequence gap，属于捕获边界。

加权 X/Y/Z mean 为 `-0.056074762/0.013271149/0.025727453 dps`，总体标准差为
`0.047037499/0.037879724/0.034651981 dps`；加速度模长
`1.018024726 +/- 0.003618586 g`。这些数据没有同步温度和原始六轴，只能支持候选慢漂判断，
不能拟合温补、六面标定或姿态估计器。

初始 I2C 瞬态的失败目录为 `echo-mpu6050-20260716-015609` 和
`echo-mpu6050-20260716-015731`；后续恢复、五次 reset、协议压力后复核和 15 分钟基线路径见
`docs/worklogs/2026-07-16_mpu6050_unattended_stability.md`。长测通过只能关闭当前接线、当前固件、
静止条件下的连续运行门禁；Motion、真实物理断开/恢复和示波器上升沿仍为明确 deferred。

六轴 IMU 的 yaw 在没有磁力计、轮速、视觉或其他外部航向观测时不可长期观测；四元数、
Mahony/Madgwick 或 KF/EKF 不能单独消除该物理限制。
