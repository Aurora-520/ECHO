# Phase 2A AT8236 无动力逻辑 PWM

日期：2026-07-15

## 目标与边界

- 在不连接 VM/4S 和电机动力线的条件下验证 AT8236 四路 MCU 控制输出的软件链。
- 保持 SystemTask 为唯一执行器输出写入者，不增加任务或动态命令注册。
- 本次不声明 D153B、物理 PWM 波形或电机运动通过。

现场条件由用户确认：VM/4S 未上电，AO1/AO2、BO1/BO2 断开并绝缘，左右编码器已接好，
用户在场。

## 关键决定

最初的调试请求依赖 SWD 写 RAM。无动力试验发现 OpenOCD 即使没有显式 `halt/reset`，连接并
写请求时仍导致 MCU 重启，串口出现旧新序列混合；该路径不能用于实时或带 VM 验收。

正式实现改为：

- `CommandService` 在 ServiceTask 中静态解析 UART frame type 2/5；
- actuator command 使用 20 字节 payload、CRC16、双 magic、唯一 sequence；
- `ChassisActuator_StageDebugRequest` 只 staging，SystemTask 在 100 Hz 边界应用；
- 单电机、绝对值不超过 100 permille、持续不超过 500 ms；
- frame type 6 ACK 报告 staging 状态，Control flags 报告实际 active 窗口；
- timing resync、完成、拒绝和 RTOS fatal 均强制四路低并清除 pending。

主机工具 `tools/chassis_motor_pulse.ps1` 默认只发送一次，并区分：

- `LogicOnly`：必须确认 VM 和全部电机输出断开；
- `MotorPowered`：必须确认用户在场、单轮架空、另一电机断开、限流和物理断电。

## 构建与烧录

| 项目 | 结果 |
| --- | --- |
| SysConfig | 0 error；1 条 PWM/QEI retention 提示 |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning |
| 程序尺寸 | Code=56,776；RO=3,112；RW=28；ZI=16,572 |
| HEX SHA-256 | `251B7FFD474CA35F8B889A8D38146EE18C7C1E3685D080D3E43755D4067747EA` |
| Flash program | passed |
| Flash readback | 59,920 B；SHA-256 `9B28050074A22678F2316ECE4F1BECFEBDF84231F4DE9D1B731ACC273CF86D5E` |

OpenOCD 目标 CRC 算法超时后，工程脚本自动回退到逐字节 readback；哈希一致并执行 `reset run`。

## 板测结果

烧录后静止 3 秒：Control 100 Hz、Health 1 Hz；CRC、gap、drop、deadline、I2C、active、sticky
全部为 0，`ActuatorOutputPermitted=0`。

| 请求 | ACK | active 帧 | trailing safe 帧 | 编码器 | 证据目录 |
| --- | --- | ---: | ---: | --- | --- |
| 左 `+50/200 ms` | status 0 | 20 | 336 | 左右 0 | `tests/artifacts/phase2a-pulse-20260715-182912` |
| 右 `+50/200 ms` | status 0 | 20 | 335 | 左右 0 | `tests/artifacts/phase2a-pulse-20260715-182957` |
| 左 `-50/200 ms` | status 0 | 20 | 335 | 左右 0 | `tests/artifacts/phase2a-pulse-20260715-183038` |
| 右 `-50/200 ms` | status 0 | 20 | 334 | 左右 0 | `tests/artifacts/phase2a-pulse-20260715-183113` |

四次均为 Control 100 Hz、Health 1 Hz，CRC/gap/out-of-order/deadline/drop/I2C/Health 全部干净。

参数协议回归：`kp 1.0 -> 1.1 -> 1.0` 均首次请求得到 applied ACK，最终值恢复 1.0，解析 CRC
和格式错误为 0。

全部操作后的 5 秒静止收尾：509 Control / 5 Health，100 Hz / 1 Hz；CRC、gap、out-of-order、
deadline、drop、I2C、active、sticky 均为 0，`ActuatorOutputPermitted=0`，parameter pending=0，
apply sequence=2。最小剩余栈为 System 154、Service 118、Telemetry 146、Display 103、Idle 103、
Timer 104 words；heap minimum 3064 B。

## 失败与恢复

- OpenOCD 实时 RAM 请求试验导致目标重启；由于 VM 和动力线断开，没有运动风险。该方案已移除。
- 点动工具首轮在发送前因 PowerShell 无符号 magic 转换失败；修正为显式十六进制解析后通过。
- 烧录期间一次串口采集得到 0 字节，是目标仍在 readback 流程中；补读原会话后确认烧录、回读和
  最终 reset 均成功。

## 未完成

- 万用表确认 PB8/PB9/PB12/PB13 静止均为低。
- 示波器复核四路 10 kHz、5% 占空比和固定低互补脚。
- 限流条件下只接左电机的 5%/200 ms 架空点动和 `motor_output_sign` 冻结。
- 右电机重复、非法命令/超时故障、连续运行、最大速率、电流和温升测试。
- Phase 2A commit/tag/正式 main 合入。
