# ECHO 小模块调试记录模板

> 使用方法：复制本模板的章节到 `DEBUGGING_PLAYBOOK.md`，分配唯一 `module_id`，同时新建对应
> worklog。没有执行的项目写 `not run`，明确延期写 `deferred`，禁止删除未通过项。

```yaml
module_id: P2B-XXX-01
module_name: <名称>
phase: <Phase>
status: planned | implemented | build_passed | bench_passed | completed | failed | deferred
integration_status: isolated | pairwise_passed | integration_ready | blocked
record_kind: normal | retrospective | correction
last_verified_at: YYYY-MM-DDTHH:MM:SS+08:00
firmware_commit: <commit 或 uncommitted>
firmware_build_id: <build id>
hardware_revision: <模块/板卡版本和丝印>
owner: <唯一运行时写入者或数据 owner>
```

## 1. 目标与非目标

- 本模块解决什么问题。
- 输入、输出和对外接口。
- 明确不做的功能，避免提前混入下一模块或下一阶段。
- 完成定义和验收标准。

## 2. 架构与所有权

```text
上游 -> 当前模块 -> 下游
```

- DriverLib/SysConfig 的唯一调用层。
- 数据快照的唯一写入者和所有读者。
- 执行器的唯一输出写入者。
- ISR 只执行哪些最小动作。
- 周期、优先级、超时、队列/ring 容量和失败策略。

## 2.1 引脚迁移与组合集成合同

| 资源类别 | 逻辑 role | 当前物理映射 | owner | 共享者/冲突 | 迁移限制 |
| --- | --- | --- | --- | --- | --- |
| GPIO/pin mux |  |  |  |  |  |
| peripheral instance |  |  |  |  |  |
| Timer/channel |  |  |  |  |  |
| DMA |  |  |  |  |  |
| IRQ/vector/group |  |  |  |  |  |
| task/priority |  |  |  |  |  |
| static RAM/stack/ring |  |  |  |  |  |
| telemetry/command ID |  |  |  |  |  |
| health/fault/parameter ID |  |  |  |  |  |

- 上层是否完全不知道 PA/PB、IOMUX、Timer 和 DMA 数字。
- 换到 MCU 支持的候选引脚需要修改哪些集中配置；哪些上层文件必须保持不变。
- 不支持的 pin/peripheral/Profile 组合如何编译失败或保持 unavailable/输出锁定。
- 初始化 owner、初始化顺序、重复初始化行为和依赖模块缺失时的降级行为。
- 与至少一个共享总线/IRQ/Timer/任务邻居的两两测试。
- 当前阶段完整组合和上一阶段基线回归结果。

## 3. 硬件身份与接线

| 项目 | 实际值 | 证据/确认方式 |
| --- | --- | --- |
| MCU/开发板 |  |  |
| 模块型号/丝印 |  |  |
| 芯片身份寄存器 |  |  |
| 模块 revision |  |  |
| 电源电压 |  |  |
| 逻辑电平 |  |  |
| 总线地址/波特率 |  |  |
| 引脚 |  |  |
| 上拉/下拉/去耦 |  |  |
| 线束与共地 |  |  |

必须写明测试时哪些设备未连接，特别是 VM/4S、电机动力线、云台和其他高功率输出。

## 4. 安全条件

- 用户是否在场。
- 机构/轮组是否架空。
- 默认输出是否确认是零。
- 限流值、电压和物理断电位置。
- 板卡/模块是否有异常发热。
- 本次允许和禁止的动作。
- 失控、堵转、总线锁死或固件失联时的恢复动作。

低功率模块不适用的项目写 `not applicable`，不能直接删除本节。

## 5. 实现摘要

- 修改文件和接口。
- 寄存器、量程、频率、滤波、Profile 或协议版本。
- 符号、单位、坐标系和倍频定义。
- 默认状态、超时、重试和 offline/fault 行为。
- 为后续控制器预留但尚未启用的接口。

## 6. 首次现象与复现步骤

```text
前置条件：
操作步骤：
预期：
实际：
出现频率：
是否依赖调试器/断点：
```

记录最初错误，不要只写最终成功结果。若没有发现故障，写明正常路径的可重复步骤。

## 7. 诊断字段

| 层级 | 字段/工具 | 正常值 | 异常含义 |
| --- | --- | --- | --- |
| BSP |  |  |  |
| device |  |  |  |
| service |  |  |  |
| task/RTOS |  |  |  |
| telemetry |  |  |  |
| host tool |  |  |  |

至少覆盖 error、timeout、drop/overflow、stale/offline、frequency、deadline、stack、heap，以及本模块
特有的身份、方向、温度、电流或计数指标。

## 8. A/B 对照

| 试验 | 只改变的变量 | 条件 A | 条件 B | 结果 | 能证明/不能证明 |
| --- | --- | --- | --- | --- | --- |
| A/B-1 |  |  |  |  |  |

每次只改变一个主要变量。无法满足时必须解释为什么结论仍然有限。

## 9. 根因与修复

- 根因状态：`confirmed / probable / open`。
- 根因所在层级。
- 排除过的错误方向及证据。
- 修复内容和为什么有效。
- 修复是否改变接口、时序、资源、安全或接线。
- 旧固件/配置的恢复方法。

不能确认根因时写当前最强证据和下一项判别试验，不得把猜测写成事实。

## 10. 构建验证

| 项目 | 命令/配置 | 结果 |
| --- | --- | --- |
| SysConfig |  |  |
| FreeRTOS full rebuild |  |  |
| App full rebuild |  |  |
| Warning | 必须为 0 |  |
| Program size | Code/RO/RW/ZI |  |
| HEX SHA-256 |  |  |
| 负向编译/fixture |  |  |

## 11. 烧录与镜像验证

- DAPLink/OpenOCD/Keil 方式和探针身份。
- program/verify/reset 是否分别通过。
- 快速 CRC 失败时是否执行逐字节 readback。
- 目标实际运行的 build ID/版本/Profile。
- 是否发生调试复位或重新 attach。

## 12. 单模块板测

| 测试 | 条件 | 时长/次数 | 预期 | 实测 | 结论 |
| --- | --- | ---: | --- | --- | --- |
| 启动 |  |  |  |  |  |
| 静止/空闲 |  |  |  |  |  |
| 正方向 |  |  |  |  |  |
| 反方向 |  |  |  |  |  |
| 边界输入 |  |  |  |  |  |

## 13. 故障注入与恢复

至少按模块风险选择：断线、无 ACK、坏 CRC、半帧、非法 ID、范围外、queue/ring 满、stale、
sensor offline、命令超时、timing resync、fatal、brownout、堵转或温升。

| 故障 | 注入方法 | 预期安全状态 | 实测 | 自动恢复 | 人工恢复 |
| --- | --- | --- | --- | --- | --- |

禁止为了测试而热插拔未设计为热插拔的模块；优先断电改线或使用明确的软件故障注入。

## 14. 连续运行与资源

```text
运行时长：
启动方式：
调试器连接：yes/no
中途暂停：yes/no
主频率：
CRC/gap/drop/timeout：
deadline/max overrun：
I2C/SPI/UART error：
active/sticky fault：
各任务 stack min-free：
heap minimum：
温度/电流/机械状态：
意外复位：
```

## 15. 证据位置

- worklog：
- 当前 Phase 文档：
- 原始数据或仓库外路径：
- 小型 JSON/CSV/截图：
- 固件 hash：
- 仪器型号和截图编号：

临时路径必须注明可能失效；没有保存原始数据时明确写“未留存”。

## 16. 验收清单

- [ ] 设计边界和 owner 明确。
- [ ] 物理引脚/Timer/DMA/IRQ 只存在于集中资源层，上层不写死。
- [ ] 资源账本、初始化顺序、缺失模块降级和 pin 迁移方法已记录。
- [ ] 接线、电平、供电和硬件身份确认。
- [ ] 所有等待有超时，输出默认安全。
- [ ] SysConfig、FreeRTOS、App 0 Error / 0 Warning。
- [ ] program/verify/reset 通过。
- [ ] 单模块板测通过。
- [ ] 故障注入和恢复通过。
- [ ] 规定时长连续运行通过。
- [ ] 诊断计数、频率、deadline、栈和 heap 无异常。
- [ ] worklog、调试手册、状态/交接已同步。
- [ ] 集成手册已同步并完成适用的两两组合与上一阶段回归。
- [ ] 未执行和 deferred 项没有被伪报为 passed。

只有单模块适用项勾选后，`status` 才能写 `completed`；只有集成适用项也通过后，
`integration_status` 才能写 `integration_ready`。

## 17. 遗留风险、下一步与禁止事项

- 未冻结参数和原因。
- 进入下一小模块的条件。
- 后续必须重新验证的组合场景。
- 禁止修改的引脚、方向、Profile 或安全契约。
- 需要用户在场或额外授权的操作。

## 18. 勘误记录

| 日期 | 旧结论 | 新证据 | 修正后结论 | 影响范围 |
| --- | --- | --- | --- | --- |
