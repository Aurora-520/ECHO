# ECHO

ECHO 是面向立创天猛星 MSPM0G3507 的电赛控制类 FreeRTOS 通用工程。目标是在加入底盘、
云台和视觉前，先建立可测量、可调参、可诊断和可安全扩展的平台。

## 当前状态

- 已验收至 Phase 1F：赛场可操作性、健康诊断、参数/UI 和持久化门禁。
- MCLK 80 MHz，FreeRTOS Tick 1 kHz，TIMG12 标称 1 MHz 微秒时基。
- SystemTask 100 Hz；当前仍发布测试信号，不是真实电机 PID。
- UART1 PA8/PA9、230400 baud、DMA TX/FIFO IRQ RX。
- 100 Hz Control、1 Hz Health 和参数 ACK 共用二进制协议与全局 sequence。
- ServiceTask 是 `SystemHealthSnapshot` 的唯一写入者；OLED、遥测和 Watch 共用语义。
- SSD1306 使用 I2C0 PA0/PA1、400 kHz、地址 0x3C，提供五页诊断 UI。
- `kp/ki/kd/target` 共用 metadata、校验、pending 和 100 Hz 周期边界应用。
- 当前执行器 armed/output permitted 始终为 0；未接电机、编码器、IMU、云台或树莓派。
- 物理 ADC 五键、Flash 掉电保存和硬件看门狗为明确 `deferred`，不能描述为已通过。

权威提交、实测数字、硬件状态和下一步见
[`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md)。

## 开始工作

新的开发任务按顺序读取：

```text
AGENTS.md
-> docs/PROJECT_STATUS.md
-> 当前 Phase 文档
-> 本任务直接相关的源码和硬件资料
```

长期阶段顺序与闭环见 [`docs/CURRENT_WORKFLOW.md`](docs/CURRENT_WORKFLOW.md)，架构红线见
[`docs/ENGINEERING_RED_LINES.md`](docs/ENGINEERING_RED_LINES.md)。

## 构建与烧录

Keil 打开工程根目录的 `ECHO.uvmpw`。VSCode 打开整个 ECHO 文件夹，常用任务：

```text
ECHO: Check Environment
ECHO: Sync Local Paths
ECHO: Build App
ECHO: Rebuild FreeRTOS + App
ECHO: Build + Flash (DAPLink)
ECHO: Debug (DAPLink + OpenOCD)
```

本机路径集中在不提交的 `config/local_paths.ps1`。烧录脚本先尝试 OpenOCD 目标端校验；
若 MSPM0 运行状态使 CRC 算法无法 halt，会自动执行逐字节 Flash readback 和 SHA-256 比较，
只有一致才 reset run。

## 运行结构

```text
main
  -> BSP_Reset_Capture
  -> SYSCFG_DL_init / BSP_Time_Init
  -> Serial / Parameter / RTOS / SystemHealth init
  -> AppTasks_CreateAll / scheduler

SystemTask, 100 Hz
  -> 周期边界应用一个 pending 参数事务
  -> 发布非阻塞 Control telemetry
  -> 记录 period / execution / jitter / deadline

ServiceTask, 2 ms
  -> 解析 UART 参数协议 / 服务 TX DMA
  -> 每 100 ms 写统一健康快照
  -> 每 1 s 发布 Health telemetry

TelemetryTask, event driven
  -> 编码 Control / Health / ACK
  -> 非阻塞写入 1024 B TX ring

DisplayTask, lowest priority
  -> 消费虚拟 UI 事件
  -> 渲染 Overview / RTOS / COMM / DEVICE / PARAM
  -> 协调 UART quiet window 并刷新 SSD1306
```

## 工具

所有串口工具要求显式 Port，不把本次 COM 号写成永久配置：

```powershell
tools\telemetry_capture.ps1 -Port COM4 -DurationSeconds 120
tools\parameter_set.ps1 -Port COM4 -Parameter kp -Value 1.2
tools\protocol_stress_test.ps1 -Port COM4
tools\phase1f_field_check.ps1 -Port COM4 -DurationSeconds 120
```

`telemetry_capture.ps1` 分别统计 Control 与 Health，并可输出 Control CSV 和机器 JSON。
`phase1f_field_check.ps1` 串联环境、全量构建、可选烧录、联合采集和失败层级摘要。

暂停 MCU 后可在 Watch 中查看：

```text
g_system_health_snapshot
g_system_health_diag
g_rtos_diag
g_telemetry_diag
g_serial_tx_diag
g_parameter_service_diag
g_display_task_diag
g_ssd1306_diag
g_bsp_i2c_diag
g_bsp_reset_diag
```

最终实时验收必须断开调试器，使用 UART 连续采集；断点结果不能代替周期证据。

## 文档地图

- [`AGENTS.md`](AGENTS.md)：未来任务必须遵守的工程、硬件、Git 和证据规则
- [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md)：当前权威状态与 Phase 2A 门槛
- [`docs/phases/PHASE1F_OPERABILITY_DIAGNOSTICS.md`](docs/phases/PHASE1F_OPERABILITY_DIAGNOSTICS.md)：Phase 1F 设计与验收
- [`docs/worklogs/2026-07-15_phase1f_operability_diagnostics.md`](docs/worklogs/2026-07-15_phase1f_operability_diagnostics.md)：本次实现和实测摘要
- [`docs/learning/PHASE1F_OPERABILITY_DIAGNOSTICS.md`](docs/learning/PHASE1F_OPERABILITY_DIAGNOSTICS.md)：可复用原理与排障方法
- [`docs/HARDWARE_TOOLCHAIN_SOURCES.md`](docs/HARDWARE_TOOLCHAIN_SOURCES.md)：硬件、工具链和资料来源
- [`DEBUGGING.md`](DEBUGGING.md)：DAPLink、断点和 Watch 使用方法

## 安全

DAPLink 连接 GND、SWDIO、SWCLK 和 nRESET。不要把 DAPLink 3V3 接到已经供电的 3.3 V
电源轨，只使用确认不异常发热的板。

涉及电机、云台、4S 电池或高功率输出时，必须由用户在现场明确许可，先架空机构、默认
零输出并准备物理断电。Phase 1F 完成不代表最终工程完成。
