# ECHO 工程协作规则

本文件是 `E:\ECHO` 中未来 Codex 任务的常驻入口。它约束工程修改、硬件操作、
验收、Git 和文档维护。用户在当前任务中的最新明确指令优先于本文件；发生冲突时，
先说明冲突和风险，不得静默扩大权限。

## 1. 唯一正式工程

- 唯一正式工程和长期文档来源：`E:\ECHO`
- `C:\Users\Auror\ECHO-phase1e-work`：只作 Phase 1E 历史参考，不是正式工程
- 外部资料、旧工程和 SDK 路径见 `docs/HARDWARE_TOOLCHAIN_SOURCES.md`
- 不得因沙箱、令牌或路径问题另建一个“新的正式工程”代替 `E:\ECHO`

## 2. 每个新任务的读取顺序

开始修改前只读取完成当前任务所需的最小集合：

1. `AGENTS.md`
2. `docs/PROJECT_STATUS.md`
3. 当前 Phase 文档，例如 `docs/phases/PHASE1F_OPERABILITY_DIAGNOSTICS.md`
4. 当前任务相关的架构、硬件资料和源码

只有追溯设计原因时才读取旧 `docs/worklogs`。不要在每个任务中加载全部历史日志、
全部学习资料或完整聊天记录。

## 3. 开工前必须确认

- 执行 `git status --short --branch`，记录分支、HEAD、暂存区和用户改动。
- 阅读 `docs/PROJECT_STATUS.md` 中的受保护路径、硬件状态和下一步。
- 明确本次范围、非目标和验收标准。
- 修改已 dirty 的文件前，先读 diff 并保留用户已有语义。
- 不把“命令进程未能启动”误判为编译或固件失败。Windows 错误
  `SetTokenInformation(TokenDefaultDacl) failed: 1344` 属于受限进程令牌问题。

## 4. 架构边界

依赖方向固定为：

```text
App / Mission -> Module / Service -> BSP -> TI DriverLib / SysConfig
                           |
                           +-> platform/freertos
```

- `app/tasks`：任务入口、周期调度和跨模块编排。
- `app/missions`：赛题状态机，只组合能力，不实现设备驱动。
- `app/ui`：页面、按键事件和参数编辑，不直接控制执行器。
- `module/device`：设备协议，如 SSD1306、ICM42688、X42S。
- `module/control`：PID、运动学和控制器；输入数据，输出命令，不读 UART/OLED/按键。
- `module/estimation`：滤波、姿态和状态估计。
- `module/service`：通信、参数、诊断、快照和执行器仲裁。
- `bsp`：板级 GPIO、Timer、UART、I2C、PWM、DMA 的唯一手写 DriverLib 入口。
- `platform/generated`：SysConfig 生成物，只能通过 SysConfig/生成脚本更新。
- `platform/freertos`：RTOS hook、静态内存和故障诊断，不包含赛题逻辑。

详细红线和正确/错误示例见 `docs/ENGINEERING_RED_LINES.md`。核心规则：

- App 不直接调用 `DL_*`。
- BSP 不知道比赛模式。
- PID、滤波器和设备驱动不读取 UI 或串口命令。
- 同一执行器只能有一个运行时写入者；任务通过命令/快照交接。
- 诊断全局结构只供观察，不承担控制通信。
- App/Module 不得写死物理 PA/PB 引脚、Timer、DMA 或 IOMUX；物理映射只存在于
  SysConfig/板级资源映射/BSP。换到 MCU 支持的候选引脚时，上层接口不得修改。
- 引脚和外设采用编译时集中选择，禁止提供比赛运行中的任意 pin mux 切换；不支持的组合必须
  明确编译失败或保持模块 unavailable/输出锁定。

## 5. FreeRTOS 与实时性

- 优先使用原生 FreeRTOS API 和静态任务、静态队列、静态缓冲。
- 不为每个设备创建一个任务；按时序责任和数据所有权划分任务。
- 高频控制路径不得等待 OLED、串口物理发送、Flash 或无界外设轮询。
- ISR 只做清标志、搬运最小数据和必要通知；不得执行协议解析或阻塞操作。
- 周期任务使用 `vTaskDelayUntil`/`xTaskDelayUntil`，实际时序用 1 MHz 时基测量。
- 新任务必须纳入栈高水位、运行次数、超期和故障诊断。
- 调试断点会暂停整个 MCU，不能用单步结果评价控制周期。

Phase 2B 的正式 ICM42688 和所有后续 IMU 必须遵守统一 READY 门禁，未来 AI 不得依赖用户提醒：

- 保留 `PROBE -> RESET_WAIT -> SETTLING -> CALIBRATING -> READY` 或语义等价的非阻塞状态机；
  禁止用固定 `vTaskDelay(4000)` 代替校准成功判断。
- MPU 备用验证和正式 ICM42688 共用 `ImuService` 快照与 `ImuService_IsReady()` 契约，只替换
  device/BSP 层驱动，不得复制一套绕过校准的控制入口。
- IMU 未 READY 时，依赖 IMU 的航向、姿态和位置闭环不得启用，执行器仲裁必须保持对应输出
  锁定或进入明确的非 IMU 降级模式。
- IMU offline、stale、采样失败或重新连接后，依赖 IMU 的闭环立即退出；重新连接必须重新完成
  稳定等待和静止校准，禁止自动恢复旧输出。
- Health、OLED 和后续树莓派 4B 状态协议必须公开 `CALIBRATING/READY/OFFLINE/STALE`，上层只能
  读取状态，不能通过命令强制伪造 READY。
- Phase 2B 验收必须包含启动移动导致校准重启、READY 前输出门禁、断开/恢复、连续运行和零漂
  统计；这些证据未通过时只能报告当前层级，不能报告 Phase 2B 完成。

## 6. 构建与硬件权限

允许未来任务自动执行：

- 环境检查、路径同步、Keil/AC6 构建
- DAPLink 烧录、复位和不连接电机的板级测试
- UART/OLED/IMU 等低功率测试，但应先确认接线和供电条件

普通应用固件烧录不等于授权全片擦除、Option/配置区改写或破坏性 Flash 故障注入。
这些操作必须先说明目标地址、恢复方法和风险，并取得用户明确许可。

必须在用户明确许可且人在现场时执行：

- 电机、云台、轮组或其他会运动的输出
- 高功率输出、4S 电池带载测试
- 可能造成机构碰撞、持续堵转或失控的试验

电机试验前必须架空轮子、默认输出为零、准备物理断电。异常发热的板卡禁止继续使用。
不得把 DAPLink 3V3 输出并到已供电的 3.3 V 电源轨。

## 7. Git 安全

- 禁止自动 `push`；必须由用户明确要求。
- 禁止自动删除 stash、分支、标签、worktree 或仓库外备份。
- 禁止 `git reset --hard`、`git checkout -- <file>` 等破坏用户改动的操作。
- dirty 工作树中禁止 `git add .`、`git add -A` 和 `git commit -am`。
- 只显式暂存本任务文件，并检查 `git diff --cached --name-only`。
- 提交前执行 `git diff --cached --check`，确认暂存区不含用户文件。
- 只有阶段验收全部通过后，才创建阶段提交和 annotated tag。
- 正式工程导入阶段提交时优先只允许 fast-forward；结构化文件必须语义合并。

截至本规则建立时，下列 4 个文件含用户改动，禁止自动暂存、覆盖或还原：

```text
ECHO.uvmpw
freertos/keil/freertos_ECHO.uvprojx
keil/ECHO.uvprojx
tools/telemetry-web/README.md
```

实际状态以每次开工时的 `git status` 和 `docs/PROJECT_STATUS.md` 为准。

## 8. 验收与证据

- 验收强度与风险匹配：纯文档检查链接与 diff；固件至少全量构建；硬件功能必须板测。
- 报告中区分“代码审查通过”“编译通过”“烧录通过”“板上实测通过”。
- 不得用某个层级的通过代替另一个层级。
- 实时功能最终验收必须无断点连续运行，记录时长、频率、错误、丢帧、超期和栈余量。
- 没有保存的原始数字不得事后编造；只写可由提交、日志或诊断快照证明的事实。
- 外设并发问题使用 A/B 对照，每次只改变一个主要变量。

## 9. 文档维护

有实质修改的工作会话结束时：

1. 更新 `docs/PROJECT_STATUS.md`。
2. 在 `docs/worklogs` 新建一份结构化摘要。
3. 每完成一个可独立验收的小模块，必须按
   `docs/learning/MODULE_DEBUG_RECORD_TEMPLATE.md` 更新
   `docs/learning/DEBUGGING_PLAYBOOK.md`；没有完成该记录不得报告小模块完成。
4. 同时更新 `docs/learning/INTEGRATION_PLAYBOOK.md` 中的资源占用、引脚迁移、初始化、
   降级行为和组合回归；没有集成合同只能报告单模块证据，不能写 `integration_ready`。
5. 如涉及新原理或排障方法，更新 `docs/learning` 中对应条目和索引。
6. 阶段完成时更新当前 Phase 文档和 README 索引。

工作日志记录目标、开始提交/标签、修改文件、关键决定、硬件状态、测试结果、
未解决风险、下一步和最终提交/标签。原始串口流、大型日志、临时截图和完整聊天记录
不进入 Git；只保存摘要和必要的小型证据。

“小模块”包括 BSP 外设入口、device 驱动、service、estimator/controller、任务、协议帧、
主机工具、OLED 页面、单侧电机或编码器等可独立构建/板测的单元。模块调试记录必须区分
代码审查、构建、烧录、板测、故障测试和连续运行；核心适用项仍为 `not run/failed` 时，
只能报告当前证据层级，不能写 `completed`。

单模块通过不等于组合通过。新增模块必须登记 Timer、DMA、IRQ、任务、内存、协议/故障 ID 和
共享总线预算，至少完成一个共享资源邻居的两两回归，并重新执行当前阶段与上一阶段基线门禁。

## 10. 语言规范

- 与用户沟通、阶段报告、验收结论和工程文档默认使用中文。
- 代码标识符、API、协议字段、命令、路径以及必须保持原样的工具输出可保留英文。
- 引用英文资料时应优先给出中文结论；不得因翻译改动原始技术含义或证据数据。
