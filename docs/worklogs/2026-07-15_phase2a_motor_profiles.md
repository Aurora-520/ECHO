# Phase 2A 编译时 Motor Profile

日期：2026-07-15

## 目标

- MG370 与未来 513X 共用现有 BSP、固定引脚、执行器安全状态机、转速换算和 PID 接口。
- 只用一个编译时宏选择型号，禁止 OLED 或 UART 运行时切换。
- 未确认参数不编造；513X 不允许生成可驱动固件。

## 实现

新增：

- `module/service/motor_profile_config.h`
- `module/service/motor_profile.h`
- `module/service/motor_profile.c`

`ECHO_MOTOR_PROFILE_SELECTION` 默认选择 MG370。Profile 包含型号/schema/profile version、额定
电压/电流、堵转电流、减速比、编码器接口/电平/PPR、最高转速、起转/最大 PWM、速度/加速度
限制、堵转阈值/时间、PID 和左右轮独立的输出符号、编码符号、解码倍频与 CPR。

MG370 已确认字段：12 V、1.1 A、堵转规格 6.2 A、34.014、GMR AB 3.3 V、500 PPR、
300 rpm、左 68,028 x4/+1、右 17,007 x1/-1。CPR 标记 provisional；左右电机输出符号为 0。

未确认的起转 PWM、运行最大 PWM、速度/加速度限制、堵转保护阈值/时间和 PID 均为 0，且对应
有效位未置位。Profile 允许现有 10%/500 ms 电气安全点动，但 `closed_loop_ready=0`。

513X 占位结构存在；选择它时 `#error` 明确要求额定电压、堵转电流、减速比、编码器接口、
信号电平和 PPR。

SystemTask 每 100 Hz 使用左右 CPR 与编码符号计算输出轴 RPM 到 `g_motor_profile_diag`。
ServiceTask 每 1 秒发布 telemetry frame type 7；采集工具显示型号、版本、有效字段、CPR、符号和倍频。

## 验证

| 项目 | 结果 |
| --- | --- |
| 固定引脚/BSP | 未增加或复制；PB8/PB9/PB12/PB13、PA29/PA30、PB6/PB7 不变 |
| SysConfig | 0 error；原有 ProjectConfig warning 与 PWM/QEI retention 提示 |
| FreeRTOS rebuild | 0 Error / 0 Warning |
| App rebuild | 0 Error / 0 Warning；Code=58,152，RO=3,296，RW=28，ZI=16,644 |
| HEX SHA-256 | `45D3035850AC9460232A75051FA1958F7F907187400E1C048274D1920F73CBC0` |
| 513X 负向编译 | passed；ArmClang 1 error，命中预期锁定文案 |
| Profile frame fixture | passed；52 B frame，CRC/gap/unknown=0，MG370 v1 字段全部匹配 |
| PowerShell AST | passed |
| `git diff --check` | 仅既有 SysConfig generated 两行空白；未手改 generated |

## 硬件状态

本次只构建和执行文件 fixture，没有烧录。板上仍运行上一版已验收的 AT8236 安全 PWM 固件；
未连接或驱动电机，未改变 VM/4S 状态。

## 未完成

- 用户再次确认现场安全后烧录并验证 1 Hz Profile 遥测和 RPM 诊断。
- 带动力冻结左右 `motor_output_sign`。
- 标定起转/最大 PWM、速度/加速度、堵转保护和 PID 后置 `closed_loop_ready`。
- 513X 到货并确认全部关键参数后解除编译锁。
