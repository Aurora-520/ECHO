# Phase 2A: AT8236、底盘电机与 GMR 编码器

状态：进行中。左右编码器无动力板测已通过；AT8236 默认零输出、UART 一次性点动安全层
已完成 0/0 构建、烧录回读和四方向无动力逻辑 PWM 板测。物理引脚波形、带 VM 单轮点动、
电机方向、故障测试和连续运行尚未完成。

## 1. 范围与边界

Phase 2A 只包含：

- D153B 双路 AT8236 模块；
- 370 与后续 513X 可切换的电机配置；
- 左右轮 GMR AB 相编码器；
- 默认零输出、唯一执行器写入者、命令超时和方向归一化；
- 单电机架空板测、故障测试和连续运行证据。

本阶段不包含速度 PID、底盘 IMU、航向/位置闭环、云台、视觉或 Mission。

## 2. 已确认硬件事实

| 项目 | 当前事实 |
| --- | --- |
| 开发电机 | MG370，GMR 编码器，12 V，减速比 1:34.014 |
| 370 转速 | 空载 300+/-12% rpm，额定 260+/-12% rpm |
| 370 电流 | 空载 0.24 A，额定 1.1 A，堵转 6.2 A |
| 最终电机 | 513X，尚未到货，完整参数未冻结 |
| 驱动模块 | D153B，AT8236 双 H 桥，VREF=3.3 V |
| 编码器标称 | 500 PPR；手转量级支持减速前编码器轴口径，精确 CPR 仍待标记盘复测 |
| 编码器电平 | 用户确认并接线为 3.3 V，左轮 PA29/PA30 板测通过 |
| 机械参数 | 轮径、轮距、左右安装朝向尚未冻结 |

D153B 的 E1A/E1B/E2A/E2B 从电机接口直接引出，没有电平转换。电机接口给编码器提供 5 V，
而 MSPM0G3507 的 PA29/PA30、PB6/PB7 是 Standard I/O，不是 5 V tolerant。确认信号高电平或
加 5 V 到 3.3 V 电平转换前，禁止把编码器相线直接接入 MCU。

### 2.1 编译时 Motor Profile

MG370 与未来 513X 共用一套 `bsp_motor`、`bsp_encoder`、ChassisActuator、转速换算和后续 PID
接口。唯一型号选择入口是 `module/service/motor_profile_config.h` 中的：

```c
#define ECHO_MOTOR_PROFILE_SELECTION ECHO_MOTOR_PROFILE_MG370
```

不允许 OLED、UART 或任务状态机在运行时切换电机型号。Profile 只保存电机与每轮语义，不包含
或改变 PB8/PB9/PB12/PB13、PA29/PA30、PB6/PB7 等固定板级引脚。

MG370 Profile version 1 当前内容：

| 字段 | 值 | 状态 |
| --- | ---: | --- |
| 额定电压 | 12,000 mV | 已确认 |
| 额定电流 | 1,100 mA | 已确认 |
| 堵转电流 | 6,200 mA | 电机规格已确认；保护阈值仍未冻结 |
| 减速比 | 34.014 | 已确认 |
| 编码器接口/电平 | GMR AB / 3,300 mV | 当前硬件已确认 |
| 编码器 PPR | 500 | 已确认口径仍需最终数据手册复核 |
| 最高输出转速 | 300 rpm | 当前空载规格 |
| 左轮 CPR / 编码符号 / 解码 | 68,028 / `+1` / x4 | CPR provisional |
| 右轮 CPR / 编码符号 / 解码 | 17,007 / `-1` / x1 | CPR provisional |
| 左右 `motor_output_sign` | 0 / 0 | 待带动力方向测试，不允许猜测 |
| 起转 PWM / 运行最大 PWM | 0 / 0 | 未确认，有效位未置位 |
| 速度/加速度限制 | 0 / 0 | 未确认，有效位未置位 |
| 堵转阈值/持续时间 | 0 / 0 | 未确认，有效位未置位 |
| 速度 PID | 全 0 | 未调参，有效位未置位 |

Profile 因此允许现有最高 10%/500 ms 的显式电气安全点动，但 `closed_loop_ready=0`；未来归一化
电机命令在电机方向、最大 PWM、速度/加速度、堵转判据和 PID 全部冻结前保持锁定。

513X 已建立同结构占位配置。若编译选择 `ECHO_MOTOR_PROFILE_513X`，当前会直接产生明确
`#error`，要求先确认额定电压、堵转电流、减速比、编码器接口、电平和 PPR。

诊断全局 `g_motor_profile_diag` 报告型号名、schema/profile version、有效字段、状态标志、左右
CPR/符号/倍频和 100 Hz 输出轴 RPM。Telemetry frame type 7 每秒报告当前 Profile 型号和版本。

## 3. 固定物理接线

以下接线由用户确认，后续不通过交换电机线或 A/B 相来修正软件方向：

| 车轮 | D153B 通道 | 电机正端 | 电机负端 | 编码器 A 相 | 编码器 B 相 |
| --- | --- | --- | --- | --- | --- |
| 左轮 | Motor A | AOUT1 / AO1 | AOUT2 / AO2 | E1A | E1B |
| 右轮 | Motor B | BOUT1 / BO1 | BOUT2 / BO2 | E2A | E2B |

统一语义是“整车向车头方向前进时，左右轮速度均为正”。左右镜像安装造成的差异只允许出现在
每轮唯一的 `motor_output_sign` 和 `encoder_count_sign` 配置中。

## 4. Phase 2A 引脚规划

| 功能 | D153B | MCU | 外设 | 状态 |
| --- | --- | --- | --- | --- |
| 左编码器 A | E1A | PA29 | TIMG8_C0 / QEI PHA | 本子阶段冻结 |
| 左编码器 B | E1B | PA30 | TIMG8_C1 / QEI PHB | 本子阶段冻结 |
| 右编码器 A | E2A | PB6 | GPIO 上升沿软件 x1 | 单模块板测通过 |
| 右编码器 B | E2B | PB7 | GPIO 方向输入 | 单模块板测通过 |
| 左电机 IN1 | AIN1 | PB8 | TIMA0_C0 / 10 kHz | `+5%/200 ms` 逻辑点动通过；物理波形未测 |
| 左电机 IN2 | AIN2 | PB9 | TIMA0_C1 / 10 kHz | `-5%/200 ms` 逻辑点动通过；物理波形未测 |
| 右电机 IN1 | BIN1 | PB12 | TIMA0_C2 / 10 kHz | `+5%/200 ms` 逻辑点动通过；物理波形未测 |
| 右电机 IN2 | BIN2 | PB13 | TIMA0_C3 / 10 kHz | `-5%/200 ms` 逻辑点动通过；物理波形未测 |

四路电机输入预留在同一 TIMA0 上，便于同步 10 kHz PWM、统一更新和一次性强制零输出。
最终配置前仍需与 UART0/2/3、底盘 IMU 和 D153B 电压 ADC 的完整引脚账本复核。

## 5. 编码器方案

MSPM0G3507 只有 TIMG8 原生支持 QEI。当前采用：

- 左轮用 TIMG8 两输入硬件 QEI，硬件对每个合法 Gray 状态变化计数，即 AB 四倍频；
- 右轮用 E2A 上升沿软件 x1，ISR 立即读取 E2B 决定方向；
- SystemTask 在 100 Hz 周期边界读取硬件计数，扩展为 64 位累计计数；
- QEI 非法状态跳变单独累计，不能静默并入位置；
- 暂不计算 rpm、轮速或里程，直到 500 PPR 定义、轮径和方向完成实测。

若 500 PPR 是减速箱输出轴每圈 A 相周期数，则 x4 后为 2000 count/rev；300 rpm 时每轮约
10000 edge/s。若 500 PPR 指电机轴或已经包含四倍频，上述换算无效，必须修改配置并重新验收。

左轮实测支持 500 PPR 位于减速前编码器轴。若右轮也使用软件 x4，300 rpm 输出速度对应约
340,000 ISR/s，不接受该负载。右轮 x1 约为 17,007 count/output-rev、满速约 85,000 ISR/s，
分辨率足够，但仍必须在最大速度下验证 ISR late、SystemTask deadline 和左右计数比例。

## 6. 当前无动力编码器门禁

首次接线和烧录前必须满足：

1. 电机两根动力线断开并分别绝缘，D153B VM/4S 不上电，不存在电机运动输出。
2. 编码器单独使用限流 5 V 或经确认可用的 3.3 V 供电，MCU 与编码器只共地。
3. MCU 断开时测 E1A/E1B 高电平；高于 3.6 V 必须经过 5 V tolerant 输入的 3.3 V
   Schmitt buffer/level shifter，或经审查的分压网络。
4. 确认 PA29 只接 E1A、PA30 只接 E1B，禁止把 5 V、AOUT1 或 AOUT2 接到 MCU。
5. 用户在场并准备物理断电；编码器子阶段只允许手转架空轮组。

## 7. 左轮编码器验收

构建门禁：FreeRTOS 与 App full rebuild 均为 0 Error / 0 Warning。

板测顺序：

1. 空输入启动，确认 `g_bsp_encoder_diag.initialized=1`、`running=1`。
2. 静止 120 秒，累计计数不漂移，`qei_error_count=0`。
3. 手转左轮，确认正反方向计数对称、反转可回到起始值附近。
4. 向车头方向转一整圈，记录原始符号和绝对 count；据此冻结 `encoder_count_sign` 与 CPR。
5. 连续快速手转并检查 QEI error、100 Hz deadline、遥测 drop 和系统 Health。

### 7.1 2026-07-15 左轮结果

| 项目 | 结果 |
| --- | --- |
| FreeRTOS/App full rebuild | 0 Error / 0 Warning |
| DAPLink program | passed |
| Flash verify | 目标 CRC 超时后，56,992 B 逐字节回读 SHA-256 一致 |
| 烧录后 5 秒 | 100 Hz / 1 Hz，CRC/gap/drop/deadline/QEI fault 全部 0 |
| 向前粗略一圈 | `+77,523`、`+76,749`；第二次首个连续段 `+69,207` |
| 理论 provisional CPR | `500 * 4 * 34.014 = 68,028` count/output-rev |
| 向后粗略一圈 | `-74,323`，误正向 `+75` |
| 方向 | 向前为正，左轮 `encoder_count_sign=+1` |
| 静止 120 秒 | 12,196 个 Control 样本全部 delta=0，净计数 0 |
| 120 秒链路 | Control 12,196 / Health 122，100 Hz / 1 Hz |
| 120 秒健康 | CRC/gap/drop/deadline/I2C/active/sticky/QEI fault 全部 0 |

粗略手转不是精密 CPR 标定。当前可以采用 `68,028` 作为 370 profile 的 provisional 值，
速度/里程闭环前必须用标记盘或多圈平均重新冻结。

## 8. 右轮编码器验收

### 8.1 2026-07-15 右轮结果

| 项目 | 结果 |
| --- | --- |
| SysConfig | 0 error；PB6 GPIOB 上升沿 IRQ，PB7 GPIOB 输入 |
| FreeRTOS/App full rebuild | 0 Error / 0 Warning；Code=53640，ZI=16236 |
| DAPLink program | passed |
| Flash verify | 目标 CRC 超时后，56,664 B 逐字节回读 SHA-256 一致 |
| 烧录后静止 5 秒 | 512 Control / 6 Health，100 Hz / 1 Hz；右轮 512 个 delta 全部为 0 |
| 向前粗略一圈 | 原始净计数 `-18,632`，反向杂散 `+22` |
| 向后粗略一圈 | 原始净计数 `+21,691`，反向杂散 `-17` |
| 方向 | 向前原始为负，右轮 `encoder_count_sign=-1` |
| 连续正反手转 | 80,768 个绝对 x1 count；10 ms 最大 230 count |
| 实时性 | SystemTask 最大执行 24 us，周期 9998-10002 us，deadline=0 |
| 链路 | 100 Hz / 1 Hz；CRC/gap/drop 全部 0 |
| 右轮 ISR late | Health issue 17 未出现，active/sticky 均未记录该问题 |
| 执行器安全 | `actuator_output_permitted=0`，没有配置或驱动 AT8236/PWM |

本次右轮单模块板测期间左轮 E1A/E1B 没有接线。PA29/PA30 无内部上下拉，机械扰动时硬件 QEI
出现浮空 `+1/-1` 抖动并记录 issue 16；这不是右轮串线或右轮 ISR late。左右同时验证前必须重新
接好左编码器，或明确增加未接通道管理，不能把浮空输入当作有效 Health 结果。

粗略一圈不是 CPR 标定。当前右轮 370 profile 继续采用
`500 * 1 * 34.014 = 17,007` count/output-rev provisional 值，速度/里程闭环前必须多圈平均。

## 9. AT8236 安全层与无动力逻辑 PWM 结果

为避免 SWD halt/reset 污染实时性并在带 VM 时冻结软件超时，正式点动入口改为 UART 固定帧：

- ServiceTask 内的静态 `CommandService` 是 SerialRx 唯一消费者，不增加任务或动态路由；
- frame type 5、20 字节 payload、CRC16、双 magic、唯一 sequence；
- 只允许单电机、绝对值不超过 10%、持续时间不超过 500 ms；
- SystemTask 在 100 Hz 周期边界消费请求并作为唯一实际输出写入者；
- frame type 6 ACK 报告 staging 状态；Control flags 证明实际 active 窗口；
- timing resync、RTOS fatal、完成或拒绝均强制四路低并清除待执行请求。

2026-07-15 实测条件：VM/4S 未上电，AO1/AO2、BO1/BO2 全部断开并绝缘，左右编码器接好。

| 项目 | 结果 |
| --- | --- |
| FreeRTOS/App full rebuild | 0 Error / 0 Warning；Code=56,776，ZI=16,572 |
| HEX SHA-256 | `251B7FFD474CA35F8B889A8D38146EE18C7C1E3685D080D3E43755D4067747EA` |
| Flash | program passed；59,920 B 回读 SHA-256 `9B28050074A22678F2316ECE4F1BECFEBDF84231F4DE9D1B731ACC273CF86D5E` |
| 烧录后静止 | 100 Hz / 1 Hz；CRC/gap/drop/deadline/I2C/active/sticky 全部 0 |
| 左 `+5%/200 ms` | ACK status 0；20 active 帧；随后 336 safe 帧；编码器 0 |
| 右 `+5%/200 ms` | ACK status 0；20 active 帧；随后 335 safe 帧；编码器 0 |
| 左 `-5%/200 ms` | ACK status 0；20 active 帧；随后 335 safe 帧；编码器 0 |
| 右 `-5%/200 ms` | ACK status 0；20 active 帧；随后 334 safe 帧；编码器 0 |
| 参数协议回归 | `kp 1.0 -> 1.1 -> 1.0` 均一次 ACK applied，CRC/格式错误 0 |

这些结果证明软件命令门、时长限制、自动归零和四个逻辑方向，不等于示波器已经测得 10 kHz
波形，也不等于 D153B、VM 或电机已经上电验收。

### 9.1 Motor Profile 构建结果

| 项目 | 结果 |
| --- | --- |
| 默认选择 | MG370 / profile version 1 |
| SysConfig | 0 error；保留原有 ProjectConfig warning 和 PWM/QEI retention 提示 |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning；Code=58,152，ZI=16,644 |
| HEX SHA-256 | `45D3035850AC9460232A75051FA1958F7F907187400E1C048274D1920F73CBC0` |
| 513X 选择 | ArmClang 预期失败；错误明确列出 6 项未确认关键参数 |
| Profile 遥测 fixture | type 7 CRC/解析通过；MG370 v1、68,028/17,007、`+1/-1`、x4/x1 |
| 烧录/板测 | not run；本次没有烧录新 Profile 固件，也没有驱动电机 |

## 10. 后续子阶段

1. VM 继续断电，用万用表确认 PB8/PB9/PB12/PB13 静止均为低；有示波器时复核 10 kHz 和 5%。
2. 用户在场、轮组架空、限流电源和物理断电准备完成后，只接左电机做 5%/200 ms 点动。
3. 冻结左右 `motor_output_sign`，再逐步验证 10% 短脉冲、非法命令和命令超时。
4. 完成单轮/双轮连续运行、最大编码器速率、温升和故障测试后，才允许 Phase 2A 验收。
5. 速度/里程闭环前用多圈平均冻结 370 输出轴 CPR；513X 到货后使用独立 profile。

## 11. 双轮速度控制结果（2026-07-16）

早期章节记录的是安全层和无动力门禁。本节为最新带 VM、轮组架空实测，优先级高于“闭环未实现”
等历史描述。

### 11.1 控制架构

- 100 Hz 位置式速度 PI：`Kp=8`、`Ki=18`、`Kd=0`，积分限幅 160 permille。
- 每轮独立前馈：左 `315 + 3.8*rpm`，右 `320 + 3.6*rpm`。
- 目标斜坡 350 rpm/s；目标斜坡期间冻结积分，避免大阶跃积分累积。
- 同方向降速时输出最低钳位到 0，不允许短暂反向驱动制动。
- 双轮共同 50% 启动，最短 40 ms；退出助推时直接交给前馈，不把助推写入积分器。
- `>=8 rpm` 使用普通 PI；`<8 rpm` 使用约 35% 滞环驱动/滑行，近零时 60% 的 40 ms kick / 40 ms rest。
- speed 命令可在运行中周期边界重定向，命令时长继续承担通信看门狗。
- 软件上限为 120 rpm、900 permille；65 mm 轮径下约 0.408 m/s。

### 11.2 右编码器高速容错

右轮 PB6 仍为 GPIO 上升沿软件 x1。ISR 若执行时 A 相已经回落，不再使用可能已变化的 B 相重新判方向，
而是累计 late 并沿用最近一次可靠方向；方向尚未建立时忽略该迟到边沿。Health schema v2 公开累计
`EncoderIsrLateCount`，单个健康窗口内至少 4 次 late 才记录 degraded event。

现有线材不能更换。120 rpm / 30 秒累计 128 次 late，但没有 sustained Health issue，左右速度仍一致；
147 rpm 探索累计 217 次并出现 sticky，因此不采用极限速度，比赛上限固定为 120 rpm。

### 11.3 板测结果

| 测试 | 结果 |
| --- | --- |
| 5 rpm / 10 s | 1000/1000；起步差 10 ms；tail `5.303/5.521 rpm`；Health clean |
| 5 rpm 窗口平均 | 0.5 s 左 `4.92–6.17`、右 `4.71–5.96`；1 s 左 `5.21–5.81`、右 `4.85–5.52 rpm` |
| 8 rpm / 10 s | `8.010/7.991 rpm`；Health clean |
| 60 rpm / 10 s | `59.959/60.025 rpm`；约 0.204 m/s；late=0 |
| 100 rpm / 10 s | `100.019/100.018 rpm`；约 0.340 m/s；late=9 |
| 120 rpm / 30 s | `120.012/120.011 rpm`；约 0.408 m/s；3000/3000；late=128；Health clean |
| 10 -> 120 rpm | `t90=280/280 ms`；skew 0 ms；overshoot `7.57%/8.61%` |
| 120 -> 10 rpm | `t90=540/530 ms`；skew 10 ms；tail `9.981/10.023 rpm` |
| 人工阻力扰动 | not run；用户确认上一组没有施加外力，主机 1 CRC / 1 gap 不作为扰动证据 |

当前板上 HEX SHA-256 为
`BB4421DBC943CA4A1EDF7CE6B48448BB5106D8F342FCC1B0BC85465DE0B1E5A5`，Profile v8。
最新测试结束后命令自动归零，`ActuatorOutputPermitted=0`。
最终 FreeRTOS/App full rebuild 均为 0 Error / 0 Warning，Code=64,768，ZI=16,980；重建哈希与板上一致。
最终静止采集为 511 Control / 5 Health / 5 Profile，左右编码器 delta 全 0，
CRC/gap/deadline/active/sticky/I2C 全 0，输出保持禁用。

### 11.4 Profile v13 负载释放与机械死点加固

- v8 有效人工扰动中，左轮最低 `48.421 rpm`、PWM 约 `508->663 permille`，松手峰值 `69.324 rpm`，暴露负载积分释放偏慢。
- `Ki=12` A/B 没有降低松手峰值且回稳更慢，因此保留 `Kp=8/Ki=18/Kd=0`。
- 目标变化与负载释放使用显式资格隔离；只有目标稳定后曾建立同方向负载积分，随后超速至少 1.5 rpm 时才快速退积分。
- 正常启动保持 50%；机械死点恢复独立为 60%、最长 80 ms。v11 两次 `120->10 rpm` 均未卡死。
- v12 单左轮有效扰动最低 `44.276 rpm`、PWM 最高 `672 permille`、松手峰值 `70.559 rpm`；10 ms 轨迹表明 3 倍退积分仍不足。
- v13 将负载积分退回倍率提高到 10 倍，并保留 6000 permille/s 的卸载专用 PWM 降沿；普通指令降速仍为 1500 permille/s。
- v13 阶跃：`120->10 rpm` 为 `550/540 ms`、尾段 `9.956/9.979 rpm`；`10->120 rpm` 为 `270/280 ms`、超调 `7.97%/7.64%`、尾段 `120.026/120.041 rpm`。
- v13 构建为 FreeRTOS/App 0 Error / 0 Warning，Code=65,200，ZI=17,020；HEX
  `43F7F69B207BA39BBF5C847B935DD05E4EADD682780F1B982CF6C6A3291B5FAF` 已烧录并验证。
- v13 单左轮人工扰动通过：左轮最低 `45.422 rpm`，PWM `507.9->670 permille`，右轮均值 `59.999 rpm`；松手峰值 `67.384 rpm`，超调 `12.31%`，820 ms 内进入并持续保持目标 ±3%。
- 相比负载相近的 v12，松手超调由 `17.60%` 降至 `12.31%`；保留当前 v13，不继续为手压工况提高控制强度。
- 最终静止收尾：512 Control / 5 Health / 5 Profile，100/1/1 Hz，CRC/gap/deadline/drop/I2C/active/sticky/encoder late 全零，`ActuatorOutputPermitted=0`。

### 11.5 阶段结论

Phase 2A 的架空直流电机与编码器子阶段完成。左右轮共用同一套 v13 速度控制器，分别保留独立前馈、方向和 CPR；双轮阶跃、同步、连续运行、低速蠕行及单左轮抗扰均已验收。右轮单独手压、电流、温升、落地负载、实际补偿、直线同步和带载上限不在本阶段继续展开，统一 deferred 到后续整车负载阶段。
