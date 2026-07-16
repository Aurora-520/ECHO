# ECHO 当前交接

```yaml
handoff_schema: 1
updated_at: 2026-07-16T20:00:00+08:00
updated_by: Codex
status: complete
```

## 1. 权威位置

```text
唯一正式工程：E:\ECHO
开发 worktree：C:\Users\Auror\ECHO-zdt-x42s-work
当前 branch：refs/heads/codex/zdt-dual-uart-stepper
起始 HEAD：494ea191ec4becdbeb9af2cfe897d61f0cd544b2
当前 HEAD：494ea191ec4becdbeb9af2cfe897d61f0cd544b2，备用步进改动尚未提交
已验收基线：refs/tags/phase-1f-operability-diagnostics
origin/main：4b1a3dbef3c96b1b627c90d3c10566e3c6a0ec2f / Phase 1F，已 push
origin/phase-2a-at8236-chassis-encoder：本文件所在的 Phase 2A 阶段提交，已 push
```

## 2. 当前目标

- Phase 2A 架空直流电机与编码器阶段已完成：冻结 MG370 Profile v13、8–120 rpm 普通 PI、0–8 rpm 蠕行、机械死点恢复、负载释放快速退积分和右编码器迟到容错。
- 比赛用软件上限暂定 120 rpm、90% PWM；65 mm 轮径下约 0.408 m/s，并保留约 16% PWM 抗负载余量。
- 本阶段不再继续调直流电机。电流、温升、落地负载、实际补偿、直线同步和带载上限转入后续阶段；当前不混入 IMU、云台、视觉或组合动作。

## 3. 当前进度

### 已完成

- 2026-07-16 闭环调试已完成：位置式速度 PI 使用 `Kp=8`、`Ki=18`、`Kd=0`，积分限幅 160 permille，目标斜坡 350 rpm/s，输出封顶 900 permille。
- 启动采用双轮共同 50% / 最短 40 ms 破静摩擦；退出时直接交给每轮前馈，不把助推写入积分器。
- 运行中 speed frame type 5 可在 SystemTask 周期边界无停顿重定向目标；电气点动和跨模式请求仍保持 busy 门禁。
- 10->120 rpm 阶跃左右 `t90=280/280 ms`、同步差 0 ms、超调 `7.57%/8.61%`；120->10 rpm `t90=540/530 ms`、同步差 10 ms。
- 120 rpm / 30 秒连续运行通过：3000/3000 speed frames，左/右 `120.012/120.011 rpm`，CRC/gap/deadline/Health 全零。
- 5 rpm 蠕行通过：1000/1000 speed frames，起步差 10 ms；1 秒窗口左 `5.21–5.81 rpm`、右 `4.85–5.52 rpm`，不再因低速失速停机。
- 8 rpm 普通 PI 边界通过：左/右 `8.010/7.991 rpm`；60 rpm 与 100 rpm 分别达到约 0.204 m/s 与 0.340 m/s。
- 右编码器迟到 ISR 已改为沿用最近可靠方向；Health v2 增加累计迟到计数，持续密集迟到才降级。120 rpm / 30 秒累计 128 次但无 sustained issue。
- 板上当前固件为 MG370 Profile v8，HEX SHA-256 `BB4421DBC943CA4A1EDF7CE6B48448BB5106D8F342FCC1B0BC85465DE0B1E5A5`。
- 最终 FreeRTOS/App full rebuild 均 0 Error / 0 Warning；Code=64,768，ZI=16,980；重建 HEX 与板上哈希一致，无需重复烧录。

- Phase 1F 已在 `4b1a3db` 完成、验收、打 annotated tag 并合入正式 main。
- Phase 2A 分支/worktree 已从正式 Phase 1F 基线创建。
- 接手清理的代码和文档修改已形成，验证摘要见当前 worklog；尚未提交。
- 用户确认左轮使用 D153B Motor A/E1A/E1B，右轮使用 Motor B/E2A/E2B。
- 已审查 TI QEI、MSPM0G3507 I/O 和 D153B 原理图，形成 Phase 2A 设计文档。
- 左轮 TIMG8 QEI 已完成 SysConfig、0/0 构建、烧录回读校验、正反手转和 120 秒静止板测。
- 左轮向前为正，`encoder_count_sign=+1`；provisional 输出轴 CPR=68,028。
- 右轮 E2A->PB6 上升沿软件 x1、E2B->PB7 方向输入已完成 SysConfig 和源码实现。
- 右轮版本已完成 SysConfig、0/0 构建、烧录回读校验和正反手转板测。
- 右轮向前原始为负，`encoder_count_sign=-1`；provisional 输出轴 CPR=17,007。
- 左右编码器同时接线静止 10 秒为零；一圈绝对计数比 3.812:1，Health 全干净。
- TIMA0 CC0-CC3 已配置为 PB8/PB9/PB12/PB13、10 kHz；启动后 BSP 将四路硬件强制低。
- UART CommandService 已替代会重启目标的 SWD RAM 点动；ServiceTask 静态解析，SystemTask 唯一写输出。
- ChassisActuator 只接受 CRC、双 magic、单电机、不超过 10%/500 ms 的一次性点动请求。
- RTOS assert/malloc/stack/fatal 路径已接紧急双低钩子；安全 PWM 版本已 0/0 构建和烧录，未上 VM。
- 安全 PWM 版本已烧录并完成左右正负 5%/200 ms 无动力逻辑点动，四次均 20 active 帧后自动归零。
- 参数协议在命令路由重构后完成 `kp 1.0 -> 1.1 -> 1.0` 回归。
- 一套 `motor_profile` 已覆盖 MG370 与 513X 占位，未复制 BSP、未增加引脚、禁止运行时切换。
- MG370 v1 已填入确认数据；左轮 x4/+1/68,028，右轮 x1/-1/17,007，左右电机输出符号保持待定 0。
- 100 Hz 输出轴 RPM 诊断和 1 Hz Profile telemetry frame type 7 已接入。
- 513X 选择已由 ArmClang 负向测试确认产生明确编译错误。

### 未开始

- 轮组落地后的电流、温升、实际补偿、直线同步、带载抗扰与速度上限；均明确 deferred 到后续阶段。
- 轮组落地后的负载、直线同步、实际轮距/里程、温升和电源电流验收。
- PB8/PB9/PB12/PB13 示波器物理波形复核；现有逻辑与编码器证据不能替代探头测量。
- 近零速短距离位置环；当前仅实现速度蠕行，不把 1–5 rpm 当作长期位置保持。

### 进行中

- 最新状态优先于本节后续保留的开环历史：双轮架空调试和最终静止收尾已完成，板上 Profile v13 输出已自动归零，本阶段结束。
- 当前串口为 COM4；未保留 OpenOCD、Keil 或串口采集后台会话。系统中另有用户/另一 Codex 的 PowerShell 进程，不得终止。
- 以下条目保留 2026-07-16 早期开环调试过程，仅作历史证据，不代表当前板上能力。

- 2026-07-16 用户在场进入直流电机调试：先低占空比开环确认 PWM/编码器链路，再进入 PI/PID 与自动整定。
- 当前 `COM4` 已识别为 DAPLink USB 串口，未发现 OpenOCD、Keil 或其他串口监视进程；尚未烧录、尚未产生本次运动输出。
- 烧录前 3 秒只读采集确认旧固件为 100 Hz Control / 1 Hz Health、CRC/gap/deadline/drop=0、左右编码器增量全 0、`ActuatorOutputPermitted=0`；旧固件无 Profile 帧。
- 同次采集发现 OLED offline、`I2cErrorCount=1693`，并存在 `ActiveIssueMask=0x00009800`；需在新固件复位后重新判定，若编码器 QEI fault 再现则禁止点动。
- MG370 Profile 固件已在 VM 断电条件下烧录；OpenOCD 快速 CRC 超时后，61,480 字节 Flash 读回 SHA-256 `D360C84932F44D8E3C11590AC2B9471AA5A2D25CE1E41B63A8886F705034B6E0` 一致并 `reset run`。
- 烧录后 5 秒静态采集通过：514 Control / 5 Health / 5 Profile，100/1/1 Hz，CRC/gap/deadline/drop/I2C/active/sticky=0，OLED online；左右编码器增量全 0，output permitted=0。
- 左轮 `+5%/200 ms` 与 `+7%/200 ms` 均被接受并准确保持 20 个控制周期后自动归零；Health/链路干净，但左编码器计数均为 0，未克服静摩擦。
- 左轮 `+10%/200 ms` 也未产生编码器计数；目标 Health 干净并已归零，但主机采集出现 1 个 CRC 错误和 1 帧缺口，该次证据判为 attention，不作为链路完全通过。
- 按用户现场指令，单次调试脉冲硬上限有界调整为 20%/1000 ms；单电机、双 magic、CRC、唯一 sequence、SystemTask 唯一写入者和自动归零保持不变，闭环仍锁定。
- 20%/1000 ms 版本完成 PowerShell AST 和 FreeRTOS/App full rebuild 0/0；Code=58,152，ZI=16,644，HEX SHA-256 `C3940A459151C975CAB52B2312FF4D6121A65960CEE48C10954AA4CAE9C6F903`。
- 该版本已在 VM 断电条件下烧录、验证和 reset run；随后 5 秒静态采集为 512 Control / 5 Health / 5 Profile，100/1/1 Hz，CRC/gap/deadline/I2C/active/sticky=0，左右编码器增量全 0，output permitted=0。
- 左轮 `+20%/1000 ms` 已执行：ACK 匹配、100 active、356 trailing safe，100/1 Hz，CRC/gap/deadline/Health 全干净并自动归零；左右编码器计数均为 0，未起转。停止继续提高占空比，等待现场电流/声音观察并检查电机端电压或限流折返。
- 用户随后明确要求改为双轮 `+50%/1000 ms`。调试协议现有界允许 1 或 2 个电机、最大 50%、最长 1 秒；主机脚本新增 `Both` 模式和独立 `ConfirmBothMotorsConnected` 门禁，原单轮模式仍要求另一电机断开。
- 双轮 50% 版本 AST 与安全负例通过，FreeRTOS/App full rebuild 0/0；Code=58,168，ZI=16,644，HEX SHA-256 `9025C4D027813A300809474B03C674F9FD1E361DAF4B6771B87BCAA51C3C80BF`。
- 用户确认 VM 断电后该版本已烧录、verify/reset；3 秒静态采集 309 Control / 4 Health / 4 Profile，100/1/1 Hz，CRC/gap/deadline/I2C/active/sticky=0，output permitted=0。
- 用户重新上电后双轮 `+50%/1000 ms` 执行通过：ACK 匹配、100 active、355 trailing safe，CRC/gap/deadline/Health=0；左原始净计数 `+34,776`、右原始净计数 `+9,608`，两路均确认起转并自动归零。
- active 窗内左/右原始计数为 `31,230 / 8,656`，按 provisional CPR 对应约 `27.5 / 30.5 rpm` 平均输出轴速度，峰值约 `38.1 / 40.2 rpm`。结合既有前进原始符号左正右负，冻结候选 `motor_output_sign` 为左 `+1`、右 `-1`。

## 4. Git 与文件所有权

本文件更新时间点，Phase 2A 工作树以实时 `git status` 为准；已观察到以下进行中修改：

```text
AGENTS.md
docs/ARCHITECTURE_BOUNDARIES.md
docs/CURRENT_WORKFLOW.md
docs/PROJECT_STATUS.md
tools/phase1f_field_check.ps1
docs/worklogs/2026-07-15_phase2a_takeover_cleanup.md（未跟踪）
docs/handoff/*（本交接机制）
docs/hardware/ECHO_WIRING_GUIDE.md（并行新增，已同步当前构建状态）
docs/phases/PHASE2A_AT8236_CHASSIS_ENCODER.md（未跟踪）
config/ECHO.syscfg
bsp/include/bsp_encoder.h（未跟踪）
bsp/source/bsp_encoder.c（未跟踪）
app/main.c
app/tasks/service_task.c
app/tasks/system_task.c
module/service/command_service.h（未跟踪）
module/service/command_service.c（未跟踪）
module/service/chassis_actuator.h（未跟踪）
module/service/chassis_actuator.c（未跟踪）
module/service/motor_profile_config.h（未跟踪）
module/service/motor_profile.h（未跟踪）
module/service/motor_profile.c（未跟踪）
module/service/parameter_service.h
module/service/parameter_service.c
module/service/telemetry.c
module/service/telemetry.h
module/service/system_health.h
module/service/system_health.c
keil/ECHO.uvprojx
platform/generated/ti_msp_dl_config.c
platform/generated/ti_msp_dl_config.h
tools/chassis_motor_pulse.ps1（未跟踪）
tools/telemetry_capture.ps1
docs/worklogs/2026-07-15_phase2a_left_encoder_bringup.md（未跟踪）
docs/worklogs/2026-07-15_phase2a_right_encoder_bringup.md（未跟踪）
docs/worklogs/2026-07-15_phase2a_at8236_logic_pwm.md（未跟踪）
docs/worklogs/2026-07-15_phase2a_motor_profiles.md（未跟踪）
```

- 暂存区在最近一次核对时为空。
- 上述接手清理文件属于当前任务，接手者必须先读 diff，不得还原或整文件覆盖。
- 正式工程原有用户文件、stash 和备份不在本任务范围内；正式工程保持只读。

## 5. 实现与决定

- 普通速度区定义为 `>=8 rpm`，采用每轮独立前馈 + 位置式 PI、测量低通、anti-windup、目标/输出斜坡和同方向扭矩约束。
- 蠕行区定义为 `<8 rpm`：双轮共同启动后，每轮使用约 35% 滞环驱动/滑行；近零时使用 60% 的 40 ms kick / 40 ms rest，不累计 PI 积分、不触发普通失速停机。
- MG370 Profile v8 前馈为左 `315 + 3.8*rpm`、右 `320 + 3.6*rpm`；软件速度上限 120 rpm、最大 PWM 900 permille。
- 目标斜坡期间冻结积分；同方向降速时控制输出最低钳位到 0，不允许为刹车反向驱动。
- 右轮 GPIO x1 ISR 若发现 A 相已回落，累计 late 但沿用最近可靠方向，避免读取已变化 B 相造成符号错误。
- Health schema 为 v2，Health frame 132 B；SerialTx 原子写上限已扩到 160 B，主机解析器兼容旧 112 B 和新 116 B payload。

- `CURRENT_HANDOFF.md` 记录基线之后的实时工作；`PROJECT_STATUS.md` 记录已确认阶段基线。
- 新对话固定先读实时交接，再用 Git、进程和硬件现场检查验证。
- Phase 1F 同名 branch/tag 使用完整 refs，不删除或重命名历史对象。
- Phase 1F field check 的累计 I2C 和 UART/OLED quiet 门禁正在接手清理中加固。
- 左轮硬件 QEI 使用 TIMG8：E1A->PA29/PHA，E1B->PA30/PHB；硬件按合法 AB 状态变化 x4 计数。
- 右轮 E2A->PB6 上升沿软件 x1、E2B->PB7 方向输入；x4 因满速约 340k ISR/s 被否决。
- 右轮向前原始为负，唯一方向配置值冻结为 `encoder_count_sign=-1`；诊断遥测继续保留原始计数。
- OpenOCD 实时连接写 RAM 会导致目标重启，禁止把 SWD RAM 写入作为电机点动入口；正式入口固定为 UART frame type 5/6。
- UART 执行器请求只 staging；SystemTask 周期边界应用，timing resync/fatal/完成/拒绝均清 pending 并四路低。
- `ECHO_MOTOR_PROFILE_SELECTION` 是唯一型号选择宏，默认 MG370；OLED/UART 不提供运行时切换。
- Profile 位于 service 层；BSP 继续只处理固定引脚和原始电气量。
- 未确认 Profile 字段为 0 且有效位清除；现有安全点动可用，闭环归一化输出保持锁定。
- 513X 缺少额定电压、堵转电流、减速比、编码器接口/电平/PPR，选择时必须编译失败。
- 未接的 QEI 输入会浮空；右轮单模块板测期间左轮未接导致 issue 16，不属于右轮 ISR late。
- PA29/PA30/PB6/PB7 不是 5 V tolerant；D153B 编码器接口没有电平转换，直连前必须测量并降压。
- 左右轮不是控制主从关系；左轮只作为第一只标定轮，整车前进时两轮统一为正。

## 6. 验证证据

| 层级 | 结果 | 说明 |
| --- | --- | --- |
| Phase 1F 固件终验 | passed | 证据见 Phase 文档和 worklog |
| 接手清理脚本 AST/fixture/负例 | passed | 见 `docs/worklogs/2026-07-15_phase2a_takeover_cleanup.md` |
| 本交接机制引用/结构检查 | passed | 引用路径存在，必填结构完整 |
| `git diff --check` | attention | SysConfig generated 2 行空白尾随；禁止手改 generated |
| Phase 2A SysConfig | passed | 0 error；1 条 STOP/STANDBY retention 提示 |
| Phase 2A FreeRTOS full rebuild | passed | 0 Error / 0 Warning |
| Phase 2A App full rebuild | passed | 0 Error / 0 Warning；Code=53992，ZI=16164 |
| Phase 2A 左编码器烧录 | passed | program；56,992 B 回读 SHA-256 一致；reset run |
| Phase 2A 左编码器方向 | passed | 前进约 +76.7k，后退约 -74.3k，sign=+1 |
| Phase 2A 左编码器静止 120 秒 | passed | 12,196 样本全部 delta=0，Health/链路全干净 |
| 右轮 SysConfig | passed | 0 error；PB6 GPIOB 上升沿 IRQ，PB7 GPIOB 输入 |
| 右轮 FreeRTOS full rebuild | passed | 0 Error / 0 Warning |
| 右轮 App full rebuild | passed | 0 Error / 0 Warning；Code=53640，ZI=16236 |
| 右轮固件烧录 | passed | program；56,664 B 回读 SHA-256 一致；reset run |
| 右轮静止 5 秒 | passed | 512 Control / 6 Health；右轮 delta 全 0；链路/Health 干净 |
| 右轮方向 | passed | 前进 `-18,632`，后退 `+21,691`，sign=-1 |
| 右轮连续手转负载 | passed | 10 ms 最大 230 count；SystemTask 最大 24 us；deadline=0 |
| 右轮 ISR late | passed | Health issue 17 未出现 |
| 左轮未接浮空 | attention | PA29/PA30 未接时机械扰动触发 issue 16；联合测试前重新接线 |
| 双编码器静止 10 秒 | passed | 左右 1019 个 delta 全 0；Health/链路干净 |
| 左 x4 / 右 x1 一圈比例 | passed | 76,573 / 20,072，绝对比 3.812:1；QEI/ISR late=0 |
| 双编码器连续静止 60 秒 | passed | 6099 个样本左右全 0；Health/链路干净 |
| AT8236 SysConfig | passed | TIMA0 CC0-CC3，PB8/PB9/PB12/PB13，4 MHz/400=10 kHz，初值 0%，默认 stop |
| AT8236 安全层构建 | passed | FreeRTOS/App 0/0；Code=56776，ZI=16572；HEX SHA-256 `251B7FFD474CA35F8B889A8D38146EE18C7C1E3685D080D3E43755D4067747EA` |
| AT8236 安全层烧录 | passed | program；59,920 B readback SHA-256 `9B28050074A22678F2316ECE4F1BECFEBDF84231F4DE9D1B731ACC273CF86D5E` |
| AT8236 四方向逻辑 PWM | passed | 左右正负 5%/200 ms 均 20 active 帧后归零；100/1 Hz；链路/Health 干净；编码器 0 |
| 参数协议回归 | passed | `kp 1.0 -> 1.1 -> 1.0`，首次 ACK applied，CRC/格式错误 0 |
| 最终静止收尾 | passed | 509 Control / 5 Health；100/1 Hz；CRC/gap/deadline/drop/I2C/active/sticky=0；output permitted=0 |
| MG370 Motor Profile 构建 | passed | FreeRTOS/App 0/0；Code=58152，ZI=16644；HEX SHA-256 `45D3035850AC9460232A75051FA1958F7F907187400E1C048274D1920F73CBC0` |
| 513X 选择门禁 | passed | ArmClang 预期 1 error；错误列出额定电压、堵转电流、减速比、接口、电平、PPR |
| Profile telemetry fixture | passed | type 7 / 52 B；MG370 v1、68,028/17,007、+1/-1、x4/x1；CRC/unknown=0 |
| 2026-07-16 烧录前静态采集 | attention | 309 Control / 3 Health，100/1 Hz，左右编码器增量全 0，output permitted=0；旧固件 OLED offline、I2C error=1693、active/sticky=`0x00009800`，新固件复位后必须复核 |
| Motor Profile 烧录 | passed | VM 断电；61,480 B 读回 SHA-256 `D360C84932F44D8E3C11590AC2B9471AA5A2D25CE1E41B63A8886F705034B6E0` 一致；reset run |
| Motor Profile 静态板测 | passed | 514 Control / 5 Health / 5 Profile，100/1/1 Hz；MG370 v1；左右编码器增量全 0；Health 全干净；output permitted=0 |
| 左轮 +5%/200 ms | passed, no start | ACK/20 active/334 safe；Health 干净；左右编码器计数 0 |
| 左轮 +7%/200 ms | passed, no start | ACK/20 active/336 safe；Health 干净；左右编码器计数 0 |
| 左轮 +10%/200 ms | attention, no start | ACK 后自动归零、Health 干净、编码器计数 0；主机流 1 CRC error / 1 sequence gap，脚本按门禁失败 |
| 20%/1000 ms 调试上限构建 | passed | AST passed；FreeRTOS/App full rebuild 0/0；Code=58152，ZI=16644；HEX `C3940A...6F903` |
| 20%/1000 ms 固件烧录/静态板测 | passed | VM 断电烧录/verify/reset；512 Control / 5 Health / 5 Profile，100/1/1 Hz；左右计数 0；Health 全干净；output permitted=0 |
| 左轮 +20%/1000 ms | passed, no start | ACK/100 active/356 safe；CRC/gap/deadline/Health=0；左右编码器计数 0；已自动归零，不继续升占空比 |
| 双轮 +50%/1000 ms 构建 | passed | Both 安全门负例通过；FreeRTOS/App full rebuild 0/0；Code=58168，ZI=16644；HEX `9025C4D0...C80BF` |
| 双轮 50% 固件烧录/静态板测 | passed | VM 断电烧录/verify/reset；309 Control / 4 Health / 4 Profile，100/1/1 Hz；Health 全干净；output permitted=0 |
| 双轮 +50%/1000 ms | passed | ACK/100 active/355 safe；左/右净计数 +34,776/+9,608；CRC/gap/deadline/Health=0；自动归零；候选 motor sign 左 +1、右 -1 |
| 5 rpm 蠕行 10 s | passed | 1000/1000；起步差 10 ms；tail `5.303/5.521 rpm`；1 s 窗口左 `5.21–5.81`、右 `4.85–5.52 rpm`；Health clean |
| 8 rpm PI 10 s | passed | `8.010/7.991 rpm`；起步差 10 ms；CRC/gap/deadline/Health=0 |
| 60 rpm PI 10 s | passed | `59.959/60.025 rpm`；0.204 m/s；encoder late=0；Health clean |
| 100 rpm PI 10 s | passed | `100.019/100.018 rpm`；0.340 m/s；encoder late=9；Health clean |
| 120 rpm PI 30 s | passed | 3000/3000；`120.012/120.011 rpm`；0.408 m/s；encoder late=128；Health clean |
| 10->120 rpm step | passed | `t90=280/280 ms`；skew=0 ms；overshoot `7.57%/8.61%`；tail `120.020/120.016 rpm` |
| 120->10 rpm step | passed | `t90=540/530 ms`；skew=10 ms；无反向制动；tail `9.981/10.023 rpm` |
| 147 rpm 探索 | attention | 实际约 `160.6/157.7 rpm` 且 encoder late=217/sticky；已放弃极限目标并把比赛上限降到 120 rpm |
| 60 rpm 人工扰动 | not run | 用户确认未施加外力；该次主机流 1 CRC / 1 gap 不属于机械扰动证据 |
| PB8/PB9/PB12/PB13 物理电平/波形 | not run | 仍需万用表/示波器实测，逻辑遥测不能替代引脚测量 |
| 电机点动 | not run | 必须重新确认架空、限流和物理断电后执行 |

## 7. 硬件现场状态

- 最新状态：用户在场、轮组架空、左右电机与编码器均连接，编码器 3.3 V 且共地，VM 12 V 已上电，物理断电开关可用。
- 板上运行 Profile v8 / HEX `BB4421...E5A5`；最近 8 rpm 测试完成后自动归零，`ActuatorOutputPermitted=0`。
- 轮径已冻结为 65 mm；0.408 m/s 对应 120 rpm。轮距仍未冻结。
- 本节后续早期“VM 未上电/动力线断开”条目仅是历史条件，不代表当前现场状态。

- 左右编码器和 AT8236 安全 PWM 固件已烧录并完成无动力逻辑板测；电机、云台和 4S 未驱动。
- 用户确认物理映射：左轮 Motor A，+->AOUT1、-->AOUT2、A/B->E1A/E1B；右轮 Motor B，
  +->BOUT1、-->BOUT2、A/B->E2A/E2B。
- 当前左右编码器均已接线：E1A/E1B->PA29/PA30，E2A/E2B->PB6/PB7，3.3 V、共地。
- 最近一次确认的 D153B VM/4S 状态为未上电，AO1/AO2、BO1/BO2 断开并绝缘。
- 已烧录 PWM 安全层，但全部板测均在 VM 断电和动力线断开条件下完成，未驱动电机。
- 板上现运行双轮最高 50%/1000 ms 有界调试固件 HEX `9025C4D0...C80BF`；双轮 50% 脉冲已通过并自动归零。用户已被要求关闭 VM，实际状态等待确认。
- 2026-07-16 用户确认：人在现场、轮组架空、首次只接左电机、右电机动力线断开并绝缘、编码器 3.3 V 且可靠共地、12 V 电源限流约 0.5 A、物理断电可立即操作。
- 已要求烧录和静态检查期间保持 12 V VM 断电；实际接通 VM 前由用户再次执行现场动作。
- 异常发热的天猛星板继续禁用。

## 8. 后台进程与运行状态

- 2026-07-16 17:14 +08:00：所有 OpenOCD、Keil 和 COM4 测试会话均已退出；板上自动归零。系统仍有不属于本任务的长期 PowerShell 进程，未终止、未干预。
- 2026-07-16 17:22 +08:00：最终静止采集通过；511 Control / 5 Health / 5 Profile，100/1/1 Hz，左右编码器 delta 全 0，CRC/gap/deadline/active/sticky/I2C=0，`ActuatorOutputPermitted=0`。串口会话已退出。

- 2026-07-15 14:59 +08:00 核对：未发现 OpenOCD、GDB 或 pyOCD 进程。
- 2026-07-15 18:37 +08:00：烧录会话已退出；COM4 完成四次点动和最终静止采集；未发现 OpenOCD/UV4 进程。
- 2026-07-15 19:03 +08:00：Motor Profile 最终构建完成；未烧录，未发现 OpenOCD/UV4 进程。
- 2026-07-16 11:57 +08:00：`COM4` 为 VID 0D28/PID 0204 的 DAPLink USB 串口且状态正常；未发现 OpenOCD、pyOCD、J-Link、Keil 或串口监视进程。
- 2026-07-16 12:00 +08:00：完成不发送命令的烧录前静态采集；串口已关闭，无后台采集进程。
- 2026-07-16 12:02 +08:00：MG370 Profile 固件烧录、读回校验、reset run 和 5 秒静态采集完成；OpenOCD/串口会话均已退出。
- 2026-07-16 12:07 +08:00：完成左轮 5%/7%/10% 三档 200 ms 点动；均无编码器响应，10% 采集出现单次 CRC/gap；所有串口会话已退出。
- 2026-07-16 12:11 +08:00：用户确认 VM 已断电；20%/1000 ms 有界调试版本完成全量构建，未烧录，无后台构建/OpenOCD/串口进程。
- 2026-07-16 12:14 +08:00：20%/1000 ms 版本完成烧录、verify/reset 和 5 秒静态板测；OpenOCD/串口会话已退出，VM 保持断电。
- 2026-07-16 12:17 +08:00：左轮 +20%/1000 ms 点动完成、未起转、自动归零；串口会话已退出，无后台输出进程。
- 2026-07-16 12:21 +08:00：双轮 50%/1000 ms 版本完成 AST、安全负例和全量构建；未烧录，无 OpenOCD/串口会话。
- 2026-07-16 12:25 +08:00：双轮 50% 固件完成烧录、verify/reset 和静态板测；OpenOCD/串口会话已退出，VM 等待用户接通。
- 2026-07-16 12:29 +08:00：双轮 +50%/1000 ms 点动通过并自动归零；两编码器均响应，串口/Health 干净；串口会话已退出。

## 9. 问题、阻塞与风险

- 当前唯一现场待测项是人工阻力扰动；上一组用户未施加外力，需要按明确时机重新施加约 1 秒阻力。
- 右编码器线材当前不能更换；120 rpm 下 late 比例很低且方向回退有效，但轮组落地、强干扰或更差线束仍需复测。
- 当前 120 rpm 上限来自架空、12 V、约 0.5 A 限流条件，不等于落地带载最高速度。
- 5 rpm 是速度蠕行平均值，不保证瞬时速度恒定；接近零速的精确位移和停车仍应由后续位置环负责。

- GMR 编码器 500 PPR 位于电机轴还是输出轴、是否已含 x4 仍未确认。
- GMR E1A/E1B 的高电平和输出类型未确认，禁止直接连接非 5 V tolerant 的 PA29/PA30。
- 轮径、轮距、左右安装朝向和 513X 完整参数未冻结。
- MG370 左右 `motor_output_sign`、起转/最大 PWM、速度/加速度限制、堵转保护和 PID 未冻结；closed loop 锁定。
- 513X 关键电气与编码器参数未确认，当前只能保留占位并编译锁定。
- 未接 PA29/PA30 会使左 QEI 输入浮空；联合测试必须重新接左编码器或设计明确的未接通道管理。
- CPU debug halt 期间不能依赖软件超时归零；首次带 VM 点动禁止断点/Watch，必须使用限流电源和物理断电。
- 实测 OpenOCD 无显式 halt 的 RAM 写入也会使本目标重启；电机命令只允许走 UART 安全帧。
- 物理 ADC 五键、Flash 持久化和硬件看门狗仍为 deferred。
- 涉及电机、轮组或 4S 前必须由用户在现场明确许可，架空轮组并准备物理断电。

## 10. 下一步精确动作

1. 用户回复“准备好”后，复位 MCU 并启动 60 rpm / 15 秒采集；用户在收到明确提示后轻压左轮约 1 秒并松开。
2. 计算速度最低点、输出增量、松手后 90% 恢复时间和左右扰动耦合；若无可测阻力则记录 deferred，不伪造通过。
3. 最终 full rebuild 已完成且哈希未变；只需在任务结束前做一次不发送命令的静止 Health/归零采集。
4. 人工扰动若本次无法配合，明确记录 deferred，不得把第一次无可辨识曲线写成通过。
5. 不自动提交、暂存或 push；正式 `E:\ECHO` 保持只读。

## 11. 禁止操作

- 不自动 push，不删除 stash、备份、branch、tag 或 worktree。
- 不使用破坏性 Git 命令，不覆盖未知 dirty 文件。
- 不手改 `platform/generated`。
- 未重新获得现场许可前，不连接电机动力线、VM/4S，不驱动轮组、云台或其他高功率输出。
- 不把 E1A/E1B 的 5 V 未确认信号直接接入 PA29/PA30。
- 不把 Phase 1F 完成描述为最终工程完成。

## 12. 相关资料

- `AGENTS.md`
- `docs/PROJECT_STATUS.md`
- `docs/CURRENT_WORKFLOW.md`
- `docs/ENGINEERING_RED_LINES.md`
- `docs/hardware/ECHO_WIRING_GUIDE.md`
- `docs/phases/PHASE1F_OPERABILITY_DIAGNOSTICS.md`
- `docs/phases/PHASE2A_AT8236_CHASSIS_ENCODER.md`
- `docs/worklogs/2026-07-15_phase1f_operability_diagnostics.md`
- `docs/worklogs/2026-07-15_phase2a_takeover_cleanup.md`
- `docs/worklogs/2026-07-15_phase2a_right_encoder_bringup.md`
- `docs/worklogs/2026-07-15_phase2a_motor_profiles.md`
- `docs/worklogs/2026-07-15_realtime_handoff_infrastructure.md`
- `docs/worklogs/2026-07-16_phase2a_speed_control_tuning.md`

## 13. 接手自检

- [x] 权威仓库、worktree、branch、HEAD 和基线已记录。
- [x] 已知 dirty 文件和所有权已记录。
- [x] 当前硬件与后台进程的不确定项未伪报。
- [x] 下一步和危险动作门槛已明确。
- [x] 新文件引用、必填结构和 `git diff --check` 已验证。

## 14. 当前功能接线指南

- 已新增长期入口 `docs/hardware/ECHO_WIRING_GUIDE.md`，当前版本 `v0.1`、状态
  `living_draft`；后续 Phase 接线继续更新同一文件。
- 指南已汇总已验收的 SWD、UART1、SSD1306 OLED 和 PB22 板载 LED，以及物理五键的
  deferred 状态。
- 已按用户确认记录：左轮 `AO1/AO2/E1A/E1B`，右轮 `BO1/BO2/E2A/E2B`；原文 `A02`
  统一规范为 `AO2`，接线仍须按模块丝印复核。
- 左右编码器接线和无动力板测状态已更新；电机 PWM 引脚已完成逻辑点动，但物理波形和带 VM 驱动仍未验收。
- 2026-07-16 已补充带 VM 双轮闭环证据：5 rpm 蠕行、8–120 rpm PI、大幅双向阶跃和 120 rpm / 30 秒连续运行均完成；人工阻力扰动待重测。
- 接线指南最初由并行文档任务新增；AT8236 安全固件此前已烧录并完成无动力板测。本次新增
  Motor Profile 仅构建，尚未烧录、上 VM 或驱动电机。

## 15. 2026-07-16 18:26 速度控制最新状态

本节优先于前文仍保留的 Profile v8 和“人工扰动 not run”历史描述。

- 板上当前为 MG370 Profile v13，HEX SHA-256
  `43F7F69B207BA39BBF5C847B935DD05E4EADD682780F1B982CF6C6A3291B5FAF`。
- v13 full rebuild：FreeRTOS/App 均 0 Error / 0 Warning，Code=65,200，ZI=17,020；DAPLink program/verify/reset passed。
- PI 仍为 `Kp=8`、`Ki=18`、`Kd=0`。普通启动为 50%；机械死点恢复独立为 60%、最长 80 ms。
- 负载释放资格要求目标稳定、曾建立同方向负载积分且随后超速至少 1.5 rpm；目标变化会清除资格。负载积分退回倍率为 10 倍，卸载专用 PWM 降沿上限为 6000 permille/s；普通降速仍使用 1500 permille/s。
- v13 `120->10 rpm`：`t90=550/540 ms`、同步差 10 ms、尾段 `9.956/9.979 rpm`，Health clean。
- v13 `10->120 rpm`：`t90=270/280 ms`、同步差 10 ms、超调 `7.97%/7.64%`、尾段 `120.026/120.041 rpm`，Health clean。
- v12 单左轮有效人工扰动：基线约 60 rpm，左轮最低 `44.276 rpm`，左 PWM 最高 `672 permille`，右轮保持约 60 rpm；松手峰值 `70.559 rpm`。10 ms 轨迹证明 3 倍退积分仍过慢，因此形成 v13。
- 另一组 v12 采集先压错右轮，随后又压左轮，不能作为单轮对照。
- v13 最终单左轮人工扰动通过：60 rpm 基线下左轮最低 `45.422 rpm`（下降 24.30%），PWM 由约 `507.9` 升至 `670 permille`；右轮均值 `59.999 rpm`。
- 松手后 100 ms 达峰值 `67.384 rpm`，超调 `12.31%`；820 ms 内进入并持续保持目标 ±3%，PWM 200 ms 内回到基线 ±5 permille。相比负载相近的 v12，超调由 `17.60%` 降至 `12.31%`。
- 最终静止收尾通过：512 Control / 5 Health / 5 Profile，100/1/1 Hz；CRC/gap/deadline/drop/I2C/active/sticky/encoder late 全零，`ActuatorOutputPermitted=0`。
- 精确下一步：轮组落地后复测电流、温升、直线同步、带载抗扰和 120 rpm 上限；架空 PI 调试不再继续为手压工况加大控制强度。

## 16. 2026-07-16 张大头备用后端最新状态

本节优先于前文把 X42S 列为云台主选的历史描述。

- 串口无刷电机是云台主执行器；两台张大头仅作备用，不接入当前主云台控制链。
- 两台张大头均为 Emm 固件 TTL 串口直连。第一代为 UART2 PB15/PB16 + DMA_CH1，第二代为
  UART3 PB2/PB3 + DMA_CH2；UART1 PA8/PA9 调试链保持不变。
- `zdt_protocol` 封装 Emm 帧和物理加速度换算，`zdt_stepper` 封装选择门禁、非阻塞轮询、
  20 ms 限频、相同目标去重、第一代位置忙拒绝和退出时停止/失能。
- 默认 `backend_selected=0`，当前应用没有调用备用选择接口，因此开机不查询、不使能、不运动。
- 2026-07-17 用户在场完成第二代实机验收：地址 `0x01`、115200 8N1、连续回包正常；
  `+15 deg` 实测 `+15.007 deg`，返回基准误差 `0.044 deg`，正反 30 rpm 反馈和停止均通过。
- 速度模式现有 1.5 s 板端租约；故意省略 Stop 的测试能够自动停止。最终第二代未使能、
  未运动、速度 0、无效响应/第二代超时/堵转均为 0，备用后端已退出。
- 最新 App 为 0 Error / 0 Warning；75,936 字节 Flash 回读 SHA-256
  `4615539E2032A871FDE4F126A3595045FC5E3F38934FA19195E3EB848DBF31F4`。
- 第一代仍须由用户在场确认接线、地址、细分、波特率、机构安全范围后单独实机验收；
  第二代带载电流、温升、堵转保护和机械限位也仍待验证。
- 本次文件位于 `C:\Users\Auror\ECHO-zdt-x42s-work`，在 `codex/zdt-dual-uart-stepper`
  分支提交；正式 `E:\ECHO` 工作目录未直接修改。
- 详细设计与验收门禁见 `docs/hardware/ZDT_BACKUP_STEPPER.md` 和
  `docs/worklogs/2026-07-16_zdt_backup_stepper.md`。
