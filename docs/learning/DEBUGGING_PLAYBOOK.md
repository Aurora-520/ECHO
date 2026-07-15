# ECHO 模块调试手册

状态：living document
适用范围：ECHO 从基础平台、底盘、IMU、云台、视觉、任务状态机到整车验收的全部小模块。

本手册保存能够跨会话、跨阶段复用的调试结论。它不替代当前 Phase 文档、实时交接、Git diff、
原始测试数据或一次性 worklog。任何小模块只有完成本文件要求的记录和证据闭环后，才能报告
“该小模块完成”；阶段完成仍必须满足对应 Phase 的全部门禁。

## 1. 强制规则

以下任一对象都按一个“小模块”管理：

- 一个 BSP 外设入口，例如 GPIO、Timer、UART、I2C、PWM、DMA、QEI；
- 一个 device 驱动，例如 SSD1306、MPU6050/MPU6500-compatible、ICM42688、X42S；
- 一个 service，例如参数、健康、遥测、执行器仲裁、Motor Profile；
- 一个 estimator/controller，例如滤波、姿态、速度环、航向环、位置环；
- 一个任务、协议帧、OLED 页面、主机工具、单侧电机或单侧编码器；
- 一个可独立接线、构建、烧录、板测和故障注入的硬件通道。

每个小模块结束时必须同时完成：

1. 在 `docs/worklogs` 写当次真实过程和证据。
2. 在本手册新增或更新该模块条目，提炼可复用的现象、原因、排查和预防方法。
3. 更新 `INTEGRATION_PLAYBOOK.md` 中的引脚迁移、资源占用、初始化、降级和组合回归合同。
4. 更新 `docs/PROJECT_STATUS.md` 或当前实时交接中的状态、阻塞和下一步。
5. 检查文档中的 build、引脚、频率、型号、版本、commit/tag 和硬件条件与实际一致。

禁止只写“已调通”“数据正常”“应该没问题”。至少要写清：

```text
模块边界 -> 硬件/软件版本 -> 接线和供电 -> 唯一 owner
-> 复现步骤 -> 诊断字段 -> A/B 对照 -> 根因/当前判断
-> 修复 -> 构建 -> 烧录 -> 板测 -> 故障测试 -> 连续运行
-> 未执行项 -> 恢复方法 -> 后续禁止事项
```

没有执行的检查必须写 `not run`；缺少硬件条件但允许后续阶段继续时写 `deferred`。任何与本模块
核心行为直接相关的构建、板测、故障恢复或连续运行仍为 `not run/failed` 时，不得报告该模块完成。
单模块完成后仍未登记资源或通过适用组合回归时，不得写 `integration_ready`。

## 2. 证据等级与结论用语

| 等级 | 可以证明 | 不能证明 |
| --- | --- | --- |
| 代码审查 | 所有权、边界、超时、静态结构合理 | 编译器接受、硬件可运行 |
| 主机 fixture | 协议编码、CRC、解析和负例逻辑 | MCU 时序、真实 UART/I2C 电气行为 |
| 0/0 全量构建 | FreeRTOS/App 在当前配置可链接 | 固件已烧录、引脚有正确波形 |
| program/verify/reset | 指定镜像进入目标且读回一致 | 外设接线正确、功能正常 |
| 单模块板测 | 当前接线和条件下核心行为成立 | 组合并发、故障恢复、长时间稳定 |
| 故障测试 | 断开、坏帧、超时或非法输入能恢复/锁定 | 长时间无故障运行 |
| 连续运行 | 规定时间内频率、错误、资源稳定 | 未覆盖的温度、电流、机械极限 |
| 阶段验收 | 当前 Phase 全部门禁闭环 | 后续阶段或最终整车完成 |

报告时使用精确表述，例如“左编码器无动力手转板测通过”“MPU 静止性能观察正常”。只有完成
全部阶段后才能说“Phase N 完成”，只有 Phase 1F 至 Phase 4 全部验收后才能说“最终工程完成”。

## 3. 通用排障流程

### 3.1 先冻结现场

记录固件 commit/HEX、build ID、上电方式、线束、模块丝印、地址、逻辑电平、VM/4S、电机动力线、
调试器是否连接、是否使用断点。涉及运动输出时先零输出、架空机构并准备物理断电。

### 3.2 先分层再猜原因

```text
传感器/执行器硬件
-> BSP 事务和引脚
-> device 协议与换算
-> service 快照/状态机
-> task 周期和所有权
-> telemetry 编码与 UART 传输
-> 主机采集器解析
```

上层 CRC 错误不等于传感器采样错误；I2C ACK 不等于量程、身份和轴向正确；遥测显示零也不等于
物理 PWM 引脚为低。每层必须使用本层证据判断。

### 3.3 一次只改变一个主要变量

典型 A/B 项：OLED refresh 开/关、IMU 读取开/关、调试器连接/断开、单编码器/双编码器、原始符号/
归一化符号、x1/x4、VM 断电/逻辑供电。改变两个以上主要变量时，结果只能作为线索，不能冻结根因。

### 3.4 保留恢复路径

任何实验前写清恢复动作：`reset run`、重新烧录已知正常 HEX、断电重接、恢复编译宏、恢复默认参数。
破坏性 Flash、Option 区、全片擦除和高功率测试必须另行授权。

## 4. 历史模块经验索引

| ID | 小模块 | 状态 | 主要结论 |
| --- | --- | --- | --- |
| P1A-01 | 可移动构建与工具链 | passed | 用无旧产物副本和 HEX 哈希判断可移动性 |
| P1A-02 | DAPLink/OpenOCD/GDB | passed | 快速 CRC 超时不等于烧录失败，逐字节回读可兜底 |
| P1B-01 | 静态 FreeRTOS 骨架 | passed | 静态任务/队列、统一 fault、栈/heap 诊断 |
| P1C-01 | 80 MHz 与 1 MHz 时基 | passed | 断点冻结时基，周期用无符号模减 |
| P1D-01 | UART DMA 遥测与 RAM 参数 | passed，历史原始数字未留存 | 物理发送必须与高频控制解耦 |
| P1E-01 | SSD1306/I2C | passed | 所有等待有界，断开不得拖停 RTOS |
| P1E-02 | OLED/UART quiet window | passed | I2C 活动与 UART 损坏需 A/B；quiet 必须成对和有最大时长 |
| P1F-01 | 统一 SystemHealthSnapshot | passed | ServiceTask 唯一写入者，active/sticky/first 分离 |
| P1F-02 | 参数元数据与周期应用 | passed | wire ID 先按 uint16 查表，UART/OLED 共用规则 |
| P1F-03 | 五页 UI 与现场检查 | passed，三个硬件门 deferred | Control/Health 分别统计，连续运行不留断点 |
| P2A-01 | 左轮硬件 QEI x4 | passed（无动力） | PA29/PA30，向前为正，CPR 仍 provisional |
| P2A-02 | 右轮软件 x1 | passed（无动力） | PB6 上升沿/PB7 方向，原始向前为负，配置 sign=-1 |
| P2A-03 | 双编码器并发 | passed（无动力） | 左 x4/右 x1 必须按轮配置，未接输入会浮空 |
| P2A-04 | AT8236 逻辑 PWM 安全链 | passed（VM 断电） | SWD RAM 写会重启，执行器命令改用 UART 一次性安全帧 |
| P2A-05 | MG370/513X Motor Profile | build passed | 单一编译宏；未知关键参数必须锁定输出或编译失败 |
| IMU-S01 | MPU6050/MPU6500-compatible spike | in progress | WHO_AM_I=0x70；静止数据正常，动态/恢复/长测未闭环 |

## 5. 已补录模块记录

### P1A-01 可移动构建与工具链

**目标与边界**：证明工程不依赖 `E:\ECHO` 的偶然绝对路径；不改变 32 MHz 固件行为。

**遇到的问题**：AXF 和静态库包含绝对调试路径或归档元数据，复制工程后哈希可能变化。如果直接
比较 AXF，会把无行为变化误判成构建不可复现。项目内 pyOCD 虚拟环境还绑定旧绝对路径。

**方法与结论**：测试副本排除旧 `Objects/Listings`，重新同步路径、运行 SysConfig、全量构建；
路径同步连续执行两次必须幂等。以最终 Flash 镜像 `ECHO.hex` 的 SHA-256 相同作为行为不变证据。

**已验证证据**：原路径和移动副本 FreeRTOS/App 均为 0 Error / 0 Warning，HEX SHA-256 均为
`C6913EBD742B7F285DF356FA8AE6D8B6C9857847AB6D169B0A39F4F559C9FD79`。

**以后必须注意**：不能携带旧对象做移动验收；不能因为 AXF 哈希不同就认定固件不同；
`platform/generated` 只能由 SysConfig 生成。

### P1A-02 DAPLink、OpenOCD 与 GDB

**现象**：OpenOCD 的目标端快速 CRC 可能超时；GNU GDB 读取 ArmClang AXF 会提示
`RW_IRAM2 outside of ELF segments`；启动新的调试连接还可能复位目标。

**判断方法**：区分“命令未启动”“快速校验失败”“逐字节读回不一致”“程序行为失败”。快速 CRC
超时后使用逐字节 Flash readback 和 SHA-256；烧录完成后显式 `reset run`。需要同一次启动的 RAM
终点证据时，应保持调试连接，不能运行结束后重新 attach 再假定没有复位。

**结论**：DAPLink、Keil 下载、OpenOCD 烧录和 GDB 断点链均已通过。`RW_IRAM2` 提示不是烧录失败。

### P1B-01 静态 FreeRTOS 骨架

**架构**：SystemTask 100 Hz；ServiceTask 接收 1 Hz 心跳；任务、队列、Idle/Timer 内存静态分配；
PB22 DriverLib 只在 BSP。诊断结构只供观察，不承担任务间通信。

**关键方法**：栈同时记录分配量、历史最小未用量和历史最大使用量；多字段快照使用奇偶 update
sequence；configASSERT、malloc、stack overflow、心跳超时等统一写 sticky fault 后停机。

**故障经验**：无调试器时执行 BKPT 会在 Cortex-M0+ 产生额外 HardFault，因此 fatal 路径不能把
BKPT 当成通用停机方式。动态 heap 保留只为 TI DPL 兼容和诊断，不应用来创建长期任务对象。

**已验证证据**：0/0 构建；连续 794 秒，deadline、队列错误和 fault 均为 0，heap minimum
`3064 B`。

### P1C-01 80 MHz 与 1 MHz 时基

**配置**：SYSOSC 32 MHz 经 SYSPLL 得到 MCLK 80 MHz；FreeRTOS Tick 1 kHz；TIMG12 由 MFCLK
4 MHz 除 4 得到 1 MHz、32 位向上计数。

**关键方法**：耗时必须使用 `(uint32_t)(now - start)`，允许跨 32 位回绕；周期诊断区分 period、
jitter、release lateness、execution 和 overrun，不能只看一个“周期值”。TIMG12 与 CPU 在 debug
halt 时同时冻结，避免断点制造虚假迟到。

**已验证证据**：100 秒 period `9999-10000 us`、max jitter `1 us`、deadline `0`；连续 704 秒
仍为 deadline/fault `0`。

**边界**：没有实际运行到约 71.6 分钟回绕点；内部 SYSOSC 适合控制时序，不是精密计量时钟。

### P1D-01 UART DMA 遥测与 RAM 参数

**已确认结论**：高频任务只把完整 frame 原子写入静态 ring，不等待 UART 物理发送；DMA、drop、
stall、restart 和 ring high-water 必须可诊断。参数接收与控制周期应用分开。

**证据限制**：Phase 1D 的仓库没有保留原始串口日志和具体验收数字，历史文档只能写“阶段已验收”，
不得根据后续 Phase 数字倒推或补造 Phase 1D 结果。

### P1E-01 SSD1306 与有界 I2C

**接线**：OLED `PA0=SDA`、`PA1=SCL`、3.3 V、共地，地址 `0x3C/0x3D`，I2C0 400 kHz。

**关键方法**：BSP 是唯一 DriverLib owner；SSD1306 只处理协议和 framebuffer；DisplayTask 负责
初始化、重试和刷新。所有 BUSY/ACK/STOP 等等待使用 1 MHz 时基和明确超时；模块断开只标记 offline
并周期重试，不得停住 FreeRTOS。

**硬件风险**：临时杜邦线、共地和并联上拉会影响边沿。模块能工作不代表上拉一定符合正式整车
要求；比赛线束应短线、可靠地线、核实上拉并增加近端去耦。

### P1E-02 OLED 与 UART quiet window

**原始现象**：OLED 周期刷新开启时出现 UART CRC 和 sequence gap，关闭刷新后链路恢复 100 Hz、
CRC/gap 为 0；控制 deadline 始终为 0。这证明问题位于外设并发/电气链，不是 SystemTask 超期。

**排查方法**：使用 RAM 开关仅改变 OLED refresh；比较同一波特率下启用/禁用；同时观察 Control
频率、CRC、gap、deadline、serial drop、I2C error。不能只看 OLED 是否亮。

**修复契约**：DisplayTask 在每次初始化/全屏刷新前申请 UART quiet window，等待有界；失败则错相
7 ms 重试，连续八次后跳过本轮。quiet 必须满足 acquire=release、active=0、最大持续时间受限。

**最终证据**：230400 baud 下 120 秒 Control `12192`、CRC/gap/deadline `0`；I2C success
`67058`、error `0`；quiet `419/419`，最大 `38529 us`；serial high-water `280 B`。

**以后必须注意**：新增共享 I2C 设备后要重新做 OLED+设备+UART 并发回归；不能无限扩大 ring 掩盖
损坏；断点暂停后不使用该次 I2C timeout 评价正常运行。

### P1F-01 统一 SystemHealthSnapshot

**架构**：各 owner 维护事实计数，ServiceTask 每 100 ms 是统一快照唯一写入者；OLED、1 Hz Health
和 Watch 读取相同语义。快照使用临界区复制和偶数 update sequence，不是事件总线。

**故障语义**：active 表示当前存在；sticky 表示发生过；first critical fault 保存首个关键原因。
可恢复项只能在故障已经消失后清 sticky，关键 first fault 不能被后续连锁故障覆盖。

**遇到的问题**：健康刷新最初在 ServiceTask 栈上同时创建两份大快照，最小余量降到 63 words。
将仅由 ServiceTask 使用的工作副本改为函数私有静态缓冲后，最终恢复到 128 words。不能通过降低
告警门槛掩盖栈峰值。

### P1F-02 参数元数据与周期边界应用

**问题**：ArmClang 可能用最小宽度表示 enum。wire ID `0x0101` 若先转成小 enum，高位可能丢失并
错误命中合法 ID `1`。

**正确方法**：保持 `uint16` wire ID 查 metadata；未命中立即拒绝；命中后才转换为内部 enum。
UART/OLED 共用 default、min、max、step、units、flags 和 staging API；SystemTask 是 applied 参数
唯一写入者，只在 100 Hz 周期边界原子应用。

**测试要求**：必须包含 NaN、Inf、范围外、未知 ID、超过 8 位但低字节合法、BUSY、duplicate 和
恢复默认事务。

### P1F-03 五页 UI、混合遥测与连续运行

**UI**：Overview、RTOS、COMM、DEVICE、PARAM 五页；虚拟输入验证 press/long/repeat/timeout；
物理 ADC 五键仍为 deferred。

**混合遥测方法**：global sequence 判断任何完整 frame 丢失；Control 与 Health 各自用 count/timestamp
计算 100 Hz/1 Hz。只筛 Control 后使用全局 sequence 会把 Health 插帧误判为 gap。

**连续运行方法**：故障注入结束后 `reset run`；断开调试器，无断点跑 120 秒和至少 10 分钟；让
Health 帧直接携带栈、heap、I2C、quiet 和 fault，不在结束后暂停补造证据。

**最终证据**：约 609.52 秒，Control `60953`、Health `610`；CRC/gap/drop/deadline/I2C error
全部 `0`；六任务最小栈 `180/128/151/102/104/104 words`；heap minimum `3064 B`。

**独立门禁**：Flash 持久化、物理 ADC 五键、硬件看门狗均为 deferred，不能描述为 passed。

### P2A-01 左轮硬件 QEI x4

**接线与实现**：D153B E1A/E1B -> PA29/PA30，TIMG8 QEI，合法 AB Gray 状态变化 x4；电机动力线
断开，VM/4S 断电，只手转左轮。

**方向结论**：真实机械前进方向原始计数为正，`encoder_count_sign=+1`。粗略一圈不能冻结 CPR；
当前输出轴 CPR `68028` 仅 provisional。

**证据**：前进约 `+77523/+76749`，后退约 `-74323`；静止 120 秒 `12196` 个 Control delta 全零，
CRC/gap/drop/deadline/QEI fault 全零。

**以后必须注意**：编码器 PPR 是电机轴还是输出轴、是否已经包含 x4 必须确认；方向修正只进入每轮
sign，不交换已冻结线束。

### P2A-02 右轮软件 x1

**实现原因**：右轮 E2A->PB6 上升沿中断、E2B->PB7 方向输入。满速若软件 x4 约产生 340 kISR/s，
因此否决软件 x4并冻结为 x1。

**方向结论**：真实机械前进方向原始净计数 `-18632`，后退 `+21691`，所以
`encoder_count_sign=-1`，归一化后整车向前仍为正。

**证据**：连续手转 10 ms 最大 `230 count`；SystemTask 最大 `24 us`；Control/Health 100/1 Hz，
CRC/gap/drop/deadline/ISR late 全零。

**遇到的问题**：左轮未接时 PA29/PA30 浮空，机械扰动触发 QEI issue 16。未接输入必须禁用、接入
确定上下拉或明确标记 unavailable，不能把浮空计数误判为另一通道串扰。

### P2A-03 双编码器并发与计数口径

**结果**：左右接好后静止 10 秒、60 秒均无计数；粗略一圈左 x4 `76573`、右 x1 `20072`，绝对比
`3.812:1`，接近理论 x4/x1，但粗略手转不能替代多圈标定。

**规则**：左右轮分别保存 `encoder_count_sign`、decode multiplier 和 CPR。遥测同时保留 raw 与
normalized 语义；不能假定两轮一定使用同一倍频或同一安装符号。

### P2A-04 AT8236 无动力逻辑 PWM 安全链

**固定引脚**：左 PB8/PB9，右 PB12/PB13，TIMA0 CC0-CC3，10 kHz；启动和 fatal 路径强制四路低。

**失败方案**：原计划用 OpenOCD 写 RAM 触发点动，但实际即使没有显式 halt/reset，连接写入也会
使目标重启，串口出现旧新 sequence 混合。SWD RAM 写不能作为实时或带动力命令入口。

**正式方法**：UART 一次性 frame，CRC16、双 magic、唯一 sequence、单电机、绝对值不超过 10%、
最长 500 ms；ServiceTask 只 staging，SystemTask 在 100 Hz 边界唯一写输出；完成、拒绝、timing
resync 和 fatal 均清 pending 并归零。

**证据边界**：VM/4S 断电、动力线断开时，左右正负 5%/200 ms 均恰好 20 active frame 后归零，
链路和 Health 干净。这只证明逻辑链，不证明物理引脚波形、D153B、VM 或电机运动。

### P2A-05 MG370 与 513X Motor Profile

**设计**：MG370/513X 共用一套 BSP、编码器、执行器安全状态机、转速换算和 PID 接口。唯一选择宏
`ECHO_MOTOR_PROFILE_SELECTION`，禁止 OLED/UART 运行时切换，不为型号复制引脚或 BSP。

**MG370 v1**：12 V、1.1 A、堵转规格 6.2 A、减速比 34.014、GMR AB 3.3 V、500 PPR、
最高 300 rpm；左 `68028/+1/x4`，右 `17007/-1/x1`。CPR provisional，左右 motor output sign、
起转/最大 PWM、速度/加速度、堵转保护和 PID 尚未冻结，`closed_loop_ready=0`。

**513X 门禁**：关键额定电压、堵转电流、减速比、编码器接口/电平/PPR 未确认，选择 513X 必须
产生明确编译错误，不能用零值生成看似可运行的固件。

**验证**：MG370 FreeRTOS/App 0/0，App Code `58152`、ZI `16644`；type 7 Profile fixture 通过；
513X 负向编译按预期失败。新 Profile 固件烧录/板测仍为 not run。

### IMU-S01 MPU6050 / MPU6500-compatible 备用 IMU

```yaml
module_id: IMU-S01
module_name: MPU6050 / MPU6500-compatible 备用 IMU
phase: isolated spike before Phase 2B
status: bench_passed
integration_status: pairwise_passed
record_kind: normal
last_verified_at: 2026-07-15T23:43:02+08:00
firmware_commit: uncommitted checkpoint
firmware_build_id: 0x02F0
hardware_revision: two modules, WHO_AM_I 0x70 and 0x68
owner: ServiceTask / ImuService
```

**硬件身份**：已测两块地址均为 `0x68`，`WHO_AM_I` 分别为 `0x70` 和 `0x68`。共用一套驱动，
由 Profile 1 选择温度公式和启动静止门限；禁止只凭模块丝印命名。

**接线和边界**：3V3/GND、PA0 SDA、PA1 SCL，与 OLED 共用 I2C0 400 kHz；不使用 PB8/PB9，
不产生电机输出。不新增 IMU Task，ServiceTask 唯一采样写快照，SystemTask 只读。

**当前配置**：100 Hz 六轴采样、42 Hz 芯片 DLPF、+/-500 dps、+/-4 g、连续静止 300 点偏置、
约 25 Hz 一阶低延迟软件低通；保留 raw、bias-corrected 和 filtered 数据。当前没有接入四元数或
卡尔曼，姿态估计应后续放入 `module/estimation`，不能塞进 device 驱动。

**静止观察**：两块各有 600 秒原始数据。`0x70` 的 Z 均值约 `0.010664 dps`，纯积分约
`6.4 deg/10 min`；`0x68` 的 Z 均值约 `-0.002115 dps`，约 `-1.3 deg/10 min`，但加速度
模长约 `1.083 g`，正式姿态使用前需六面标定。数据归档位置见对应 worklog，正式算法冻结前
必须重新采集标准化数据。

**故障安全**：离线重初始化必须立即发布 invalid 快照，不能继续重复最后一份 valid/READY 数据。
现场工具同时拒绝四个遥测通道完全冻结。软件注入 3 次连续采样失败后，实测状态为
`READY -> INVALID -> VALID_NOT_READY -> CALIBRATING -> READY`，111 个 INVALID 帧三轴为 0；
最终 active=0、sticky 保留 IMU offline，CRC/gap/deadline 全 0。

**共享总线问题**：OLED + IMU + UART 曾复现 Control 帧尾被截断。DMA_DONE 后等待 EOT、UART DMA
统一由 ServiceTask 启动、IMU 短优先 quiet window 和 OLED backlog 后优先到期 IMU，已使 600 秒
联合运行保持 CRC/gap/I2C/deadline 全 0。

**当前证据边界**：第一块修复后 120 秒为 `12195/122` Control/Health，100/1 Hz，全部门禁为 0。
三轴 Motion、温度时间序列和物理断开恢复仍未完成；面包板只能四针整体拔插，物理故障测试等待
独立插头、跳帽或串联开关。正式底盘 IMU 仍为 ICM42688，本 spike 不能替代 Phase 2B 验收。

**暂停状态**：用户决定先恢复 Phase 2A 电机工作。算法补偿、Motion、六面标定和 ICM42688
均 deferred；恢复时重新采集标准化数据，不能直接用临时面包板统计冻结正式参数。

## 6. 后续模块新增条目的最低内容

每个新条目使用 `MODULE_DEBUG_RECORD_TEMPLATE.md`，并在本文件索引中增加唯一 ID。建议 ID：

```text
P2B-ENC-01   编码器速度换算
P2B-IMU-01   ICM42688 device
P2B-EST-01   姿态估计
P2B-CTL-01   单轮速度 PID
P2B-CTL-02   双轮航向控制
P2C-GIM-01   X42S UART 协议
P2D-VIS-01   树莓派通信
P3-MIS-01    任务状态机
P4-SYS-01    整车压力测试
```

条目必须链接对应 worklog/Phase 文档，并写出最后一次验证日期和固件身份。发现旧结论错误时不
静默改写历史；增加带日期的勘误，说明旧结论为什么不再成立以及新的证据。

## 7. 本次回溯补录的来源

以下文件是本手册历史条目的主要证据来源；条目与来源冲突时，以更接近实测时点且保存了证据的
Phase 文档/worklog 为准，并在条目中增加勘误：

- `docs/PHASE1A_BASELINE.md`
- `docs/HARDWARE_VALIDATION_20260713.md`
- `docs/PHASE1B_RTOS_SKELETON.md`
- `docs/PHASE1C_CLOCK_TIMEBASE.md`
- `docs/PHASE1E_OLED_UI.md`
- `docs/phases/PHASE1F_OPERABILITY_DIAGNOSTICS.md`
- `docs/learning/PHASE1F_OPERABILITY_DIAGNOSTICS.md`
- `docs/worklogs/2026-07-15_phase1f_operability_diagnostics.md`
- Phase 2A 工作树中的 `docs/phases/PHASE2A_AT8236_CHASSIS_ENCODER.md`
- Phase 2A 工作树中的左右编码器、AT8236 logic PWM 和 Motor Profile worklog
- 本 spike 的 `docs/spikes/MPU6050_HARDWARE_SPIKE.md`
- 本 spike 的 `docs/worklogs/2026-07-15_mpu6050_hardware_spike.md`
- 本 spike 的实时交接和当前任务已记录的串口静止统计

Phase 2A 与 MPU 仍在进行中，其条目只描述已经达到的证据层级，不能替代最终 Phase 文档。
