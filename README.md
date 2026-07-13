# ECHO

ECHO 是立创天猛星 MSPM0G3507 的电赛控制类 FreeRTOS 基线工程。

## 当前状态

- Keil MDK 5.39 + Arm Compiler 6.21
- MSPM0 SDK 2.10.00.04
- TI MSPM0G1X0X_G3X0X DFP 1.3.1
- FreeRTOS V11.2.0
- 当前系统时钟为 32 MHz，不是目标架构中的 80 MHz
- 两个任务通过队列通信，PB22 板载 LED 亮 1 秒、灭 1 秒
- 暂未接入底盘、IMU、OLED、树莓派或云台

## 打开与构建

Keil 打开工程根目录下的 `ECHO.uvmpw`。完整重建时先构建
`freertos_ECHO`，再构建 `ECHO`。

VSCode 默认构建任务为 `ECHO: Build App`。需要从头重建 FreeRTOS 静态库时，
运行 `ECHO: Rebuild FreeRTOS + App`。

## 本机路径配置

所有 SDK 和工具链位置集中在：

```text
config/local_paths.ps1
```

换电脑或移动 SDK/Keil/OpenOCD 后：

1. 参考 `config/local_paths.example.ps1` 修改本机路径。
2. 运行 VSCode 任务 `ECHO: Check Environment`。
3. 运行 `ECHO: Sync Local Paths`，更新 Keil 和 VSCode 中的已解析路径。
4. 运行 `ECHO: Rebuild FreeRTOS + App`。

工程内部文件使用相对路径，因此同一台电脑上可以整体移动 ECHO 文件夹。
不要只移动 `keil`、`app` 或 `freertos` 中的单个目录。

项目内 `.tools/pyocd` 是旧位置绑定的备用虚拟环境，不属于可移动基线。
主调试链使用 OpenOCD，不依赖该虚拟环境。

## 目录边界

阶段 1A 只创建目录骨架，不改变现有程序行为，也不把新目录加入 Keil 构建。
边界说明见 `docs/ARCHITECTURE_BOUNDARIES.md`，基线记录见
`docs/PHASE1A_BASELINE.md`。

## 下载与调试

DAPLink 使用 CMSIS-DAP + SWD，建议连接 GND、SWDIO、SWCLK 和 nRESET。
VSCode 调试用 AXF 读取符号、用 HEX 完整下载镜像。详细步骤见 `DEBUGGING.md`。
