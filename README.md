# ECHO

ECHO 是立创天猛星 MSPM0G3507 的电赛控制类 FreeRTOS 基线工程。

## 当前状态

- Keil MDK 5.39 + Arm Compiler 6.21
- MSPM0 SDK 2.10.00.04
- TI MSPM0G1X0X_G3X0X DFP 1.3.1
- FreeRTOS V11.2.0
- 当前系统时钟为 32 MHz，80 MHz 留到 Phase 1C 单独验证
- SystemTask 以 100 Hz 运行并每秒发布一次心跳
- ServiceTask 接收静态队列心跳并每秒翻转一次 PB22 LED
- 应用任务、心跳队列、Idle 和 Timer 任务均使用静态内存
- g_rtos_diag 可直接观察任务、栈、堆和故障状态
- 暂未接入底盘、IMU、OLED、树莓派或云台

## 打开与构建

Keil 打开工程根目录下的 ECHO.uvmpw。完整重建时先构建
freertos_ECHO，再构建 ECHO。

VSCode 默认构建任务为 ECHO: Build App。修改 FreeRTOSConfig.h 或需要
从头重建时，运行 ECHO: Rebuild FreeRTOS + App。

构建脚本要求 FreeRTOS 库和 App 都达到 0 errors, 0 warnings。

## 本机路径配置

所有 SDK 和工具链位置集中在：

~~~text
config/local_paths.ps1
~~~

换电脑或移动 SDK、Keil、OpenOCD 后：

1. 参考 config/local_paths.example.ps1 修改本机路径。
2. 运行 VSCode 任务 ECHO: Check Environment。
3. 运行 ECHO: Sync Local Paths。
4. 运行 ECHO: Rebuild FreeRTOS + App。

工程内部源码使用相对路径，因此同一台电脑上可以整体移动 ECHO。
不要只移动 keil、app 或 freertos 中的单个目录。

项目内 .tools/pyocd 是旧位置绑定的备用虚拟环境。主调试链使用
OpenOCD，不依赖该虚拟环境。

## Phase 1B 运行结构

~~~text
SystemTask (100 Hz)
    -> 长度 1 静态 HeartbeatQ，每 1 秒 xQueueOverwrite
ServiceTask
    -> 接收心跳
    -> BSP_LED_Toggle
    -> 更新 g_rtos_diag
~~~

App 不直接操作 LED 引脚；唯一的 DL_GPIO_togglePins 位于
bsp/source/bsp_led.c。

在 Keil 或 VSCode Watch 中添加：

~~~text
g_rtos_diag
~~~

栈字段的单位是 StackType_t 字数，在 MSPM0 上 1 word 为 4 bytes。

## 文档

- docs/PHASE1A_BASELINE.md：可移动工程基线
- docs/PHASE1B_RTOS_SKELETON.md：静态任务与运行验收
- docs/ARCHITECTURE_BOUNDARIES.md：目录和依赖边界
- DEBUGGING.md：DAPLink、断点和 Watch 使用方法

## 下载与调试

DAPLink 使用 CMSIS-DAP + SWD，连接 GND、SWDIO、SWCLK 和 nRESET。
VSCode 使用 AXF 读取符号、使用 HEX 下载完整镜像。不要把 DAPLink
3V3 输出接到已经供电的 3.3V 电源轨。
