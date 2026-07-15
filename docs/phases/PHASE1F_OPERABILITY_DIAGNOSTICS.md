# Phase 1F：赛场可操作性与故障诊断

状态：核心软件与低功率板测验收通过；Flash、物理 ADC 五键、硬件看门狗 deferred

基线：`cb7c4c3` / `phase-1e-oled-ui`

## 1. 阶段目的

Phase 1A-1E 已证明工程能构建、调试、遥测和显示，但加入电机后，错误代价会显著上升。
Phase 1F 先把“怎样知道系统能不能上场、出问题先查哪里、怎样安全修改参数”固化下来，
再进入底盘与云台开发。

本阶段完成后，操作者应能在不读源码的情况下回答：

- 当前固件和配置是谁？
- 每个关键任务、服务和外设是否健康？
- 最近一次故障是什么，发生在哪一层？
- 串口/OLED 断开时系统如何降级？
- 参数是否有效、是否已经应用，持久化能力当前是启用、无效还是 deferred？
- 当前是否允许执行器动作？

## 2. 明确非目标

- 不接入底盘电机、编码器、AT8236、IMU、云台或树莓派。
- 不实现具体赛题 Mission。
- 不运行真实 PID。
- 不在没有硬件安全门的情况下增加任何运动输出。
- 不为“看起来完整”而一次引入大量任务或抽象层。

## 3. 实施拆分

本阶段的核心门禁是 1F-A、1F-B 的软件部分、1F-C 和 1F-F。物理 ADC 五键、Flash 掉电
保存和硬件看门狗分别验收：没有硬件证据时只能标记 `deferred`，既不能写成通过，也不能
阻塞对已完成核心能力的客观记录。是否允许带着某个 deferred 项进入 Phase 2A，要在 1F
终验时按其对执行器安全的影响单独决定。

### 硬性约束

- 不新增任何运动输出，也不把未来执行器“占位接口”接到真实 PWM/UART/STEP 引脚。
- OLED、遥测和 Watch 必须读取同一健康语义，禁止各自计算一套 overall 状态。
- 诊断快照只供观察；任务间控制仍使用队列、通知、请求或只读状态快照。
- SystemTask 不等待显示、串口物理发送、Flash 或无界外设轮询。
- 新等待必须有 timeout，新队列/ring 必须有 drop/overflow/high-water 诊断。
- 新长期 RTOS 对象优先静态创建，并纳入运行次数、栈高水位和故障记录。
- `platform/generated` 只由 SysConfig 生成；本阶段不借机重排引脚或外设资源。
- 每个子项独立实现、独立回归，不把 UI、Flash、按键和看门狗混成一个不可隔离的提交。

### 1F-A：统一身份与健康快照（必做）

建立一个只读、版本化的系统快照，汇总而不是复制各模块内部诊断：

```text
build/version
boot count / uptime / reset reason（能力确认后）
overall health：OK / DEGRADED / FAULT
first sticky fault + current active fault mask
task alive / stack / deadline
UART RX/TX / telemetry / parameter status
OLED / I2C status
future sensor age / actuator armed 占位契约
```

要求：

- 每个健康项有 owner、更新时间戳、正常阈值和故障码。
- 区分“从未初始化”“在线”“降级”“故障”，不能只用一个布尔值。
- 首个关键故障 sticky；可恢复服务同时保留 current 状态和累计次数。
- 快照供 OLED、遥测和 debugger 读取，不作为模块间控制通道。
- 多字段快照使用版本/奇偶序列或临界区保证一致读取。

### 1F-B：赛场 UI 与输入契约（必做软件部分）

在当前两页诊断 UI 上建立稳定页面模型：

1. Overview：固件、uptime、总体健康、首故障。
2. RTOS：周期、deadline、各任务最小剩余栈。
3. Comms：UART RX/TX、CRC/drop/gap、参数结果。
4. Devices：OLED/I2C 和以后 IMU/执行器占位状态。
5. Parameters：只显示/编辑已登记且允许现场修改的参数。

输入只产生事件：`UP/DOWN/LEFT/RIGHT/OK`、长按、重复和超时。UI 不读取 GPIO/ADC，
输入后端不控制 OLED。虚拟键继续作为自动验收后端。

物理五键门禁：

- 如果硬件准备好，优先实现单 ADC 电阻梯形后端；引脚必须可配置。
- 在焊接前先给出电阻值、理论电压、1% 容差/ADC 误差窗口和去抖策略。
- 实测空闲及五键 ADC 分布，阈值不得只靠理论值。
- 若本阶段硬件仍未准备好，软件输入契约和虚拟键必须通过，物理后端标记 deferred，
  不得伪报完成。

### 1F-C：参数登记与安全应用（必做）

将参数从散落 ID 提升为统一元数据表：

```text
id / name / type / default / min / max / step / units / flags / version
```

串口和 OLED 共用同一登记表、校验和周期边界应用逻辑。至少覆盖当前 `kp/ki/kd/target`
测试参数，禁止 UI 和串口各维护一份范围。

要求：

- NaN/Inf、越界、未知 ID、忙状态有明确结果。
- 修改先成为 pending，在 SystemTask 周期边界原子应用。
- 每次应用有 transaction ID、apply sequence 和结果。
- 提供恢复默认值，但必须有确认流程。
- 参数编辑不得直接触发未来执行器动作。

### 1F-D：Flash 掉电保存（独立硬件门禁）

用户需要掉电保持，但 Flash 设计必须先确认 MSPM0 擦写粒度、保留区域和运行限制。实现时：

- 使用带 magic、schema version、payload length、sequence 和 CRC 的记录。
- 至少采用双槽/日志式提交，掉电时保留上一份完整记录。
- 启动时先校验，再加载；无效时使用编译默认值并报告原因。
- 只保存带 persistent flag 的参数。
- 采用延迟提交/显式 Save，禁止每次按键都擦写 Flash。
- 写入前确认不会覆盖程序、向量表、用户数据或 TI 保留区域。
- Flash 操作不得阻塞 100 Hz 控制路径；现阶段无电机也要测量最坏耗时。

验收必须包含：正常保存/复位、CRC 损坏回退、旧 schema 回退、模拟提交中断和擦写次数计数。
在 Flash 区域与策略核对完成前，本子项不得开始写入。

### 1F-E：复位原因、故障目录与可恢复操作（必做调研，按能力实现）

- 建立稳定故障码：类别、严重度、来源和建议动作。
- 捕获可由 MSPM0 可靠读取的复位原因；确认读清顺序和调试复位行为。
- 提供“清可恢复计数”命令，但不能清除尚在活动的故障，也不能掩盖首个关键故障证据。
- 断开 OLED、占用/断开串口、错误参数和服务超时都必须有明确降级路径。
- 看门狗先形成设计与实测计划：喂狗 owner、窗口、调试 halt 行为、复位证据和执行器
  默认安全。未证明这些条件前不启用为默认功能。

### 1F-F：赛场检查与一键诊断（必做）

提供简短、可重复的操作入口：

```text
环境检查 -> 全量构建 -> 烧录校验 -> 启动自检
-> 读取健康快照 -> UART/OLED 联合运行 -> 输出验收摘要
```

脚本不得写死 COM 号，不自动 push，不删除日志。失败报告要指出发生在环境、构建、SWD、
UART 协议、OLED/I2C、RTOS 或参数哪一层。

## 4. 推荐实现顺序

1. 冻结健康状态、故障码和参数元数据结构，不先改 UI。
2. 为现有 RTOS/UART/I2C/OLED 诊断建立只读适配，形成统一快照。
3. 增加 Overview/RTOS/Comms/Devices 页面，继续使用虚拟键验收。
4. 让串口参数工具和 OLED 编辑共用参数元数据与周期边界提交。
5. 做无 OLED、无串口、错误帧、队列满等软件故障注入。
6. 核实 Flash 手册和链接布局，再单独实现、构建和掉电/损坏测试。
7. 物理五键硬件到位后接入 ADC 后端并采样标定。
8. 最后评估看门狗，完成赛场检查脚本和连续运行。

每完成一步都应能回到前一份已验收固件。不得把 Flash、按键、看门狗和 UI 重构一次性混入
一个无法隔离问题的大提交。

## 5. 诊断接口要求

统一快照建议只暴露稳定语义，不直接把每个内部结构复制给 App：

```c
typedef enum {
    HEALTH_UNKNOWN,
    HEALTH_OK,
    HEALTH_DEGRADED,
    HEALTH_FAULT
} HealthState;

bool SystemHealth_ReadSnapshot(SystemHealthSnapshot *out);
```

实际结构在实现前评审。必须包含 schema/version 和一致性序列；字段单位写在名称中。Telemetry
可以发送压缩版本，但不能发一个与 OLED 含义不同的“第二套健康状态”。

## 6. 验收标准

### 构建与回归

- FreeRTOS 与 App 全量构建均为 0 Error / 0 Warning。
- Keil 构建/烧录/调试链和 VSCode 构建/烧录/调试链均可用。
- 原 Phase 1E OLED + 100 Hz 遥测联合测试不退化。
- `fault_code=0`，100 Hz 控制周期无 deadline miss。

### 功能

- 虚拟五键对每个事件只消费一次，所有页面可达且文本不越界。
- OLED 离线时 RTOS、遥测和参数通道继续运行，并显示/上报 DEGRADED。
- 串口离线或主机不读取时控制与 OLED 不被拖停，drop/timeout 可见。
- 非法参数不应用；合法参数只在周期边界应用；OLED 与串口规则一致。
- 统一快照的 overall、active 和 sticky 状态可由故障注入验证。
- 清计数操作不能清活动故障或改变执行器安全状态。

### Flash 子项（启用后才适用）

- 保存后冷启动恢复，CRC/schema 无效时可靠回到默认/上一有效记录。
- 模拟中断写入后至少一份旧记录仍有效。
- 写入地址、擦写次数和最坏阻塞时间有记录。

### 物理按键子项（硬件到位后才适用）

- 五键和空闲 ADC 分布实测不重叠，包含电源与容差裕量。
- 短按、长按、重复、组合噪声和去抖通过；无误触发。

### 连续运行

- 最少 10 分钟无断点运行；阶段终验建议 30 分钟。
- UART 遥测 CRC 0、sequence gap 0、发送 drop 0。
- OLED/I2C 无未恢复错误，quiet acquire/release 配对，active 最终为 0。
- 所有任务栈保留评审余量，heap minimum 不持续下降。
- 输出一份机器可读摘要和一份 worklog。

任何未执行的硬件门禁必须明确写 `deferred`，不能用代码审查代替板上验收。

## 7. 阶段交付物

- 源码与配置修改。
- 更新后的 `PROJECT_STATUS.md`、README 和本 Phase 文档。
- 一份 Phase 1F worklog 和相应学习文档。
- 通过验收后的单独提交与 annotated tag：建议 `phase-1f-operability-diagnostics`。
- 不自动 push。

## 8. 进入 Phase 2A 的门槛

只有以下条件都满足才接入底盘：

- 统一健康快照和故障目录稳定。
- 参数范围、周期边界应用和恢复默认已统一。
- 赛场检查流程可以重复执行。
- 已明确执行器默认零输出、通信失联和调试暂停的安全策略。
- Phase 1F 的必做项验收通过；deferred 的物理硬件子项有明确后续计划且不影响底盘安全。

## 9. 2026-07-15 实际结果

### 9.1 实现结论

- `ServiceTask` 每 100 ms 汇总一次 `system_health_snapshot_t`，是唯一运行时写入者；OLED、
  1 Hz Health 遥测和 debugger Watch 读取同一份快照语义，没有新增 HealthTask。
- 故障目录区分 UNKNOWN/OK/DEGRADED/FAULT，保存 active、recoverable sticky 和不可清除的
  first critical fault；瞬态事件保持 2 秒便于观察。
- 参数表统一登记 `kp/ki/kd/target` 的 type/default/min/max/step/units/flags/version。UART 与
  OLED 共用 staging/validation，只有 SystemTask 能在 100 Hz 周期边界应用。
- OLED 页面为 Overview、RTOS、COMM、DEVICE、PARAM；虚拟输入支持 PRESS、LONG_PRESS、
  REPEAT 和一次性 TIMEOUT。参数恢复默认需要 LONG OK 进入确认，再按 OK 提交。
- Health 帧为 112 B payload/128 B 完整帧，工具按类型独立统计 Control 与 Health，同时检查
  全局 sequence。COM 号改为必填参数。
- `BSP_Reset_Capture()` 在 SysConfig 初始化前只读复位原因；本阶段没有启用看门狗或执行器。

### 9.2 构建与烧录

| 项目 | 结果 |
| --- | --- |
| FreeRTOS full rebuild / AC6 6.21 | 0 Error / 0 Warning |
| App full rebuild / AC6 6.21 | 0 Error / 0 Warning |
| 程序尺寸 | Code=53048, RO-data=2864, RW-data=28, ZI-data=15956 |
| HEX SHA-256 | `1A205780BF54C948915A7D29E1DC6C240912C4A4FE4A95499D4B093FF25D3157` |
| DAPLink/OpenOCD | 1 MHz SWD，program/verify/reset 通过 |
| Flash 逐字节回读 SHA-256 | `0053B588B9293B34CBC9C8F00E27E283290D12ECC879F89BCFD71B0ACD3D2109` |

OpenOCD 目标端 CRC 算法有时无法在运行固件上重新 halt；烧录脚本会自动改用实际 Flash
逐字节回读并比较二进制 SHA-256。该回退本次通过，不等同于跳过校验。

### 9.3 功能与故障测试

- Unknown ID `0x0101`、NaN、Inf、KP 下越界和 target 上越界均拒绝；这项测试发现并修复了
  小枚举 ABI 将 `0x0101` 截成 `0x01` 的边界错误。
- 坏 CRC 不产生 ACK；半帧超时后下一帧可恢复。8 帧 burst 得到 2 APPLIED / 6 BUSY。
- Duplicate transaction 返回原 apply sequence；50 次连续调参全部应用，sequence `5..54`。
- 软件强制 OLED 离线时 Health DEGRADED、active/sticky=`0x1000`，Control 保持 100 Hz；解除后
  OLED 自动恢复。活动故障不可清，恢复后的 sticky 可由 Overview LONG OK 清除。
- 注入关键 CONTROL_STALE 时 Health FAULT、first fault=3；解除并请求清除后 active=0，但
  critical sticky 与 first fault 保留。
- 虚拟键页面索引按 `0->1->2->3->4` 到达全部页面；9 个事件消费 9 次。KP 经 metadata step
  从 1.0 到 1.1，并在边界 sequence=1 应用；确认恢复默认后 KP=1.0、sequence=2。

### 9.4 120 秒最终回归

| 指标 | 结果 |
| --- | ---: |
| Control / Health | 12194 @ 100 Hz / 121 @ 1 Hz |
| 总有效帧 | 12315 |
| CRC / gap / duplicate / out-of-order | 0 / 0 / 0 / 0 |
| 控制周期 | 9998-10002 us |
| 最大执行 / 最大 jitter | 23 us / 2 us |
| deadline / publish / transport / serial drop | 0 / 0 / 0 / 0 |
| I2C success / error | 43378 / 0 |
| OLED refresh / quiet acquire-release | 271 / 271-271 |
| quiet max / active | 38529 us / 0 |
| TX ring high-water | 280 B |
| 六任务最小剩余栈 | 180 / 128 / 151 / 102 / 104 / 104 words |
| heap minimum | 3064 B |
| Health / active / sticky | OK / 0 / 0 |

### 9.5 最终连续运行

无调试器、无中途暂停，MCU timestamp 窗口约 609.52 秒：

| 指标 | 结果 |
| --- | ---: |
| Control / Health / 总帧 | 60953 / 610 / 61563 |
| 分类型频率 | 100 Hz / 1 Hz |
| CRC / gap / duplicate / out-of-order | 0 / 0 / 0 / 0 |
| 控制周期 | 9998-10002 us |
| 最大执行 / 最大 jitter / deadline | 23 us / 2 us / 0 |
| publish / transport / serial / RX overflow | 0 / 0 / 0 / 0 |
| I2C success / error | 234578 / 0 |
| OLED refresh / quiet acquire-release | 1466 / 1466-1466 |
| quiet max / active | 38529 us / 0 |
| TX ring high-water | 280 B |
| 六任务最小剩余栈 | 180 / 128 / 151 / 102 / 104 / 104 words |
| heap minimum | 3064 B |
| Health / active / sticky / actuator permitted | OK / 0 / 0 / 0 |

最终 `reset run` 后又采集 3 秒，参数 apply sequence=0、parameter errors=0、Health OK、
active/sticky=0，板上保持干净默认状态。

### 9.6 三个独立硬件门禁

| 门禁 | 状态 | 原因与后续 |
| --- | --- | --- |
| 物理 ADC 五键 | deferred | 当前没有电阻梯形硬件、引脚冻结和 ADC 分布；虚拟输入契约已通过。Phase 3 菜单前必须完成。 |
| Flash 掉电保存 | deferred | 当前 scatter 将完整 128 KiB Flash 分配给程序，尚无保留双槽；未执行任何擦写或破坏性注入。实现前先冻结区域和擦写策略。 |
| 硬件看门狗 | deferred | 已确认 SDK 提供 iWDT/WWDT，但未完成喂狗 owner、debug halt、复位证据和执行器安全测试，默认不启用。 |

三项 deferred 不阻塞 Phase 2A 的分模块台架开发；Phase 2A 接真实电机前仍必须单独完成默认
零输出、失联、调试暂停和物理断电门禁。Phase 1F 只能报告本阶段完成，不能报告最终工程完成。
