# ECHO 模块组合与集成防退化手册

状态：living document

本文件解决一个长期问题：每个模块单独能运行，不代表把底盘、IMU、云台、视觉、OLED、遥测和
任务状态机组合后仍能工作。目标不是承诺“永远不会出现集成问题”，而是让资源冲突、时序退化、
安全状态和接口分叉在模块完成时就暴露，而不是到赛场整车联调时集中爆发。

每个可独立验收的小模块完成时，必须同时更新：

1. `DEBUGGING_PLAYBOOK.md`：记录该模块怎么调、遇到什么问题、怎么恢复。
2. 本文件：记录该模块占用什么资源、如何组合、缺失/故障时如何降级、与现有模块的回归结果。
3. 当次 worklog、当前 Phase 文档和实时交接。

未填写集成合同，或尚未通过适用的组合回归时，只能报告“单模块通过”，不得报告“可集成完成”。

## 1. 引脚必须可迁移，但不是运行时任意切换

### 1.1 正确目标

ECHO 的物理引脚采用“集中、编译时选择、重新生成、重新验收”：

```text
App / Mission / Control / Service / Device
                |
                v
        BSP logical API
                |
                v
board resource alias / SysConfig generated symbol
                |
                v
MCU peripheral instance + pin mux + physical PA/PB pin
```

- App、Mission、control、estimation、service 和 device 禁止知道 `PA0`、`PB8` 等物理编号。
- device 只调用 `BSP_I2C_WriteRead()`、`BSP_Encoder_SampleLeft()` 等逻辑接口。
- BSP 使用集中资源别名或 SysConfig 生成符号，不在多个 `.c` 复制端口、pin mask 和 IOMUX 常量。
- 物理引脚、外设 instance、DMA channel 和 IRQ 只允许在 SysConfig/板级资源映射中冻结。
- 更换到 MCU 支持的候选引脚时，只修改集中配置和接线文档，重新生成、构建和板测；上层算法、
  协议和任务不应修改。

这不等于运行时让 OLED 或 UART 随意切换引脚。Timer capture、QEI、PWM、UART、I2C 等复用功能受
MSPM0 pin mux 限制；运行时任意重映射会破坏输出安全、ISR 所有权和静态分析，因此禁止作为普通
功能提供。

### 1.2 如果候选引脚不支持原外设

不能通过改宏强行映射不存在的复用功能。正确处理顺序：

1. 在数据手册/SysConfig 中确认候选 pin 支持所需 peripheral function。
2. 如果支持同一 instance，只改变 SysConfig 和板级 alias。
3. 如果只支持另一 instance，在 BSP 内新增同接口 backend，并重新评估 IRQ、DMA、时钟和 errata。
4. 如果只能从硬件 QEI 改成 GPIO 软件解码，上层仍使用相同 encoder API，但必须重新冻结倍频、
   最大中断率、CPU 预算和故障语义。
5. 如果没有安全可行映射，编译时明确 `#error`，不能静默退化成错误引脚或空实现。

### 1.3 改引脚的固定验收步骤

```text
查 MCU mux/电气属性
-> 查资源账本冲突
-> 修改 SysConfig/board alias
-> 重新生成 generated
-> 检查 semantic diff
-> FreeRTOS/App 0/0 full rebuild
-> VM/高功率断电检查 idle level
-> 单模块板测
-> 与共享总线/IRQ/Timer 邻居两两回归
-> 当前完整基线回归
-> 更新接线、Profile、调试和集成文档
```

引脚改变属于硬件接口改变。即使编译通过，也必须重新烧录和板测；旧引脚的历史证据不能自动
继承到新引脚。

## 2. 集成合同

每个模块必须声明以下内容，缺一项不得标记 `integration_ready`：

| 类别 | 必填内容 |
| --- | --- |
| 逻辑接口 | 输入、输出、单位、坐标系、符号、版本、失败返回 |
| owner | 初始化者、唯一写入者、ISR owner、快照 owner |
| 引脚/外设 | 逻辑 role、physical pin、instance、mux、idle level、电平容限 |
| 实时性 | 周期、最大执行时间、deadline、ISR 最大频率、允许 jitter |
| 阻塞 | 每个等待的超时、mutex/queue/ring、满载降级策略 |
| 资源 | Timer、DMA、IRQ、任务、栈、静态 RAM、Flash、协议 frame ID |
| 初始化 | 顺序、依赖、重复初始化行为、模块缺失时行为 |
| 安全 | 默认输出、失联、stale、fatal、debug halt、reset 行为 |
| 诊断 | online/stale/error/drop/high-water/stack/heap/版本/Profile |
| 组合测试 | 至少一个共享资源邻居和当前完整基线 |
| 恢复 | 断电、reset、重新探测、重新烧录或编译宏恢复方法 |

## 3. 集中资源账本

后续模块加入前先更新本表。物理引脚表同时在接线指南维护；这里重点记录软件所有权、共享关系和
集成风险。

| 资源 | 当前 owner | 消费者 | 当前配置 | 组合风险与迁移要求 |
| --- | --- | --- | --- | --- |
| MCLK | SysConfig/platform | 全系统 | 80 MHz | 改变后重验 Tick、UART、I2C、Timer 和所有控制周期 |
| TIMG12 | `bsp_time` | 全部诊断/超时 | 1 MHz 32-bit | 禁止被 PWM/capture 复用；改 instance 后重验回绕和 halt 行为 |
| UART1 TX/RX | UART BSP/Service | telemetry、参数、执行器命令 | PA8/PA9，230400 8N1 | 上层不得知道引脚；改 pin/instance 后重验 DMA、RX IRQ、协议压力 |
| UART TX DMA | `bsp_uart_tx_dma` | SerialTx | physical DMA channel 3 | 当前有编译时 channel 3 约束；迁移 DMA 必须审查 errata 和中断 |
| I2C0 | `bsp_i2c` | SSD1306、MPU spike，未来低速设备 | PA0/PA1，400 kHz，静态 mutex | 生成符号仍名为 `OLED_I2C_*`，应迁移为总线 role；新增设备重验带宽/quiet/恢复 |
| OLED | DisplayTask/SSD1306 | UI | `0x3C/0x3D` | 可离线降级；不得阻塞控制；全刷约 38.5 ms |
| MPU spike | ServiceTask/ImuService | SystemTask/telemetry | `0x68`，WHO `0x68/0x70` Profile 1，100 Hz | 与 OLED 共享 I2C；offline 立即 invalid，重新探测/校准后 READY；正式 ICM42688 需重新验收 |
| TIMG8 QEI | `bsp_encoder` | 左编码器 | PA29/PA30，x4 | 候选 pin 必须支持 TIMG8 PHA/PHB；未接输入禁止浮空 |
| GPIO Group1 IRQ | `bsp_encoder` 当前 handler | 右编码器 E2A | PB6 IRQ，PB7 direction，x1 | 未来其他 GPIO Group1 必须共享分发，禁止定义第二个 `GROUP1_IRQHandler` |
| TIMA0 CC0-3 | motor BSP/ChassisActuator | AT8236 双路 | PB8/PB9/PB12/PB13，10 kHz | 改 pin 必须确认同 Timer channel、idle low 和 fatal 双低 |
| PB22 LED | `bsp_led` | Service heartbeat | 1 Hz toggle | 非关键；不得成为实时同步依据 |
| Serial TX ring | SerialTx | Control/Health/ACK/Profile | 1024 B，单次 128 B | Health 已到 128 B 上限；新增 frame 不得无界扩展或破坏原子写 |
| SystemTask | App | timing、参数应用、执行器、采样读取 | 100 Hz | 保持唯一高频 owner；新增工作必须测 execution/deadline/stack |
| ServiceTask | App | UART、健康、IMU spike | 2 ms | 不得持续堆入阻塞设备；新增服务先评估栈和最坏执行时间 |
| DisplayTask | App | OLED/UI | 500 ms online | 低优先级，可跳帧；不能成为控制依赖 |
| Health frame | SystemHealth/Telemetry | 主机/OLED | 1 Hz，128 B | 新字段需 schema/拆帧方案，不可继续追加超限 |

资源表中的引脚是当前已冻结配置，不是上层代码依赖。改变引脚后更新这一行、接线指南和模块条目，
而不是到处搜索替换 `PA/PB` 字符串。

## 4. 当前可迁移性审查

### 4.1 已符合方向

- SSD1306 和 MPU device 不直接读取 PA0/PA1，只调用 `bsp_i2c`。
- `bsp_i2c` 使用 SysConfig 生成的 instance、PORT、PIN 和 IOMUX 宏，没有直接写数值端口地址。
- 左右编码器 BSP 使用 `LEFT_ENCODER_QEI_INST`、`GPIO_RIGHT_ENCODER_*` 等生成宏。
- Motor Profile 不包含物理引脚，MG370/513X 共用相同 BSP 和固定板级接口。
- App/Service/Control 不直接调用 GPIO/Timer DriverLib。
- ImuService 是设备无关快照边界；离线时立即撤销 valid/READY，未来 ICM42688 必须复用该门禁，
  不能复制一套绕过重新校准的控制入口。

### 4.2 尚需偿还的可迁移技术债

1. 共享 I2C 的生成实例仍叫 `OLED_I2C_INST`，现在已经同时服务 OLED 和 IMU。后续应在不手改
   generated 的前提下，把 SysConfig 实例名和 BSP 逻辑名迁移为 `AUX_I2C` 或 `SENSOR_I2C`。
2. UART DMA 当前明确要求 physical channel 3。该约束是安全的显式失败，但还不是任意 DMA 可迁移；
   改 channel 前需要结合 MSPM0 errata 做独立验证。
3. `GROUP1_IRQHandler` 当前直接属于右编码器。后续任何 Group1 GPIO 中断加入前，必须建立 BSP
   内部静态分发器，禁止多个模块各自定义同名 handler。
4. 尚无单独的 `board_resource_map`/resource version。正式整车前应让 telemetry/Health 能报告
   board map version，避免固件和线束版本错配。
5. Phase 2A 与 MPU spike 当前位于不同工作树，尚未执行正式代码组合；不能因各自单模块通过就
   宣称两者已经可一起运行。
6. MPU spike 当前为 `pairwise_passed`：只证明 OLED + MPU + UART 邻居组合；用户已暂停该 spike，
   未完成 Motion、物理恢复或与 Phase 2A 的正式组合，因此禁止写 `integration_ready`。

## 5. 防止“单独能跑，拼起来就坏”的设计约束

### 5.1 模块缺失不能拖死系统

- OLED、非关键遥测、备用传感器缺失时进入 offline/degraded，控制环继续。
- 关键传感器 stale 时执行器进入明确的零输出/禁能，而不是继续使用旧数据。
- 模块 init/probe/retry 都有超时；禁止开机无限等待某个外设出现。
- 编译时可选模块必须提供明确的 unavailable 状态，不提供伪造零数据。

### 5.2 初始化顺序必须显式

推荐顺序：

```text
SysConfig clocks/pins
-> BSP safe outputs/timebase
-> diagnostics/fault storage
-> communication and shared buses
-> device drivers in disabled/offline state
-> services/snapshots
-> tasks
-> scheduler
-> module self-test/probe
-> explicit arm after all safety gates
```

执行器安全输出必须早于可能失败的设备探测。任何模块不得依赖“另一个任务大概已经初始化”。

### 5.3 共享中断必须只有一个入口

- 一个 vector 只能定义一个 ISR 符号。
- 同组 GPIO 中断由 BSP 聚合读取 status，再调用静态登记的最小 handler 或更新对应计数。
- ISR 不进行动态注册、malloc 或协议解析。
- 新增共享 IRQ 后必须测试同时 pending、未知 pending bit 和中断风暴限幅。

### 5.4 共享总线要有预算和仲裁

- I2C/SPI/UART 每个事务有长度上限和超时。
- 明确 mutex owner、优先级反转风险和高频任务是否允许调用。
- 计算最坏总线占用，不以平均速率代替峰值。
- OLED 全屏刷新、IMU 100 Hz、参数命令和 telemetry 同时运行时必须保存 error/drop/deadline 证据。

### 5.5 协议和故障 ID 必须集中登记

- Telemetry frame type、parameter ID、Health issue ID、fault code 和 command status 不得在多个头文件
  各自手写同一数字。
- 新增 ID 前检查冲突，更新 schema/version 和主机解析 fixture。
- 未识别新 frame 必须可跳过，不能破坏旧 Control/Health 解析。

### 5.6 资源预算必须保留余量

- 新任务加入后检查所有任务 stack min-free，而不仅是新任务。
- 检查 heap minimum、Serial ring high-water、queue/ring overflow 和静态 RAM 增量。
- 检查 SystemTask max execution、period/jitter/deadline。
- 检查 UART 带宽、I2C bus occupancy、最大 ISR 频率和中断优先级。
- 资源门槛接近上限时先拆协议、降频或改变调度，不通过扩大所有 buffer 掩盖问题。

## 6. 模块集成测试阶梯

每个模块按以下阶梯推进，不能从单模块直接跳到整车：

```text
host/unit fixture
-> 0/0 full build
-> 单模块板测
-> 故障/恢复
-> 与共享资源邻居两两组合
-> 当前阶段所有模块组合
-> 上一阶段完整回归
-> 无断点连续运行
-> 高负载/极限/故障演练
```

### 6.1 两两组合测试例

| 新模块 | 必测邻居 | 原因 |
| --- | --- | --- |
| I2C IMU | OLED + UART telemetry | 共享 I2C、quiet window 和任务负载 |
| 新 UART 设备 | telemetry UART/command | instance、DMA、IRQ、带宽、frame ID 冲突 |
| 编码器 | PWM/电机 | Timer/IRQ 负载、方向、噪声和地弹 |
| 电机控制器 | OLED/参数/遥测 | 调参时不能改变唯一输出 owner 或阻塞控制 |
| 云台 | 底盘控制 | 任务频率、UART/DMA、供电和安全停机 |
| 树莓派通信 | Mission/参数/执行器 | 命令仲裁、失联、旧包和重连 |

### 6.2 当前完整基线回归最低指标

- FreeRTOS/App 0 Error / 0 Warning。
- Control/Health/新增周期 frame 各自达到目标频率。
- CRC、sequence gap、drop、timeout、I2C/SPI/UART error 无非预期增长。
- SystemTask deadline miss 为 0，max execution 和 jitter 不明显退化。
- quiet acquire/release 配对、active=0。
- 所有任务 stack 和 heap 保持在项目门槛以上。
- 非关键模块断开时控制继续；关键模块 stale 时输出进入安全状态。
- reset/debug halt 后默认输出保持零，必须重新显式 arm。

## 7. 每个模块的集成记录格式

在本文件末尾为每个模块建立条目，至少包含：

```text
module_id / version / phase / status
logical API
pin roles and current board mapping
peripheral/DMA/IRQ/task/memory/frame/fault resources
initialization owner and order
blocking and worst-case timing
default/degraded/fault behavior
diagnostic fields
pin migration method and restrictions
pairwise tests
full baseline regression
known conflicts and next integration gate
```

## 8. 组合集成完成门禁

模块只有同时满足以下条件才能写 `integration_ready`：

- [ ] 上层不含物理 pin/port/IOMUX 常量。
- [ ] 当前 physical mapping 只存在于 SysConfig/board resource/BSP。
- [ ] 不支持的 pin/peripheral/Profile 组合会编译失败或保持输出锁定。
- [ ] Timer、DMA、IRQ、任务、内存、协议 ID 已登记且无冲突。
- [ ] 初始化 owner、顺序、重复初始化和缺失设备行为明确。
- [ ] 默认、offline、stale、fatal、reset 和 debug halt 安全状态明确。
- [ ] 单模块构建、烧录、板测、故障恢复和连续运行达到要求。
- [ ] 至少完成一个共享资源邻居的两两组合测试。
- [ ] 完成当前阶段组合和上一阶段基线回归。
- [ ] 调试手册、集成手册、worklog、接线和实时交接全部同步。

## 9. 禁止的“拼凑式”实现

- 在 App/Module 里复制 `PAx/PBx`、Timer instance、DMA channel 或 IOMUX 数字。
- 为 MG370、513X 或不同引脚复制整套 BSP/device 源码。
- 多个模块各自定义同一个 IRQ handler。
- 为每个设备创建一个任务，却没有周期、优先级、栈和 owner 依据。
- 用全局变量绕过 service 仲裁，让 UI、树莓派和 Mission 同时写执行器。
- 通过无限增大 ring/queue 掩盖吞吐和调度错误。
- 模块断开后无限重试、assert 或拖停控制环。
- 只验证新功能，不重跑上一阶段的 100 Hz、Health、deadline、I2C、UART 和资源门禁。
- 把“所有文件能链接”当作“整车可集成通过”。

## 10. 后续落地动作

1. 正式 Phase 2A 后续实现引入集中 board resource version，并在诊断中报告。
2. 正式集成 MPU/ICM42688 前，将共享 I2C 的逻辑命名从 OLED 专属迁移为总线 role。
3. 增加 GPIO Group1 BSP 分发约束，避免后续按键/传感器中断与右编码器冲突。
4. 每个新模块在实现前先在资源账本预登记，验收后填写实际占用和组合测试结果。
5. Phase 4 前生成最终资源矩阵、初始化顺序图、故障传播表和整车回归清单。
