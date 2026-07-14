# ECHO

ECHO 是面向立创天猛星 MSPM0G3507 的电赛控制类 FreeRTOS 通用工程。

## 当前状态

- Keil MDK 5.39 + Arm Compiler 6.21
- MSPM0 SDK 2.10.00.04
- TI MSPM0G1X0X_G3X0X DFP 1.3.1
- FreeRTOS V11.2.0，原生 API
- MCLK 80 MHz，FreeRTOS Tick 1 kHz
- TIMG12 作为 32 位、1 MHz 自由运行微秒时基
- SystemTask 以 100 Hz 运行，每秒发布一次心跳
- ServiceTask 接收心跳并每秒翻转一次 PB22 LED
- 应用任务、心跳队列、Idle Task 和 Timer Task 均使用静态内存
- `g_rtos_diag` 可观察任务、栈、堆、故障和微秒级周期诊断
- 暂未接入底盘、IMU、OLED、树莓派或云台

## 打开与构建

Keil 打开工程根目录下的 `ECHO.uvmpw`。完整重建时先构建
`freertos_ECHO`，再构建 `ECHO`。

VSCode 打开整个 ECHO 文件夹，然后运行：

```text
ECHO: Build App
ECHO: Rebuild FreeRTOS + App
ECHO: Build + Flash (DAPLink)
ECHO: Debug (DAPLink + OpenOCD)
```

烧录脚本会先尝试 OpenOCD 快速 CRC 校验。如果 MSPM0 的目标端 CRC
算法超时，会自动切换为 Flash 逐字节读回和 SHA-256 比较。只有读回内容
与 HEX 完全一致才会复位运行并报告成功。

## 本机路径

SDK 和工具链路径集中在：

```text
config/local_paths.ps1
```

换电脑或移动 SDK、Keil、OpenOCD 后：

1. 参考 `config/local_paths.example.ps1` 修改本机路径。
2. 运行 `ECHO: Check Environment`。
3. 运行 `ECHO: Sync Local Paths`。
4. 运行 `ECHO: Rebuild FreeRTOS + App`。

工程内部源码使用相对路径，可以整体移动 ECHO。不要只移动 `keil`、`app`
或 `freertos` 中的单个目录。

## 运行结构

```text
main
  -> SYSCFG_DL_init
  -> BSP_Time_Init
  -> RtosDiagnostics_Init
  -> AppTasks_CreateAll
  -> vTaskStartScheduler

SystemTask, 100 Hz
  -> BSP_Time_GetUs
  -> 每秒 xQueueOverwrite 心跳
  -> 更新周期、抖动、执行时间、迟到和 deadline 诊断

ServiceTask
  -> 接收心跳
  -> BSP_LED_Toggle
  -> 更新栈、堆和任务诊断
```

App 不直接操作 GPIO 或 Timer DriverLib。LED DriverLib 只存在于
`bsp/source/bsp_led.c`，时间基准 DriverLib 只存在于
`bsp/source/bsp_time.c`。

在 Keil 或 VSCode Watch 中添加：

```text
g_rtos_diag
```

## 文档

- `docs/PHASE1A_BASELINE.md`：可移动工程基线
- `docs/PHASE1B_RTOS_SKELETON.md`：静态任务与运行验收
- `docs/PHASE1C_CLOCK_TIMEBASE.md`：80 MHz、1 MHz 时基和抖动诊断
- `docs/ARCHITECTURE_BOUNDARIES.md`：目录和依赖边界
- `DEBUGGING.md`：DAPLink、断点和 Watch 使用方法

## 调试安全

DAPLink 使用 CMSIS-DAP + SWD，连接 GND、SWDIO、SWCLK 和 nRESET。
不要把 DAPLink 的 3V3 输出接到已经供电的 3.3V 电源轨。只使用确认正常、
不会异常发热的天猛星板。
