# ECHO DAPLink 调试

## 连接

只使用确认正常、不发热的天猛星板。

~~~text
DAPLink GND    -> 板卡 GND
DAPLink SWDIO  -> PA19
DAPLink SWCLK  -> PA20
DAPLink nRESET -> 板卡 nRESET
~~~

板卡单独供电。不要把 DAPLink 的 3V3/VCC 输出接到已经供电的 3.3V
电源轨。明确标记为 VTref 或 TVCC 的参考电压脚才可以按调试器说明连接。

## 构建和烧录

在 VSCode 中运行：

~~~text
Terminal -> Run Task -> ECHO: Build + Flash (DAPLink)
~~~

任务会构建 App，使用 OpenOCD 下载并校验 ECHO.hex，随后复位目标。
调试配置同样下载 HEX；ECHO.axf 用于源码、符号、变量和调用栈。

## 开始调试

1. 使用 Ctrl+Shift+D 打开 Run and Debug。
2. 选择 ECHO: Debug (DAPLink + OpenOCD)。
3. 按 F5。
4. 工程会重新构建、下载并停在 main。
5. 再按一次 F5 才会连续运行。

常用快捷键：

~~~text
F9        添加或取消断点
F5        开始调试或继续运行
F10       单步跳过
F11       单步进入
Shift+F11 单步跳出
Pause     暂停整个 MCU
Ctrl+Shift+F5 重新启动调试
Shift+F5 结束调试
~~~

## FreeRTOS 断点

断点命中时停止的是整个 CPU，不只是当前 FreeRTOS 任务。Tick、中断、
队列和其他任务都会暂停，因此不能根据单步调试判断真实控制周期。

Phase 1B 可在 BSP_LED_Toggle() 或 ServiceTask_Entry() 的 LED 调用之后
设置断点。每按一次 F5，程序继续运行约 1 秒，再次翻转 LED 并停下。
一次变亮、下一次变灭是正常现象。

调试电机前禁止在控制路径随意下断点。CPU 停止后，PWM 或串口执行器
可能保持最后一条命令。

## Watch 诊断

在 Watch 中添加：

~~~text
g_rtos_diag
~~~

| 字段 | 含义 |
| --- | --- |
| scheduler_started | 调度器已经开始运行 |
| system_task_run_count | SystemTask 10 ms 周期次数 |
| service_task_run_count | ServiceTask 已处理心跳数 |
| led_toggle_count | PB22 软件翻转次数 |
| last_led_toggle_tick | 最近一次翻转的 RTOS Tick |
| *_stack_min_free_words | 启动以来历史最少未用栈，单位为 word |
| heap_free_bytes | 当前 FreeRTOS heap 剩余字节 |
| heap_min_ever_free_bytes | 历史最小 heap 剩余字节 |
| fault_code | 0 表示没有已记录 RTOS 故障 |
| fault_file / fault_line | configASSERT 失败位置 |

MSPM0 的 StackType_t 为 4 bytes。例如栈高水位为 100 words，代表历史
最少保留约 400 bytes。Watch 只在 CPU 暂停时刷新。

Phase 1B 的周期关系应满足：

~~~text
system_task_run_count * 10 ~= system_task_last_wake_tick
led_toggle_count * 1000 == last_led_toggle_tick
service_task_run_count == queue_send_count
service_task_run_count == queue_receive_count
fault_code == 0
~~~

## 常见调试输出

SIGINT 通常表示按下 Pause 暂停 CPU，本身不是固件故障。

GNU GDB 读取 ArmClang AXF 时可能显示：

~~~text
RW_IRAM2 outside of ELF segments
pc ... not in symtab
~~~

下载使用完整 HEX，因此这些信息不妨碍正常源码断点。若出现
Could not find MEM-AP、Failed to read device ID 或 Target not examined，
则是连接问题，需要检查供电、共地、SWD 和 nRESET。

程序暂停在 prvCheckTasksWaitingTermination() 通常表示当时正在运行
Idle Task，不代表任务正在被删除，也不代表系统故障。

OpenOCD 是主调试链，pyOCD 仅作备用。同一个 DAPLink 同一时间只能由
一个 Keil、OpenOCD 或 GDB 会话占用。

## 已验证链路

~~~text
VSCode Cortex-Debug
-> GNU Arm GDB 14.2
-> xPack OpenOCD 0.12.0+dev
-> DAPLink CMSIS-DAP v2
-> MSPM0G3507 Cortex-M0+
~~~

已验证目标检测、1 MHz SWD、Flash 下载和校验、复位、源码符号、
硬件断点、变量读取、暂停、继续和断开后恢复运行。

## 电机安全

调试电机代码前：

- 架空驱动轮；
- 默认关闭驱动使能；
- 保留物理急停和快速断电；
- 不把调试器断开动作当作急停；
- 不在电机带载时暂停尚未具备硬件安全门的控制任务。
