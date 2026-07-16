# ECHO 当前状态

> 临时说明：本工作树是从 Phase 1F 创建的 MPU6050 隔离硬件 spike，不是正式阶段推进。
> 正式 Phase 2A 仍位于 `C:\Users\Auror\ECHO-phase2a-work` 并保留其 dirty 电机/编码器实现。
> 本 spike 已完成双 Profile、invalid 快照修复、0/0 构建、烧录、两块 600 秒静止诊断、
> 软件故障恢复、`WHO_AM_I=0x70` 的 120 秒联合回归，以及 2026-07-16 的 27000 秒无人值守
> 静态 soak 和 reset 后 20 秒清洁复核；Motion、物理断开恢复与示波器上升沿仍未完成，
> 禁止描述为 Phase 2B 完成。
> 用户已要求稳定性任务收尾后恢复独立 IMU 算法工作树；温补、姿态和滤波参数必须基于重新
> 采集的温度、多姿态、三轴转动、六面标定与外部航向参考数据。本 spike 只保留独立证据，
> 不合入正式 main，也不改变正式 Phase 2A/ICM42688 决策；用户已明确授权把完成的稳定性
> checkpoint 推送到独立 GitHub 分支。
> 完成的稳定性 checkpoint 已推送到 `origin/codex/mpu6050-hardware-spike`，未创建阶段 tag。

最后核对日期：2026-07-16（Asia/Shanghai）

## 1. 权威仓库

- 唯一正式工程：`E:\ECHO`
- 正式分支：`main`
- 最近已验收阶段：Phase 1F
- 最近已验收固件：本文件所在的 Phase 1F 提交
- 阶段标签：`phase-1f-operability-diagnostics`
- 基线父提交：`cb7c4c32783cf7eeeabbbdec4a193aee99077159` / `phase-1e-oled-ui`
- 远端 `origin/main`：`4b1a3db` / Phase 1F，已 push

Phase 1E 与 Phase 1F 已合入正式 main 并推送至 GitHub。正式 Phase 2A 与 MPU6050 硬件 spike
继续保持独立分支/worktree；本 spike 的完成成果只发布到隔离分支，不合入正式 main。

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

- MCLK 80 MHz，FreeRTOS Tick 1 kHz，TIMG12 标称 1 MHz、32 位向上计数。
- SystemTask 100 Hz；当前仍发布测试信号，不是真实电机 PID。
- ServiceTask 2 ms 服务 UART/参数，并每 100 ms 成为健康快照的唯一写入者。
- TelemetryTask 非阻塞发送 100 Hz Control、1 Hz Health 和参数 ACK；UART1 PA8/PA9、230400 8N1。
- Health 快照包含 schema、身份、uptime/reset reason、overall/active/sticky/first fault、任务栈、
  deadline、UART/Telemetry、参数、OLED/I2C、future sensor/actuator 安全占位。
- OLED 为 Overview、RTOS、COMM、DEVICE、PARAM 五页；虚拟输入支持 press/long/repeat/timeout。
- `kp/ki/kd/target` 共用一个 metadata 表；UART/OLED 只 staging，SystemTask 周期边界应用。
- 启动最早期通过 BSP 只读捕获 reset cause；当前执行器 armed/output permitted 始终为 0。
- 当前 spike 接入 PA0/PA1 上的第一块 MPU6500-compatible 样品；同一驱动也支持已测的标准
  MPU6050 样品，100 Hz 采样并发布角速度。没有电机、云台、树莓派或真实运动输出，正式
  底盘 IMU 仍为后续 ICM42688。

### MPU6050 稳定性追加证据（2026-07-16）

- 02:06 的 FreeRTOS/App full rebuild 均为 0 Error / 0 Warning；App 尺寸保持
  Code=57220、RO=3068、RW=28、ZI=16236，HEX SHA-256 保持
  `66A1263E853BB9217399A2FA605D0D48822C219EB276EB57060E34F72783A328`。
- 初始两次 20 秒检查分别出现 READY `0/2037`、I2C error `270`，以及 READY `54/2036`、
  I2C error `61`；两次 OLED 均 offline、active/sticky 均为 `0x00009800`。保留失败现场后，
  后续 30 秒与 60 秒复核恢复 I2C/Health 全 0。该瞬态没有在五次 reset、15 分钟基线或整夜
  soak 中复现，当前不把它解释为永久硬件故障。
- 五次独立 DAPLink reset 后的 20 秒 Static 均通过；每次 Control `2035-2036`、Health
  `20-21`，ResetReason=3，CRC/gap/deadline/I2C/active/sticky 全 0。
- 参数协议压力覆盖非法 ID、NaN/Inf、越界值、坏 CRC、截断恢复、BUSY、duplicate 和连续
  50 次调参；随后 30 秒复核 `ParameterErrorCount=7`、`ApplySequence=53`，链路与 Health 全 0。
- reset 后 15 分钟基线为 Control/Health `91453/915`，全部 Control valid+READY，
  CRC/gap/deadline/I2C/active/sticky 全 0。
- 无人值守静态 soak 由 25200 秒主体与 1800 秒补充组成，共 30 个 900 秒段、请求采集时长
  27000 秒；两次 runner 主机墙钟合计 `27573.452 s`，累计 Control/Health `2743306/27432`，
  全部 Control READY，CRC/gap/duplicate/
  out-of-order/deadline/I2C/active/sticky 全 0。最小栈 94 words、heap minimum 3056 B、
  serial ring high-water 464 B、quiet max 38777 us、OLED 全程 online。
- 成功后执行 DAPLink `reset run`；10:44 的 20 秒 Static 为 `2032/20`，11:14 再次独立
  `reset run` 后为 `2038/20`。两次均为 100/1 Hz、全部 READY、实时性/通信/I2C/Health
  门禁全 0，执行器输出许可仍为 0。
- 30 段加权 X/Y/Z mean 为 `-0.056074762/0.013271149/0.025727453 dps`，总体标准差为
  `0.047037499/0.037879724/0.034651981 dps`；加速度模长
  `1.018024726 +/- 0.003618586 g`。本轮遥测没有同步温度，不能据此拟合温补。
- 主证据根目录：`C:\Users\Auror\AppData\Local\Temp\echo-unattended-soak-20260716-overnight`；
  结构化摘要见 `docs/worklogs/2026-07-16_mpu6050_unattended_stability.md`。

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
- Health frame 已达到 128 B 原子 TX 上限；未来扩展应升级协议或拆帧，不能继续追加 payload。
- 当前临时面包板接线存在已复现的 I2C/UART 并发敏感性；短优先 quiet window 已使 20 秒
  OLED + IMU + UART 联合回归全 0，正式整车仍必须改善线束、共地、上拉和去耦。
- 27000 秒无人值守静态 soak 未复现新的 I2C/UART、Health、deadline 或资源退化，但它不覆盖
  Motion、真实物理断开/恢复、示波器上升沿、温度循环或正式整车线束。
- 两次前置启动异常在后续复位与长稳中未复现；当前只能记录为未定位的启动/共享 I2C 瞬态，
  不能声称根因已经关闭。正式硬件仍需核实上拉、去耦、共地和示波器上升沿。
- MPU6500-compatible 样品的 600 秒静止诊断为 100/1 Hz，全部门禁为 0；Z 轴总体均值
  `0.010664 dps`，纯积分约 `6.4 deg/10 min`。
- 标准 MPU6050 样品的 600 秒静止诊断主机墙钟 `607.127 s`、设备跨度 `609.600 s`，全部门禁
  为 0；Z 轴总体均值 `-0.002115 dps`，纯积分约 `-1.3 deg/10 min`，但静态加速度模长
  `1.083473 g` 暴露比例偏差。正式航向仍需要长期观测校正，正式姿态使用前需六面标定。
- invalid 快照修复后的 `WHO_AM_I=0x70` 样品 120 秒联合回归为 `12195/122` Control/Health，
  100/1 Hz，全部门禁为 0；兼容路径回归已关闭。
- 软件故障注入已证明离线快照立即 invalid、三轴归零、重新探测/校准并恢复 READY。物理断开
  恢复因面包板只能四针整体拔插而 deferred，待安全断开点准备后执行。
- 当前 actuator safety 只是锁定占位契约；Phase 2A 必须建立真实的唯一写入者、零输出和失联策略。

## 8. 下一步：隔离算法研究与 Phase 2A

IMU 算法只在 `C:\Users\Auror\ECHO-mpu6050-algorithm-work` 中继续。恢复硬件采集后先增加同步
温度与原始六轴数据，完成冷启动到热稳定、多温点、多姿态、三轴正反转、六面加速度和外部航向
参考，再比较温度补偿、四元数、Mahony/Madgwick、Allan 方差及必要的 KF/EKF。六轴 IMU 没有
外部航向观测时不能保证长期 yaw 零漂。

Phase 2A 范围仅限 AT8236、底盘电机和 GMR 编码器。开始前：

1. 确认正式 main 已安全合入 Phase 1F，并从该标签创建新分支/worktree。
2. 记录电机额定电压/电流、减速比、轮径、轮距和编码器计数定义。
3. 核实 AT8236 引脚、极性、制动真值表、电流和温升边界。
4. 设计唯一执行器写入者、默认零输出、失联/调试暂停安全状态。
5. 用户必须在场，架空轮组，准备物理断电，再允许任何运动输出。

不得提前混入底盘 IMU、云台或树莓派功能。长期阶段顺序见 `docs/CURRENT_WORKFLOW.md`。
