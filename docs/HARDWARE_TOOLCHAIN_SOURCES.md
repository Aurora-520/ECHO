# ECHO 硬件、工具链与资料来源

最后核对：2026-07-16

本文件记录“事实从哪里来”。它不是把所有资料复制进仓库，而是让新任务知道先查哪份
手册、哪些路径只在本机存在，以及哪些方案仍未冻结。

## 1. 资料优先级

出现矛盾时按以下顺序处理：

1. 芯片/模块官方数据手册、勘误和 TI SDK 文档。
2. 天猛星原理图、引脚表和厂商资料。
3. 当前 ECHO 已板测并形成阶段记录的代码与诊断数据。
4. 所用模块的明确版本手册，例如 X42S UART V2.0。
5. 开源工程和获奖方案，用于学习结构与思路。
6. 旧 STM32 工程，用于理解用户习惯和算法，不直接当作 MSPM0 驱动依据。
7. 视频、论坛和聊天内容，只作线索，必须回到手册或实测验证。

不得因为开源项目“能跑”就整段复制其驱动、引脚和时钟假设。先确认许可证，再提取通用
设计，按 ECHO 分层和目标硬件重写/适配。

## 2. 本机资料目录

| 路径 | 用途 | 备注 |
| --- | --- | --- |
| `E:\电赛相关资料` | 队伍手册、数据和长期 Word 汇总 | 工程 Markdown 是权威版本 |
| `E:\电赛相关资料\天猛星` | MSPM0G3507 天猛星板资料 | 查原理图、引脚和板级限制 |
| `E:\电赛相关资料\markdown` | 用户准备的 Markdown 资料 | 按任务选择读取，不全量加载 |
| `E:\电赛开源` | 已下载的开源控制类工程 | 学习架构，不直接替换 ECHO |
| `E:\STM32project hal\SMART CAR (10)` | 用户旧智能车工程 | 参考业务/算法经验 |
| `E:\STM32project hal\firsttest` | 用户旧测试工程 | 参考实验历史 |
| `D:\sftoware\TI_CCS\mspm0_sdk_2_10_00_04\examples\nortos\LP_MSPM0G3507\driverlib\empty` | TI 官方 empty 示例 | 不是可单独搬走的完整工程 |

已整理的总体架构 Word：

```text
E:\电赛相关资料\MSPM0G3507 电赛控制类 FreeRTOS 通用工程架构 v0.1_美化版.docx
```

工程持续更新以 `E:\ECHO\*.md` 为准；大阶段结束后再从 Markdown 汇总新版 Word，避免
Word 和代码状态双向修改造成冲突。

## 3. 外部参考

- 江南大学 2025 电赛开源页：
  `https://oshwhub.com/auhdbwiuda/jiangnan-university-25-electric-`
- 控制题备赛视频：
  `https://www.bilibili.com/video/BV1sBM466EF6`
- ECHO GitHub：
  `https://github.com/Aurora-520/ECHO.git`

外部页面可能更新或失效；关键结论需要在 worklog 中记录来源日期和本地证据。

## 4. 当前硬件清单

### 主控与供电

- 两块立创天猛星 MSPM0G3507。
- 调试使用 DAPLink CMSIS-DAP v2，SWDIO=PA19、SWCLK=PA20、nRESET。
- 整车电源计划为 4S 电池。
- 其中一块板仅接 5 V 就异常发热，即使程序能运行也禁止继续使用；正常板作为开发板。

### 底盘

- 两轮差速。
- 当前优先：GMR 编码器 370 电机；520 电机保留为候选，不得在接口中写死机械参数。
- 万能板上两路 AT8236 电机驱动。
- 红外灰度模块尚未记录确切型号和引脚。

进入 Phase 2A 前必须补齐：电机额定电压/电流、编码器每转计数定义、减速比、轮径、
轮距、AT8236 引脚/极性/制动真值表、电流和温升边界。

### IMU

- 两颗 ICM42688：底盘一颗、云台一颗。
- 第一版先用 I2C，接口保留以后改 SPI 的可能。
- 云台 IMU 安装位置和轴向尚未确定。

进入控制前必须记录每颗 IMU 的坐标轴、安装方向、采样率、带宽、时间戳和静态偏置。

### 云台

执行器后端按当前优先级：

1. 串口无刷电机，作为云台主执行器；具体型号、协议和接线尚未提供。
2. 两台张大头闭环步进电机，均使用 Emm 固件和 TTL 串口直连，仅作为备用执行器：
   第一代使用 UART2 PB15/PB16，第二代使用 UART3 PB2/PB3。
3. STEP/DIR 只保留为更低优先级的应急方案，本次未实现。

上层只能使用统一的角度/速度/使能/急停语义，禁止 Mission 直接拼 UART 字节。云台闭环
可用频率必须通过命令响应、IMU 采样、机械共振和任务抖动实测决定，不能只凭串口波特率判断。
无刷后端与步进备用后端必须经过唯一执行器仲裁，禁止同时获得输出权限。

### 感知与人机界面

- 树莓派 4B 优先负责高层视觉，向 MCU 发送经过校验的目标/状态指令。
- 单目相机和 MaxiCam 已列入设备，但通信协议和分工尚未冻结。
- SSD1306 128x64：I2C0，PA0 SDA、PA1 SCL，3.3 V，实测 0x3C、400 kHz。
- 计划五键输入；当前没有物理按键，仅调试器虚拟键。若使用单 ADC，必须先设计并实测
  电阻梯形网络、容差、去抖和各键电压窗口。

## 5. 已核实工具链

| 工具 | 路径/版本 | 作用 |
| --- | --- | --- |
| Keil MDK | `D:\keil mdk\UV4\UV4.exe`，5.39 | AC6 构建、DAPLink 调试备用/主界面 |
| ArmClang | 6.21 | 固件编译器 |
| TI MSPM0 SDK | `D:\sftoware\TI_CCS\mspm0_sdk_2_10_00_04` | DriverLib、FreeRTOS 与示例 |
| TI SysConfig | `C:\ti\sysconfig_1.26.0` | 外设与引脚生成 |
| OpenOCD | `D:\sftoware\openOCD\xpack-openocd-0.12.0-7-win32-x64\...` | VSCode DAPLink GDB server |
| GNU Arm | `D:\sftoware\cube CLT\STM32CubeCLT_1.18.0\GNU-tools-for-STM32\bin` | GDB、nm、objdump |
| 串口助手 | `E:\软件\UartAssist.exe` | 手工串口观察 |

GNU Arm 工具来自 STM32CubeCLT，但 GDB 是通用 Arm 调试器；固件仍由 Keil ArmClang 编译。
具体本机路径集中在 `config/local_paths.ps1`，不应散落到源码或提交 Git。

## 6. 已验证通信与调试事实

- DAPLink 调试链：VSCode Cortex-Debug -> GNU Arm GDB -> OpenOCD -> CMSIS-DAP -> MSPM0。
- SWD 已验证目标识别、下载、校验、复位、源码断点、Watch、暂停和继续。
- DAPLink 探针序列号：`4CDD7B98801CB180A5B29C09725A99B3`。
- UART1 当前：PA8 TX、PA9 RX、8N1、230400 baud，无硬件流控。
- COM 号会随 USB 枚举变化；工具必须接受参数或自动发现，不永久写死 COM4/COM8。
- 同一个 DAPLink 不能同时被 Keil、OpenOCD 或另一个 GDB 会话占用；同一个 COM 口也不能
  同时被网页遥测、采集脚本和串口助手独占。

## 7. 路径和环境边界

`ECHO` 可以整体移动，因为工程内部尽量使用相对路径，并通过脚本同步本机工具链路径。
它不意味着 `empty`、`keil`、`app` 或 `.tools` 中任意子目录可以独立搬走。

换电脑/路径后按顺序执行：

```text
修改 config/local_paths.ps1
-> ECHO: Check Environment
-> ECHO: Sync Local Paths
-> ECHO: Rebuild FreeRTOS + App
-> 再做烧录/调试验收
```

Windows 受限进程错误 `SetTokenInformation(TokenDefaultDacl) failed: 1344` 发生在命令启动前，
不是 FPGA、MSPM0、Keil 或代码错误。发生时记录失败层级，再由具备权限的终端重试必要命令。
