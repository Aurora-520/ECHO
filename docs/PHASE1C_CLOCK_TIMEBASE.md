# Phase 1C clock and timebase

记录日期：2026-07-14

## 范围

Phase 1C 只建立系统时钟、硬件微秒时基和 SystemTask 时间诊断，不接入
UART、OLED、IMU、电机、树莓派或云台。

## 80 MHz 系统时钟

时钟链为：

```text
内部 SYSOSC 32 MHz
  -> SYSPLL
  -> HSCLK / MCLK 80 MHz
```

本方案不依赖天猛星板载 40 MHz 高速晶振或 32.768 kHz 晶振。SysConfig
生成代码包含：

- Flash wait state 2
- ULPCLK 除以 2，保持在 40 MHz
- SYSPLL_ERR_01/FCC 锁定校验
- `CPUCLK_FREQ 80000000`

`FreeRTOSConfig.h` 中的 `configCPU_CLOCK_HZ` 同步为 80000000，
`configTICK_RATE_HZ` 保持 1000。最终 AXF 的 SysTick LOAD 为
79999，对应 `80 MHz / 1000 - 1`。

## 1 MHz 硬件时基

TIMG12 是 MSPM0G3507 的 32 位 TimerG，当前未被其他模块占用。配置为：

```text
MFCLK 4 MHz
  -> Timer input divider /4
  -> TIMG12 1 MHz
  -> PERIODIC_UP
  -> LOAD 0xFFFFFFFF
```

每个计数为标称 1 us，完整回绕周期为 4294.967296 秒，约 71 分 35 秒。
时基不使用中断，也不占用引脚。

`BSP_Time_GetUs()` 返回 32 位计数。所有耗时计算必须使用无符号模减：

```c
uint32_t elapsed_us = (uint32_t)(now_us - start_us);
```

只要被测间隔小于一次完整回绕，该写法可以跨越 `0xFFFFFFFF -> 0`。
需要判断早晚的有符号时间差限制在半个回绕周期内，约 35.8 分钟。

TIMG12 使用 `DL_TIMER_CORE_HALT_IMMEDIATE`。调试器暂停 CPU 时，硬件时基
与 Cortex SysTick 一起冻结，因此断点不会制造虚假的巨大迟到或抖动。

## SystemTask 时间诊断

SystemTask 的目标周期为 10000 us。每次激活记录：

```text
period_us       = start[n] - start[n-1]
period_error_us = period_us - 10000
jitter_us       = abs(period_error_us)
release_error   = actual_start - expected_release
lateness_us     = max(release_error, 0)
execution_us    = workload_finish - workload_start
deadline        = expected_release + 10000
overrun_us      = max(workload_finish - deadline, 0)
```

`execution_us` 表示 SystemTask 功能工作段，包括计数、心跳判断和队列覆盖，
不包含 `RtosDiagnostics_RecordSystemTiming()` 自身的记录开销。deadline 也以
功能工作完成点为准。

如果 `xTaskDelayUntil()` 返回 `pdFALSE`，当前样本仍按原计划释放点和截止期
记录真实迟到与超期，记录结束后才为下一周期重同步。

32 位微秒快照使用独立奇偶序列 `system_timing_update_sequence`。偶数表示
一次完整更新已经结束。`system_last_sample_valid` 为 1 才能使用当前样本；
历史最小值和最大值只由有效样本更新。

## Watch 字段

在 Keil 或 VSCode 暂停后查看 `g_rtos_diag`，重点字段为：

```text
configured_cpu_clock_hz
timebase_frequency_hz
system_timing_update_sequence
system_last_sample_valid
system_period_target_us
system_last_period_us
system_min_period_us
system_max_period_us
system_last_period_error_us
system_last_jitter_us
system_max_jitter_us
system_last_execution_us
system_max_execution_us
system_last_release_error_us
system_max_lateness_us
system_deadline_miss_count
system_delay_no_block_count
system_timing_resync_count
system_timing_invalid_count
fault_code
```

VSCode Cortex-Debug 的普通 Watch 只在 MCU 暂停时可靠刷新。全速运行时若要
连续画 PID 曲线，应使用非阻塞遥测通道，例如 UART DMA、树莓派通信或后续
专用调参协议，而不是依赖断点 Watch。

## 构建与硬件验收

```text
FreeRTOS library: 0 Error(s), 0 Warning(s)
ECHO application: 0 Error(s), 0 Warning(s)
Program size: Code 11260, RO-data 692, RW-data 4, ZI-data 8756 bytes
```

DAPLink 序列号：`4CDD7B98801CB180A5B29C09725A99B3`。

Flash 快速 CRC 算法在当前 OpenOCD/MSPM0 组合上会超时，烧录脚本已自动
回退为逐字节读回和 SHA-256 比较。最终候选固件读回一致。

100 秒硬件快照：

```text
configured CPU clock     80000000 Hz
timebase                 1000000 Hz
FreeRTOS uptime          100000 ticks
SystemTask               10001
Service/send/receive/LED 100 / 100 / 100 / 100
period min/max           9999 / 10000 us
max jitter               1 us
max workload execution   6 us
max lateness             0 us
deadline miss            0
delay no-block/resync    0 / 0
invalid sample/fault     0 / 0
```

暂停 MCU 约 2 秒前后，TIMG12 CTR 均为 `0x023C3480`，FreeRTOS Tick 均为
37000；恢复后没有增加 lateness、deadline miss、resync 或 invalid count。

连续运行终验从同一次目标复位开始，中途只短暂停止读取快照并立即恢复。终点：

```text
continuous run           704000 ticks = 704 seconds
SystemTask               70473
ServiceTask              704
queue send/receive       704 / 704
LED toggle               704
timing sequence          140946, even
valid timing samples     70472
period min/max           9999 / 10000 us
max jitter               1 us
max workload execution   7 us
max lateness             0 us
deadline miss            0
delay no-block/resync    0 / 0
invalid sample/fault     0 / 0
stack min-free words     225 / 225 / 103 / 104
heap current/minimum     3056 / 3056 bytes
```

关系检查成立：

```text
Service = send = receive = LED = 704
system_timing_update_sequence = 2 * SystemTask = 140946
```

## 已知边界

- 当前没有让硬件实际运行到 71.6 分钟回绕点的自动测试。
- 当前没有自动注入 35.8 分钟无效半周期或强制 deadline miss 的测试。
- 32 位模减和重同步路径已经过代码审查，后续可增加表驱动主机测试。
- 当前 1 MHz 精度由内部 SYSOSC 决定，适合控制周期和抖动测量，不是高精度
  计量时钟。
