# ECHO 队友接手与调试手册

适用对象：只有基础 C 语言、GPIO、串口和定时器知识，希望在自己的 Windows 电脑上借助
Codex 接手 ECHO 构建、烧录、低功率调试和问题排查的队友。

本文件说明“怎么做”。当前做到哪一步、哪块硬件已接、哪些文件正在修改，必须查看：

```text
docs/handoff/CURRENT_HANDOFF.md
docs/PROJECT_STATUS.md
docs/phases/当前 Phase 文档
```

不要只凭本手册判断某个电机、引脚或功能已经可以上电。

## 1. 先记住五条规则

1. 正式工程由负责人维护；队友电脑上的仓库是开发副本，不能自行宣布为正式基线。
2. 每次先检查 Git 和实时交接，再修改代码。不要覆盖不认识的 dirty 文件。
3. 先构建，后烧录；先低功率单模块，后组合测试；没有证据不能写“通过”。
4. 电机、云台、轮组、4S 或高功率输出必须由负责人明确许可且有人在现场。
5. 不自动 push，不全量暂存，不手改 SysConfig 生成文件。

## 2. 在自己电脑上拿到正确工程

当前远端不一定包含最新阶段。不能只执行 `git clone` 后默认 `origin/main` 就是最新版。
负责人必须通过以下任一方式提供当前阶段：

- 已推送的明确 branch/commit；
- 包含当前 branch/commit 的 Git bundle；
- 负责人确认过的完整 Git 工作树传输。

拿到工程后，记录自己的开发路径，例如：

```text
D:\work\ECHO-phase2a-work
```

不要把自己的路径写成新的“唯一正式工程”。负责人机器上的正式工程仍以
`CURRENT_HANDOFF.md` 记录为准。

第一次打开后执行：

```powershell
git status --short --branch
git rev-parse HEAD
git worktree list
git diff
git diff --cached
```

确认 branch、HEAD 和负责人交给你的目标一致。存在不认识的修改时先停下询问。

## 3. 让 Codex 正确接手

在 Codex 中打开当前阶段 worktree，不要打开旧工程或正式 main。第一条消息可以直接使用：

```text
你负责接手 ECHO 当前阶段。工作区是 <我的绝对路径>。
先完整读取 AGENTS.md、docs/handoff/TEAMMATE_HANDOFF.md、
docs/handoff/CURRENT_HANDOFF.md、docs/PROJECT_STATUS.md 和当前 Phase 文档。
然后执行 git status --short --branch、git diff、git diff --cached、git worktree list。
不要自动 push，不要删除 stash/分支/worktree，不要手改 platform/generated。
涉及电机、云台、4S 或运动输出前必须询问我并确认我在场。
先告诉我真实状态、当前风险和下一步，不要编造构建或板测结果。
```

Codex 给出结论后，队友要检查它是否明确区分：

```text
代码审查通过
编译通过
烧录及校验通过
板上实测通过
连续运行通过
```

这五项不是一回事。断点看到变量变化不能代替无断点连续运行证据。

## 4. 工程架构怎么看

依赖方向固定：

```text
App / Mission
      |
      v
Module / Service
      |
      v
BSP
      |
      v
TI DriverLib / SysConfig
```

| 目录 | 用途 | 队友通常做什么 |
| --- | --- | --- |
| `app/tasks` | FreeRTOS 任务入口和周期调度 | 看任务何时调用模块，不直接写寄存器 |
| `app/missions` | 比赛任务状态机 | Phase 3 前通常不动 |
| `app/ui` | OLED 页面和按键事件 | 修改显示逻辑，不直接控制电机 |
| `module/device` | OLED、IMU、云台等设备协议 | 调协议、解析和设备状态 |
| `module/control` | PID、运动学和控制算法 | 只处理数据，不读串口/OLED |
| `module/service` | 通信、参数、健康、快照、执行器仲裁 | 处理跨模块所有权和安全状态 |
| `bsp/include`、`bsp/source` | GPIO、Timer、UART、I2C、PWM 板级接口 | 所有手写 `DL_*` 调用放这里 |
| `platform/freertos` | RTOS hook、静态内存和故障诊断 | 不放比赛任务逻辑 |
| `platform/generated` | SysConfig 自动生成文件 | 只读，禁止手工修改 |
| `tests` | 采集脚本测试和板测证据 | 保存可复现的测试结果 |

判断代码放哪一层的简单方法：

- 出现 `DL_GPIO_*`、`DL_Timer*`：应在 BSP。
- 出现 PID/速度/位置计算：应在 `module/control`。
- 出现 OLED 文本或按键：应在 `app/ui`。
- 出现周期调度和任务入口：应在 `app/tasks`。
- 出现唯一写入者、命令超时、健康快照：应在 `module/service`。

## 5. 新电脑工具链

当前工程使用的主要工具版本和来源见 `docs/HARDWARE_TOOLCHAIN_SOURCES.md`。通常需要：

- Git for Windows；
- VSCode；
- VSCode Cortex-Debug 扩展；
- Keil MDK / ArmClang 6.21；
- TI MSPM0 SDK 2.10.00.04；
- TI SysConfig 1.26.0；
- xPack OpenOCD 0.12.0-7；
- GNU Arm GDB/objcopy/objdump，可使用工程已验证的 STM32CubeCLT 工具链；
- DAPLink/CMSIS-DAP 探针。

复制本机配置模板：

```powershell
Copy-Item .\config\local_paths.example.ps1 .\config\local_paths.ps1
```

编辑 `config/local_paths.ps1`，把六个路径改成自己电脑上的实际位置。该文件是本机配置，
不进入 Git。

然后执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\check_environment.ps1 -Scope All

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\sync_local_paths.ps1
```

`sync_local_paths.ps1` 会更新 Keil/VSCode 中的本机绝对路径。运行后不要把纯路径变化误当成功能
修改提交；先用 `git diff` 检查。

## 6. 标准构建流程

推荐先运行完整环境检查，再做全量构建：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\check_environment.ps1 -Scope All

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\generate_syscfg.ps1

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\build_echo.ps1 -Mode All
```

验收要求是两套工程均显示：

```text
0 Error(s), 0 Warning(s)
```

SysConfig 的 STOP/STANDBY retention 提示是已知提示，但任何 `error` 都必须先解决。不要手改
`platform/generated/ti_msp_dl_config.c/.h` 消除生成问题，应修改 `config/ECHO.syscfg` 后重新生成。

VSCode 也提供同名任务：

```text
ECHO: Check Environment
ECHO: Generate SysConfig
ECHO: Rebuild FreeRTOS + App
ECHO: Build + Flash (DAPLink)
ECHO: Debug (DAPLink + OpenOCD)
```

## 7. DAPLink 接线

### 7.1 SWD 调试

```text
DAPLink                 天猛星 MSPM0G3507
SWDIO   --------------> PA19
SWCLK   --------------> PA20
nRESET  --------------> nRESET
GND     --------------> GND
```

重要：不要把 DAPLink 的 `3V3` 接到已经由其他电源供电的 3.3 V 电源轨。除非确认探针该引脚
只是电压参考且接法正确，否则只接 SWDIO、SWCLK、nRESET、GND。

### 7.2 UART 遥测

```text
USB-TTL / DAPLink UART       天猛星 MSPM0G3507
RXD       <---------------- PA8 / UART1_TX
TXD       ----------------> PA9 / UART1_RX
GND       ----------------- GND
VCC       不接（主控已有供电时）
```

串口参数：`230400 baud, 8N1, no flow control`。COM 号会变化，不能永久写死为 COM4。

### 7.3 当前低功率外设速查

| 功能 | 模块端 | MCU 端 |
| --- | --- | --- |
| OLED SDA | SSD1306 SDA | PA0 |
| OLED SCL | SSD1306 SCL | PA1 |
| 左编码器 A/B | D153B E1A/E1B | PA29/PA30，硬件 QEI x4 |
| 右编码器 A/B | D153B E2A/E2B | PB6/PB7，软件 x1 |

完整且持续更新的接线状态只看 `docs/hardware/ECHO_WIRING_GUIDE.md`。电机 PWM、VM、4S 和
高功率接线只有在实时交接明确允许、负责人在场确认后才能使用。

## 8. 烧录和 DAPLink 调试

烧录前确认：

- 当前是正确 worktree 和 branch；
- 构建为 0 Error / 0 Warning；
- DAPLink 只被一个程序占用；
- 低功率接线和供电正确；
- 若固件含运动输出，VM/4S 必须保持断开，直到单独完成安全门禁。

清理残留调试进程：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\prepare_debug.ps1
```

烧录并校验：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\flash_echo.ps1
```

OpenOCD 的目标端 CRC 有时无法 halt。脚本会自动退回逐字节 Flash 回读和 SHA-256 比较；只有
哈希一致并成功 `reset run` 才能写“烧录及校验通过”。

VSCode 调试步骤：

1. 运行 `ECHO: Debug (DAPLink + OpenOCD)`。
2. 程序停在 `main` 后查看 Call Stack、Variables、Registers 和 Watch。
3. 需要继续运行时按 Continue，不要长期停在断点上评价任务周期。
4. 结束调试后运行 `ECHO: Stop Stale Debug Servers`，避免 OpenOCD 占用探针。

常用 Watch：

```text
g_system_health_snapshot
g_system_health_diag
g_rtos_diag
g_telemetry_diag
g_serial_tx_diag
g_serial_rx_diag
g_parameter_service_diag
g_display_task_diag
g_ssd1306_diag
g_bsp_i2c_diag
g_bsp_reset_diag
g_bsp_encoder_diag
```

调试器可能暂停整个 MCU，甚至在 OpenOCD target 状态异常时触发调试复位。断点和 RAM 读取
只用于定位问题；实时频率、drop、deadline 和连续运行必须用 UART 无断点采集。

## 9. UART 采集

先关闭串口助手和网页工具，保证 COM 口只被一个程序占用：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\telemetry_capture.ps1 `
  -Port COM4 -DurationSeconds 10
```

正常结果应包含 Control 约 100 Hz、Health 约 1 Hz，并检查：

```text
CrcErrors = 0
SequenceGaps = 0
DeadlineMissCount = 0
LatestHealth.ActiveIssueMask = 0
```

具体 Phase 还要检查对应的 I2C、OLED、QEI、ISR late、drop 和连续运行门槛。输出 CSV/JSON 时
优先写到 `tests/artifacts`；大型原始数据通常被 Git ignore，只把真实摘要写入 worklog。

## 10. 常见故障

| 现象 | 先检查什么 |
| --- | --- |
| PowerShell 禁止运行脚本 | 命令是否带 `-ExecutionPolicy Bypass` |
| `SetTokenInformation...1344` | 这是受限进程令牌问题，不等于编译失败；在正常终端重试 |
| DAPLink 找不到 | SWD 接线、USB 线、探针固件、是否被 Keil/OpenOCD 占用 |
| OpenOCD 端口 3333 被占用 | 运行 `tools/prepare_debug.ps1` |
| 烧录 verify CRC 超时 | 看脚本是否完成回读 SHA-256，不能只看前面的 CRC 报错 |
| 串口没有数据 | TX/RX 是否交叉、共地、230400、COM 号和端口独占 |
| CRC/gap 突然增加 | 线长、共地、主机读取阻塞、多个程序抢占 COM 口 |
| 左 QEI 未接时出现抖动 | PA29/PA30 浮空；未接通道不能当有效 Health 结论 |
| 调试后 uptime/计数归零 | OpenOCD 可能触发调试复位，重新记录测试起点 |

## 11. 绝对不要动的内容

- 不在当前阶段验收完成前直接修改正式 `E:\ECHO` main。
- 不手工编辑 `platform/generated`；只改 `.syscfg` 并重新生成。
- 不自动暂存或覆盖负责人保留的用户文件：

```text
ECHO.uvmpw
freertos/keil/freertos_ECHO.uvprojx
keil/ECHO.uvprojx
tools/telemetry-web/README.md
```

- 不提交 `config/local_paths.ps1` 或纯本机路径噪声。
- 不使用 `git add .`、`git add -A`、`git commit -am`。
- 不自动 push，不删除 stash、备份、branch、tag 或 worktree。
- 不使用 `git reset --hard`、`git checkout -- <file>` 覆盖未知修改。
- App 不直接调用 `DL_*`，BSP 不写比赛任务，UI 不直接驱动执行器。
- 不为每个设备随意增加任务、事件总线或动态注册系统。
- 不在高频任务中等待 OLED、UART、Flash 或无超时外设操作。
- 不在负责人不在场时连接或驱动电机、云台、轮组、VM、4S 或其他高功率输出。

## 12. 修改完成后如何交回

结束前执行：

```powershell
git status --short --branch
git diff --check
git diff
git diff --cached
```

向负责人报告：

- 修改了哪些文件和原因；
- 构建、烧录、板测分别是否执行；
- 保存的 CSV/JSON/日志位置；
- 当前接线、供电和 MCU 运行状态；
- 失败、风险、deferred 和下一步；
- 是否存在未提交或本机路径修改。

有实质修改时更新 `docs/handoff/CURRENT_HANDOFF.md` 和对应 worklog。没有负责人明确要求时，
不要自行 commit/tag/push。阶段最终提交、标签和正式合入仍由阶段负责人按验收流程完成。
