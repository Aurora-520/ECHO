# Phase 1B FreeRTOS skeleton

记录日期：2026-07-14

## 目标

Phase 1B 将 TI blinky 示例替换为 ECHO 自己的最小运行骨架。此阶段保持
32 MHz 和 PB22 每秒翻转行为，不接入电机、IMU、OLED、树莓派或云台。

## 已实现结构

~~~text
main
  -> SYSCFG_DL_init
  -> RtosDiagnostics_Init
  -> AppTasks_CreateAll
  -> vTaskStartScheduler

SystemTask, 100 Hz
  -> 每秒向长度1静态队列覆盖写入最新心跳

ServiceTask
  -> 接收心跳
  -> BSP_LED_Toggle
  -> 刷新 g_rtos_diag
~~~

应用代码不直接调用 DL_GPIO。PB22 的手写 DriverLib 调用只存在于
bsp/source/bsp_led.c。

## 静态对象

| 对象 | 优先级 | 分配 |
| --- | ---: | ---: |
| SystemTask | 2 | 256 words / 1024 bytes |
| ServiceTask | 1 | 256 words / 1024 bytes |
| Timer daemon | 3 | 128 words / 512 bytes |
| Idle Task | 0 | 128 words / 512 bytes |
| HeartbeatQ | - | 长度1，uint32_t |
| MSP/中断栈 | - | 1024 bytes |

System、Service、Idle、Timer 和 HeartbeatQ 均使用静态内存。动态分配支持
暂时保留，以兼容 TI DPL 和提供 heap_4 诊断。启动时只执行一次1字节
分配并立即释放，用于初始化 heap_4 记账；任务和队列没有动态创建。

TI 默认 AppHooks_freertos.c 和 StaticAllocs_freertos.c 已退出内核工程，
Idle/Timer内存和故障hook全部由 platform/freertos 提供。

## 诊断契约

在 Keil 或 VSCode Watch 中查看：

~~~text
g_rtos_diag
~~~

diagnostics_update_sequence 在刷新开始时变为奇数，全部字段写完后变为偶数。
读取者只接受相同且为偶数的前后序列号。diagnostics_valid 为1后，栈字段
才是实测值。

每个任务栈同时记录：

- allocated_words：分配总量；
- min_free_words：启动以来历史最少未用量；
- max_used_words：历史最大使用量。

栈单位为 StackType_t words，在 MSPM0 上每 word 为4 bytes。MSP/中断栈
不属于 FreeRTOS 任务栈，本阶段只先扩大到1 KiB，后续外设中断增加前仍需
加入独立水位监测。

## 故障处理

以下故障统一写入 sticky 诊断并关闭中断停机：

1. configASSERT；
2. malloc失败；
3. 任务栈溢出；
4. 静态队列创建失败；
5. SystemTask或ServiceTask创建失败；
6. 调度器意外返回；
7. 心跳队列发送失败；
8. ServiceTask心跳超时。

故障上下文先写入，fault_code 最后发布。无调试器时不执行 BKPT，避免
Cortex-M0+ 因断点指令进入额外 HardFault。

## 构建验收

~~~text
FreeRTOS library: 0 Error(s), 0 Warning(s)
ECHO application: 0 Error(s), 0 Warning(s)
Program size: Code 9044, RO-data 628, RW-data 4, ZI-data 8636 bytes
~~~

SysConfig 2.10.0 会额外输出一条 Project Configuration File generation
is disabled 工具提示。生成文件显示 Unchanged，Keil C/汇编编译汇总为
0 warnings。本阶段保留 TI 内核工程原有 warning level 0；App 使用 level 4。

最终产物 SHA-256：

~~~text
ECHO.hex
47F72B632CD12AD9B82E65FF9F38CE7D385D47F295634D95C15224305FFF3E4F

ECHO.axf
4B2B0B24E68471038D13786FFB4FDDC3F79AA196FF2F3A47D76D41D15E7EAB68

freertos_ECHO.lib
37DA1A04713FE00B6206A1F19A7AE5A6B9446691F8B482A1AFC5C3A0A8445415
~~~

Note: ECHO.hex is the stable firmware identity. AXF embeds debug paths and the
static library archive embeds build metadata, so those two hashes are records
of this build rather than cross-path reproducibility checks.

## 下载与调试验收

- DAPLink CMSIS-DAP v2，SWD 1 MHz；
- 探针序列号 4CDD7B98801CB180A5B29C09725A99B3；
- MSPM0G3507识别成功；
- Flash写入完成；
- OpenOCD快速CRC超时后回退逐字节比较，最终 Verified OK；
- 复位成功；
- AXF中的 App、BSP 和 platform/freertos 源码路径均为 E:\ECHO；
- GDB可暂停、读取 g_rtos_diag、恢复并断开；
- 暂停在 prvIdleTask 或 prvCheckTasksWaitingTermination 是正常 Idle 状态。

## 连续运行终验

最终候选固件从复位后不设置断点、不暂停运行，终点一次性读取：

~~~text
连续运行              794000 ticks = 794 seconds
SystemTask次数         79410
ServiceTask次数        794
队列发送/接收          794 / 794
LED翻转                794
deadline miss          0
队列发送失败           0
fault code/count       0 / 0
diagnostics sequence   1588，偶数
heap current/min       3064 / 3064 bytes
~~~

周期关系：

~~~text
79410 * 10   = 794100 = system_task_last_wake_tick
794 * 1000   = 794000 = last_led_toggle_tick
Service = send = receive = LED = heartbeat sequence = 794
~~~

栈终点：

| 任务 | 分配 | 历史最少未用 | 历史最大使用 |
| --- | ---: | ---: | ---: |
| System | 256 | 225 | 31 words / 124 bytes |
| Service | 256 | 225 | 31 words / 124 bytes |
| Idle | 128 | 103 | 25 words / 100 bytes |
| Timer | 128 | 103 | 25 words / 100 bytes |

四个任务Handle和队列Handle均非空，四个任务Handle互不相同。压力测试
超过10分钟要求，期间无复位、无调试暂停、无deadline miss、无内存下降、
无队列错误和无故障。

## 工程工具改进

- Keil和VSCode include路径均包含 app、app/tasks、bsp/include、config
  和 platform/freertos；
- VSCode和Keil统一使用C99；
- 路径同步不会删除 Application、BSP、Platform 文件组；
- 内核工程或SDK路径更新后，Build App会自动重建FreeRTOS库；
- App工程文件更新后会自动触发App Rebuild；
- 构建脚本把Keil最终汇总中的任何warning视为失败。

## 结论与下一阶段

Phase 1B 软件、构建、烧录、GDB后端和连续运行验收通过。PB22软件输出
严格每1000 ticks翻转一次，物理亮灭仍以现场肉眼或逻辑分析仪为最终证据。

下一阶段为 Phase 1C：80 MHz时钟、1 MHz硬件时间戳和周期抖动测量。
本阶段不提前加入UART、OLED或电机。
