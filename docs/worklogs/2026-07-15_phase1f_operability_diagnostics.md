# 2026-07-15 Phase 1F 赛场可操作性与故障诊断

性质：normal

本文件记录 Phase 1F 的真实实现、低功率板测和验收证据。未执行项写 `not run`，没有硬件
证据的独立门禁写 `deferred`。

## 1. Git 与任务范围

```text
正式工程：E:\ECHO
开发 worktree：C:\Users\Auror\ECHO-phase1f-work
开始分支：phase-1f-operability-diagnostics
开始 commit/tag：cb7c4c3 / phase-1e-oled-ui
结束分支：phase-1f-operability-diagnostics
结束 commit/tag：本 worklog 所在提交 / phase-1f-operability-diagnostics
是否 push：no
```

本次实际完成：

- [x] 1F-A 统一身份与健康快照
- [x] 1F-B 软件 UI 与输入契约
- [x] 1F-C 参数登记与周期边界应用
- [ ] 1F-D Flash 掉电保存：deferred
- [x] 1F-E 复位原因/故障目录/可恢复操作；看门狗 deferred
- [x] 1F-F 赛场检查与验收摘要

明确未做：电机、编码器、AT8236、IMU、云台、树莓派、真实 PID、物理 ADC 五键、
Flash 擦写/掉电保存和硬件看门狗启用。

## 2. 修改文件

按层列出，不粘贴完整 diff：

```text
App：main、ServiceTask、DisplayTask、五页 DiagnosticPage、UiInput 事件
Module/Service：SystemHealth、ParameterService metadata、128 B Health telemetry
BSP：bsp_reset 只读 reset cause
Platform/FreeRTOS：无语义修改
Config/generated：Keil App 项目只登记 system_health.c/bsp_reset.c；generated 未修改
Tools：capture、parameter_set、protocol_stress、phase1f_field_check、fixture test
Docs：AGENTS、状态、Phase 1F、长期规则、README、学习文档、本 worklog
```

健康快照唯一运行时写入者是 ServiceTask；OLED、Health telemetry 和 Watch 是读者，多字段
一致性由临界区复制与偶数 update sequence 保证。参数 applied 值唯一写入者是 SystemTask；
UART 和 OLED 只能 staging。

## 3. 三个独立硬件门禁

| 门禁 | 状态：passed/deferred/failed | 证据或延期原因 | 是否影响进入 Phase 2A |
| --- | --- | --- | --- |
| 物理 ADC 五键 | deferred | 无电阻梯形、引脚冻结和 ADC 实测分布；虚拟后端通过 | 不阻塞 2A 台架；Phase 3 前完成 |
| Flash 掉电保存 | deferred | scatter 未保留双槽，未执行擦写或破坏性注入 | RAM 调参可进入 2A；赛场前完成 |
| 硬件看门狗 | deferred | owner/window/halt/reset/执行器安全未板测，默认未启用 | 不作为 2A 安全依赖；真实输出前复审 |

当前 scatter 把完整 128 KiB Flash 交给程序，因此没有安全保留地址，Flash 子项未开始。
物理键硬件未准备；看门狗只完成 SDK API 调研，未启用。

## 4. 构建、烧录与调试链

| 项目 | 条件/版本 | 结果 | 证据摘要 |
| --- | --- | --- | --- |
| Keil FreeRTOS full rebuild | AC6 6.21 | passed | 0 Error / 0 Warning |
| Keil App full rebuild | AC6 6.21 | passed | 0 Error / 0 Warning |
| VSCode build task | 共用 `build_echo.ps1 -Mode All` | passed | 一键检查再次执行 0/0 |
| DAPLink flash + verify | OpenOCD，SWD 1 MHz | passed | fast verify 或逐字节 readback 回退通过 |
| Keil debug | GUI | not run | 未用 GUI 会话替代板测 |
| VSCode/OpenOCD debug | GNU Arm GDB + OpenOCD | passed | Watch、halt/resume、reset run 通过 |

最终程序 Code=53048、RO=2864、RW=28、ZI=15956。HEX 157400 B，SHA-256：

```text
1A205780BF54C948915A7D29E1DC6C240912C4A4FE4A95499D4B093FF25D3157
```

逐字节 Flash readback 二进制 SHA-256：

```text
0053B588B9293B34CBC9C8F00E27E283290D12ECC879F89BCFD71B0ACD3D2109
```

目标已 reset run。最终 120 秒和 10 分钟窗口没有保持调试器连接；每秒 Health 帧直接携带
栈、I2C、OLED 和 quiet 证据。

## 5. UI、参数和健康快照

### UI

```text
页面名称和数量：Overview、RTOS、COMM、DEVICE、PARAM，共 5 页
每页可见字段：身份/健康、任务时序与栈、通信与参数、OLED/I2C/安全门、metadata 参数
虚拟键单次消费：页面 0->1->2->3->4；9 个注入事件消费 9 次，请求变量回 0
页面循环/边界行为：左右循环；参数上下选择，编辑值按 metadata clamp
文本越界检查：统一 21 字符 line builder 截断，8 行固定布局
OLED 离线页面/遥测表现：OLED 不可见但 Control/Health 继续；恢复后自动 online
```

### 参数

```text
登记参数：KP、KI、KD、TARGET
类型/范围/步长/单位：float32；0..1000/0.1、0..1000/0.01、0..1000/0.01、-10000..10000/1 unit
合法参数 apply sequence：UI KP 1.0->1.1 sequence=1；defaults sequence=2；最终压力 50 次 sequence=5..54
非法/NaN/Inf/未知 ID：BAD ID/BAD VALUE，均不应用
BUSY/duplicate：8 帧 burst 为 2 APPLIED/6 BUSY；duplicate 返回原 sequence
恢复默认确认：LONG OK 进入 confirm，再按 OK 才 staging
OLED 与 UART 是否共用同一元数据表：yes
```

### 健康快照

```text
schema/version：magic=HLTH，version=1，build phase=0x010F
一致性方法：ServiceTask sole writer，critical copy + even update sequence
overall health：UNKNOWN/OK/DEGRADED/FAULT
active fault mask：当前活动；瞬态事件保持 2 秒可观察
first sticky fault：关键 first fault 不可被 clear recoverable 掩盖
更新时间戳/数据新鲜度：100 ms refresh，1 Hz Health publish，source/task age
OLED/遥测/Watch 是否读取同一语义：yes
```

## 6. 故障注入与恢复

| 注入条件 | 预期状态 | 实测状态 | 控制周期是否继续 | 是否恢复 | 证据 |
| --- | --- | --- | --- | --- | --- |
| OLED 启动前断开 | DEGRADED | not run（未带电改线） | not run | not run | 未热插拔 |
| OLED 运行中错误/离线模拟 | DEGRADED | DEGRADED，active/sticky=`0x1000` | yes，100 Hz | yes | `phase1f-oled-forced-offline.json` |
| UART 坏 CRC | 通信计数增加 | no ACK，错误计数增加 | yes | yes | stress script passed |
| UART 半帧超时 | 解析器重同步 | 下一合法帧 APPLIED | yes | yes | stress script passed |
| RX overflow | 丢弃并重同步 | not run | not run | not run | 未用非现实电气方法强制制造 |
| 非法参数 | reject | unknown/NaN/Inf/越界均 reject | yes | yes | stress script passed |
| 模拟 stale/关键 fault | FAULT/sticky | active/sticky=`0x4`，first=3 | yes，100 Hz | active 恢复 | `phase1f-critical-injected.json` |
| 清计数时故障仍活动 | 不清 active fault | active/sticky 保留；关键 first 永久保留 | yes | yes | Health JSON + Watch |

禁止为了测试带电热插拔未设计为热插拔的 I2C 模块。优先在断电状态改变接线，或使用明确的
软件故障注入开关。

本次遵守该要求，OLED 离线使用 `g_display_debug_force_offline`。恢复后的可恢复 sticky 通过
Overview LONG OK 清除；活动 OLED 故障不可清。关键故障解除并请求清除后 active=0，但
sticky=`0x4`、first fault=3 仍保留。

## 7. Phase 1E 回归对照

| 指标 | Phase 1E 基线 | Phase 1F 实测 | 结果 |
| --- | ---: | ---: | --- |
| UART baud | 230400 | 230400 | passed |
| 遥测频率 | 100 Hz | Control 100 Hz + Health 1 Hz | passed |
| 120 秒有效帧 | 12192 | 12194 Control + 121 Health | passed |
| CRC / sequence gap | 0 / 0 | 0 / 0 | passed |
| 控制周期 | 9999-10001 us | 9998-10002 us | passed，Health 插帧后无 deadline |
| deadline miss | 0 | 0 | passed |
| serial drop | 0 | 0 | passed |
| TX ring high-water | 280 B | 280 B | passed |
| quiet acquire/release | 419 / 419 | 271 / 271 | passed，条件/uptime 不同但完全配对 |
| 最大 quiet | 38529 us | 38529 us | passed |
| I2C error | 0 | 0 | passed |
| Display 最小剩余栈 | 166 words | 102 words | passed，仍高于 64-word 告警门槛 |
| RTOS fault | 0 | 0 | passed |

基线值用于回归参照，不要求 Phase 1F 的刷新次数或帧总数机械等于 Phase 1E；条件变化时必须
解释差异。错误、drop、deadline 和资源安全门不得退化。

## 8. 连续运行

```text
固件 commit/HEX：最终 Phase 1F 候选；HEX SHA-256 见上
启动方式：DAPLink program/verify/reset 后持续运行
调试器是否保持连接：no
中途是否暂停：no
运行时长：MCU timestamp 约 609.52 s
UART 总字节/有效控制帧/Health/ACK：3491448 B / 60953 / 610 / 0
CRC/gap/drop：0 / 0 / 0
period min/max：9998 / 10002 us
deadline miss/max overrun：0 / 0
quiet acquire/release/max/active：1466 / 1466 / 38529 us / 0
I2C success/error/recovery：234578 / 0 / 0
各任务 stack min-free words：180 / 128 / 151 / 102 / 104 / 104
heap current/min-ever：3064 / 3064 B
overall/active/sticky fault：OK / 0 / 0
意外复位：0；reset reason 保持 software reset 启动值
```

至少记录一次 120 秒联合回归和一次不少于 10 分钟的无断点运行；终验建议 30 分钟。

120 秒独立窗口：12194 Control、121 Health、12315 total，100/1 Hz，CRC/gap/drop/deadline 0，
period 9998-10002 us；quiet 271/271，I2C 43378/0，六栈 180/128/151/102/104/104。

本机 ignored 证据：

```text
tests/artifacts/phase1f-final-120s.json
tests/artifacts/phase1f-final-120s-control.csv
tests/artifacts/phase1f-final-10min.json
tests/artifacts/phase1f-final-10min-control.csv
tests/artifacts/phase1f-field-check-smoke/field-check.json
tests/artifacts/phase1f-final-clean-state.json
```

## 9. 结束状态与风险

```text
最终 commit：本 worklog 所在的 Phase 1F 提交
最终 annotated tag：phase-1f-operability-diagnostics
正式 main ahead/behind：安全合入后比 origin/main 领先 2
暂存区：提交前显式核对；不包含两项本地噪声
4 个受保护用户文件是否仍保留：yes；正式 keil XML 需语义加入两个源节点
stash/备份是否仍存在：yes，不 pop/drop/delete
未解决风险：三个 hardware gate deferred；Health frame 已达 128 B TX 上限
进入 Phase 2A 的结论：有条件允许独立台架设计；真实电机输出前重新执行安全门禁
```

附上小型证据文件路径或哈希；原始大串口流和截图不提交 Git。

最终 `reset run` 后 3 秒干净状态为 parameter sequence=0、parameter errors=0、Health OK、
active/sticky=0、OLED online、actuator output permitted=0。异常发热板继续禁用，COM4 不写成
永久事实，不自动 push。

这只是 Phase 1F 完成，不是最终工程完成。
