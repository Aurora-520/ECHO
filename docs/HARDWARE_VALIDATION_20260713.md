# Hardware validation - 2026-07-13

测试对象：正常、不发热的天猛星 MSPM0G3507 板和 DAPLink CMSIS-DAP v2。

```text
DAPLink serial: 4CDD7B98801CB180A5B29C09725A99B3
SWD DPIDR:     0x6BA02477
SWD clock:     1 MHz
Target:        Cortex-M0+ r0p1, 4 breakpoints, 2 watchpoints
```

## Result matrix

| 链路 | 结果 | 证据 |
| --- | --- | --- |
| Keil 全量编译 | 通过 | FreeRTOS 与 ECHO 均为 0 errors, 0 warnings |
| Keil DAPLink 下载 | 通过 | Erase Done, Programming Done, Verify OK |
| VSCode Build App | 通过 | ECHO 为 0 errors, 0 warnings |
| VSCode/OpenOCD 烧录 | 通过 | Programming Finished, Verified OK, Resetting Target |
| VSCode/OpenOCD + GDB 调试 | 通过 | 在 `main` 和 `main-blinky.c:208` 命中硬件断点 |
| FreeRTOS 周期运行 | 通过 | 同一断点连续命中，`xTickCount` 为 1000 和 2000 |

在 `main-blinky.c:208` 停止时读取到：

```text
PC = 0x0000083E
SP = 0x20200470
xTickCount = 1000, then 2000
uxSchedulerSuspended = 0
```

断点位于 LED 翻转语句之后，因此两次命中证明接收任务每 1 秒执行并翻转一次
PB22。测试结束前已删除断点并执行 `reset run`。

Keil 的命令行 `-f` 下载路径本次已经实机验证。uVision 的交互式 Debug
按钮不能通过当前命令行可靠地无界面自动触发，因此本次没有把 Keil 单步/断点
重复计算为自动化通过；该功能在阶段 1A 之前已由用户手动在第 208 行验证通过，
且本次未修改 `ECHO.uvoptx` 的 Keil 调试配置。

## Expected warnings

GNU GDB 读取 ArmClang AXF 时仍会显示：

```text
Loadable section "RW_IRAM2" outside of ELF segments
```

这不是烧录失败。下载使用完整的 `ECHO.hex`，Flash 内容已通过 OpenOCD 校验。
