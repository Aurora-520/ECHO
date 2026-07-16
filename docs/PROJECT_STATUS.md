# ECHO 当前状态

最后核对日期：2026-07-16（Asia/Shanghai）

## 1. 权威仓库

- 唯一正式工程：`E:\ECHO`
- 正式分支：`main`
- 最近已验收阶段：Phase 1F
- 最近已验收固件：本文件所在的 Phase 1F 提交
- 阶段标签：`phase-1f-operability-diagnostics`
- 基线父提交：`cb7c4c32783cf7eeeabbbdec4a193aee99077159` / `phase-1e-oled-ui`
- 远端 `origin/main`：`4b1a3db` / Phase 1F，已 push

Phase 1E 与 Phase 1F 已合入正式 main 并推送至 GitHub。Phase 2A 已从正式 Phase 1F 基线
创建独立分支/worktree，完成成果已推送至 `origin/phase-2a-at8236-chassis-encoder`：

```text
branch:   phase-2a-at8236-chassis-encoder
worktree: C:\Users\Auror\ECHO-phase2a-work
start:    4b1a3db / refs/tags/phase-1f-operability-diagnostics
```

Phase 2A 已开始。左右编码器无动力板测已通过；AT8236 默认零输出和 UART 一次性点动安全层
已完成 FreeRTOS/App 0/0 构建、烧录回读校验及四方向 5%/200 ms 无动力逻辑 PWM 板测。
共享底层的 MG370/513X 编译时 Motor Profile 已实现并完成 MG370 0/0 构建，但新 Profile 固件
尚未烧录。物理 PWM 波形、带 VM 单轮点动、方向冻结、故障测试和连续运行尚未完成。

## 2. 必须保留的用户状态

正式工作树原有 4 个用户文件，阶段合入时必须保持其语义，不得自动暂存、覆盖或还原：

```text
ECHO.uvmpw
freertos/keil/freertos_ECHO.uvprojx
keil/ECHO.uvprojx
tools/telemetry-web/README.md
```

Phase 1F 需要在 `keil/ECHO.uvprojx` 中登记 `system_health.c` 和 `bsp_reset.c`。正式合入必须
只增加这两个 `<File>` 节点，同时保留用户已有路径/配置变化，禁止整文件覆盖。

安全恢复点仍保留：

- `stash@{0}: pre-phase1e-user-protected-changes`
- `C:\tmp\ECHO-phase1e-merge-backup-20260715`

不得自动 pop/drop stash，不得删除备份。

## 3. 已完成阶段

| 阶段 | 提交 | 标签 | 结论 |
| --- | --- | --- | --- |
| 1A | `f3a4552` | `phase-1a-baseline` | 可移动工程、Keil/VSCode/DAPLink 链通过 |
| 1B | `212513a` | `phase-1b-rtos-skeleton` | 静态 RTOS 骨架、hook、栈/堆/故障诊断通过 |
| 1C | `cc4b52f` | `phase-1c-clock-timebase` | 80 MHz、1 MHz 时基和 100 Hz 时序诊断通过 |
| 1D | `e7a1ac7` | `phase-1d-telemetry-tuning` | UART DMA 遥测、网页曲线和 RAM 参数协议通过 |
| 1E | `cb7c4c3` | `phase-1e-oled-ui` | SSD1306 UI、I2C 超时恢复和 UART quiet window 通过 |
| 1F | 本文件所在提交 | `phase-1f-operability-diagnostics` | 统一健康、五页 UI、参数元数据、诊断工具与低功率终验通过 |

Phase 1D 仓库没有保存原始串口日志或具体验收数字，后续仍不得补写。Phase 1F 的数字来自
本次保存的 ignored JSON/CSV 和 RAM 快照，摘要见 Phase 文档与 worklog。

## 4. 当前固件行为

- Phase 2A worktree 的当前固件已覆盖下列 Phase 1F/早期 Phase 2A 描述：SystemTask 100 Hz 已运行真实双轮速度控制，不再发布纯测试信号。
- MG370 Profile v8：左/右方向 `+1/-1`，CPR `68028/17007`，速度上限 120 rpm，PWM 上限 900 permille。
- `>=8 rpm` 使用位置式 PI（`Kp=8`、`Ki=18`、`Kd=0`）与每轮前馈；`<8 rpm` 使用短启动脉冲、滞环驱动/滑行和近零 kick/rest 蠕行。
- UART speed 命令支持运行中在 SystemTask 周期边界重定向；命令结束、调度异常和 RTOS fatal 仍强制双路归零。
- Health schema v2 公开右编码器 ISR late 总数；late 边沿沿用最近可靠方向，持续密集迟到才产生 degraded issue。
- MCLK 80 MHz，FreeRTOS Tick 1 kHz，TIMG12 标称 1 MHz、32 位向上计数。
- SystemTask 100 Hz；当前仍发布测试信号，不是真实电机 PID。
- ServiceTask 2 ms 服务 UART/参数，并每 100 ms 成为健康快照的唯一写入者。
- TelemetryTask 非阻塞发送 100 Hz Control、1 Hz Health 和参数 ACK；UART1 PA8/PA9、230400 8N1。
- Health 快照包含 schema、身份、uptime/reset reason、overall/active/sticky/first fault、任务栈、
  deadline、UART/Telemetry、参数、OLED/I2C、future sensor/actuator 安全占位。
- OLED 为 Overview、RTOS、COMM、DEVICE、PARAM 五页；虚拟输入支持 press/long/repeat/timeout。
- `kp/ki/kd/target` 共用一个 metadata 表；UART/OLED 只 staging，SystemTask 周期边界应用。
- 启动最早期通过 BSP 只读捕获 reset cause；当前执行器 armed/output permitted 始终为 0。
- 当前已有左右编码器诊断采样和受限 UART 单次 PWM 命令；默认输出为零，尚无带动力运动证据。
- 执行器命令固定为 CRC、双 magic、单电机、最高 10%、最长 500 ms；SystemTask 唯一写输出。
- 电机型号由 `ECHO_MOTOR_PROFILE_SELECTION` 编译时选择；默认 MG370 v1，禁止 OLED 运行时切换。
- MG370 每轮 Profile 固定左 x4/+1/68,028、右 x1/-1/17,007；电机输出符号和闭环限制仍待实测。
- 513X 关键参数未确认，选择它会产生明确编译错误；不会生成可驱动固件。

## 5. Phase 1F 最终构建与实测

| 项目 | 结果 |
| --- | ---: |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning |
| 程序尺寸 | Code=53048, RO=2864, RW=28, ZI=15956 |
| HEX SHA-256 | `1A205780BF54C948915A7D29E1DC6C240912C4A4FE4A95499D4B093FF25D3157` |
| DAPLink/OpenOCD | program/verify/reset passed |
| 120 秒 Control / Health | 12194 / 121，100 Hz / 1 Hz |
| 120 秒 CRC / gap / deadline / drop | 0 / 0 / 0 / 0 |
| 120 秒周期 / max execution / jitter | 9998-10002 / 23 / 2 us |
| 120 秒 I2C / OLED / quiet | 43378 success、0 error、271 refresh、271/271 |
| 10 分钟 Control / Health / total | 60953 / 610 / 61563 |
| 10 分钟 CRC / gap / deadline / drop | 0 / 0 / 0 / 0 |
| 10 分钟 I2C / OLED / quiet | 234578 success、0 error、1466 refresh、1466/1466 |
| quiet max / active | 38529 us / 0 |
| TX ring high-water | 280 B |
| 六任务最小剩余栈 | 180 / 128 / 151 / 102 / 104 / 104 words |
| heap minimum | 3064 B |
| Health / active / sticky / actuator permitted | OK / 0 / 0 / 0 |

最终已 `reset run`，3 秒干净状态复核为 parameter sequence=0、parameter errors=0、Health OK、
active/sticky=0、OLED online、输出门锁定。

## 6. Phase 1F 硬件门禁

| 门禁 | 状态 | 说明 |
| --- | --- | --- |
| 物理 ADC 五键 | deferred | 无电阻梯形、引脚冻结和 ADC 实测分布；虚拟输入契约已通过 |
| Flash 掉电保存 | deferred | scatter 未保留参数槽；未执行 Flash 擦写或损坏注入 |
| 硬件看门狗 | deferred | 未完成 owner/window/debug halt/reset cause/执行器安全板测，默认未启用 |

这些 deferred 不允许被描述为通过。它们不阻塞 Phase 2A 的独立台架实现，但真实电机输出前
必须重新审查安全影响；物理菜单、赛场掉电保存和看门狗分别在对应硬件准备完成后闭环。

## 7. 已知风险

- OLED 仍可能是临时杜邦线；正式整车需短线、可靠共地、核实上拉和去耦。
- 两块天猛星中异常发热的板继续禁用。
- OpenOCD 目标端 CRC 算法偶尔无法 halt；脚本的逐字节 Flash 回读 SHA-256 回退已通过。
- COM 号会变化；工具已要求显式 `-Port`，不能把本次 COM4 当永久事实。
- Health frame 已扩展为 132 B，SerialTx 原子写上限同步提高到 160 B；主机解析器兼容旧 112 B 和新 116 B Health payload。
- AT8236 逻辑安全层已建立唯一写入者、默认零输出和限时单次命令，但物理波形与带载失效状态未验收。
- MG370 的起转/最大 PWM、速度/加速度限制、堵转保护阈值和 PID 尚未冻结，闭环输出接口保持锁定。

## 8. 下一步：Phase 2A

Phase 2A 范围仅限 AT8236、底盘电机和 GMR 编码器。开始前：

1. 已确认正式 main 安全合入 Phase 1F，并从该标签创建独立分支/worktree。
2. 记录电机额定电压/电流、减速比、轮径、轮距和编码器计数定义。
3. 用户再次确认现场安全后烧录 MG370 Profile 固件，VM 断电复核 1 Hz Profile 遥测和四路静止低。
4. 用户在场、单轮架空、另一电机断开、限流和物理断电确认后，先做左轮 5%/200 ms 点动。
5. 冻结左右电机方向与运行限制后完成故障、连续运行、最大编码器速率、电流和温升验收。

接手清理已更新工作流程和架构状态，并加固 Phase 1F field check 的累计 I2C 与 UART/OLED
quiet-window 门禁。这些维护不代表 Phase 2A 功能已经实现。

当前左轮编码器配置为 D153B E1A/E1B -> PA29/PA30 TIMG8 硬件 QEI。用户确认实际信号为
3.3 V；向前原始计数为正，provisional 输出轴 CPR 为 68,028，闭环前仍需多圈精确标定。

不得提前混入底盘 IMU、云台或树莓派功能。长期阶段顺序见 `docs/CURRENT_WORKFLOW.md`。

## 9. Phase 2A 当前确认基线（2026-07-16）

| 项目 | 结果 |
| --- | --- |
| 当前板上 HEX | `BB4421DBC943CA4A1EDF7CE6B48448BB5106D8F342FCC1B0BC85465DE0B1E5A5` |
| 构建 | FreeRTOS/App full rebuild 0 Error / 0 Warning；Code=64,768，ZI=16,980 |
| 5 rpm 蠕行 | 10 秒完整；1 秒窗口左 `5.21–5.81`、右 `4.85–5.52 rpm`；Health clean |
| 8 rpm PI 边界 | `8.010/7.991 rpm`；Health clean |
| 60 / 100 rpm | `59.959/60.025`、`100.019/100.018 rpm`；Health clean |
| 120 rpm 连续 | 30 秒、3000/3000；`120.012/120.011 rpm`；约 0.408 m/s；Health clean |
| 10 -> 120 rpm | `t90=280/280 ms`，skew 0 ms，overshoot `7.57%/8.61%` |
| 120 -> 10 rpm | `t90=540/530 ms`，skew 10 ms，同方向降速禁止反向驱动 |
| 147 rpm 探索 | 不采用；编码器 late=217 且闭环模型超出线性区，比赛上限降为 120 rpm |
| 人工阻力扰动 | not run；用户确认上一组没有施加外力，1 CRC / 1 gap 仅为主机串口偶发 |
| 最终静止收尾 | 511 Control / 5 Health / 5 Profile；左右 delta=0；CRC/gap/deadline/active/sticky/I2C=0；output permitted=0 |

当前 120 rpm 上限仅代表轮组架空、12 V、约 0.5 A 限流和现有线材条件。轮组落地后的电流、温升、
直线同步和抗扰仍需独立验收。正式工程 `E:\ECHO` 未修改，当前 worktree 未自动提交、暂存或 push。

## 10. Phase 2A 速度控制更新（2026-07-16 18:26）

- 当前板上固件：MG370 Profile v13，HEX
  `43F7F69B207BA39BBF5C847B935DD05E4EADD682780F1B982CF6C6A3291B5FAF`。
- full rebuild：FreeRTOS/App 0 Error / 0 Warning，Code=65,200，ZI=17,020；DAPLink program/verify/reset passed。
- 普通区 PI 仍为 `Kp=8/Ki=18/Kd=0`；新增有资格门控的负载释放快速退积分，目标变化不会触发该路径。
- 正常启动 50%，机械死点恢复 60% / 最长 80 ms；软件速度/PWM 上限仍为 120 rpm / 900 permille。
- v13 `120->10 rpm`：`t90=550/540 ms`，尾段 `9.956/9.979 rpm`；v13 `10->120 rpm`：`t90=270/280 ms`，超调 `7.97%/7.64%`。
- v13 最终单左轮人工扰动通过：左轮最低 `45.422 rpm`，PWM `507.9->670 permille`，松手峰值 `67.384 rpm`（`12.31%`），820 ms 内进入并持续保持目标 ±3%；右轮均值 `59.999 rpm`。
- 最终静止收尾为 512 Control / 5 Health / 5 Profile，CRC/gap/deadline/drop/I2C/active/sticky/encoder late 全零，输出门禁为 0。
- 当前测试命令已结束并自动归零。正式 `E:\ECHO` 仍未修改；未自动暂存、提交或 push。
- 阶段状态：架空直流电机与编码器调试完成。电流、温升、落地负载、实际补偿、直线同步和带载速度上限 deferred 到后续阶段。

## 11. 云台张大头备用后端（2026-07-16）

- 用户已冻结执行器优先级：串口无刷电机是云台主选；第一代和第二代张大头只作备用，
  不得接管当前主云台控制链。
- 两台张大头均为 Emm 固件、TTL 串口直连。UART1 PA8/PA9 调试链保持不变；第一代分配
  UART2 PB15/PB16 + DMA_CH1，第二代分配 UART3 PB2/PB3 + DMA_CH2，均为 115200 8N1。
- 已加入协议编码/解析、双 UART BSP 和非阻塞备用步进状态机。默认后端未选择，开机零发送；
  相同运动目标不随 100 Hz 循环重发，最小发送间隔 20 ms，状态轮询间隔 50 ms。
- Emm 的 8 位加速度参数已封装为物理 `RPM/s`。推荐初始值 500 RPM/s，对应编码 `0xD8`；
  不再由上层直接填写容易误用的原始 `acc` 字节。
- 第一代位置运动未完成时拒绝新的位置目标；第二代按其 Emm 固件能力允许更新位置目标。
  退出备用后端时，两路都执行“立即停止 -> 失能”。
- 协议单元测试、mock UART 状态机测试和 App/FreeRTOS 构建已通过；没有烧录，没有板上 UART
  回包证据，也没有电机运动、稳定性、温升或机构边界证据。详情见
  `docs/hardware/ZDT_BACKUP_STEPPER.md` 和本次 worklog。
