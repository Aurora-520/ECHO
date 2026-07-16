# Phase 2A 右轮编码器单模块板测

日期：2026-07-15
工作树：`C:\Users\Auror\ECHO-phase2a-work`
分支：`refs/heads/phase-2a-at8236-chassis-encoder`
起始 HEAD：`4b1a3dbef3c96b1b627c90d3c10566e3c6a0ec2f`

## 1. 目标与边界

- 验证右轮 `E2A -> PB6` 上升沿软件 x1、`E2B -> PB7` 方向输入。
- 冻结右轮向前原始符号，检查 ISR late、100 Hz deadline 和串口链路。
- D153B VM/4S 不上电，BO1/BO2 动力线断开并绝缘，只允许手转右轮。
- 不配置或驱动 AT8236/PWM，不进行电机运动测试。

## 2. 实现

- SysConfig 新增 GPIOB.6 上升沿中断和 GPIOB.7 输入。
- `GROUP1_IRQHandler` 在 E2A 上升沿读取 E2B，按软件 x1 累计方向计数。
- SystemTask 100 Hz 采样右轮；诊断遥测 `control_output` 保留右轮原始 x1 delta，
  `auxiliary` 为 x4 等效显示。
- Health issue 17 监控 `right.late_irq_count`，执行器输出许可始终为 0。

## 3. 构建与烧录

| 项目 | 结果 |
| --- | --- |
| SysConfig | 0 error；1 条 TIMG8 STOP/STANDBY retention 提示 |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning；Code=53640，ZI=16236 |
| HEX SHA-256 | `CF384A1E27635DB08D4F5FB47EAB91F242A0AF3E837C03BCB50F399B9F2E4857` |
| DAPLink program | passed |
| Flash verify | 目标 CRC 超时；56,664 B 回读 SHA-256 `E8BED2D8F72F0C47AA81518984EA0601245F7CEDB6F9EBCF272DBDFF20D284F3` 一致 |

## 4. 板测结果

| 项目 | 结果 |
| --- | --- |
| 烧录后静止 5 秒 | 512 Control / 6 Health，100 Hz / 1 Hz；右轮 512 个 delta 全 0 |
| 向前粗略一圈 | 原始净 `-18,632`；反向杂散 `+22` |
| 向后粗略一圈 | 原始净 `+21,691`；反向杂散 `-17` |
| 方向配置 | 右轮 `encoder_count_sign=-1`，归一化后整车向前为正 |
| 连续正反手转 | 80,768 absolute x1 count；10 ms 最大 230 count |
| Control / Health | 100 Hz / 1 Hz |
| CRC / gap / drop / deadline | 全部 0 |
| SystemTask | 最大执行 24 us；周期 9998-10002 us |
| 右轮 ISR late | Health issue 17 未出现 |
| 执行器输出许可 | 始终为 0 |

原始证据保存在 ignored 的 `tests/artifacts/phase2a_right_*.csv/json`。

## 5. 异常与解释

- 本次右轮单模块板测时，用户确认左轮 E1A/E1B 完全未接。
- PA29/PA30 当前无内部上下拉，机械扰动时 TIMG8 QEI 输入浮空，出现 `+1/-1` 抖动并记录
  Health issue 16。该问题不是右轮串线，也不是右轮 ISR late。
- 一次 OpenOCD RAM 只读尝试遇到 target unknown state，使 MCU 调试复位；读取到的零值不作为
  累计诊断证据。随后静止 5 秒复核通过，ResetReason=debug reset。
- 粗略一圈不用于冻结 CPR。右轮继续使用 provisional 17,007 count/output-rev，闭环前多圈标定。

## 6. 结论与下一步

右轮 PB6/PB7 软件 x1 单模块无动力板测通过，方向和实时负载满足继续联合测试的条件。
Phase 2A 尚未完成。下一步重新接好左编码器，在 VM/4S 断电且电机动力线断开的条件下验证
左硬件 x4、右软件 x1 的同时运行、计数比例、干扰、QEI error、ISR late 和 CPU 负载。
