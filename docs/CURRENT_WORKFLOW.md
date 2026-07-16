# ECHO 当前工作流程

本文说明 ECHO 从已验收 Phase 1F 到整车最终验收的固定工作流程。工程状态、硬件条件或用户
指令变化后，应先更新本文或 `PROJECT_STATUS.md`，不得依赖聊天记忆继续操作。

## 1. 权威位置

- 唯一正式工程：`E:\ECHO`
- Phase 1F 验收工作树：`C:\Users\Auror\ECHO-phase1f-work`（归档前保留）
- 文档整理来源：`C:\Users\Auror\ECHO-docs-staging-20260715`（已逐文件语义选入）
- Phase 2A 分支：`phase-2a-at8236-chassis-encoder`
- Phase 2A 工作树：`C:\Users\Auror\ECHO-phase2a-work`
- Phase 2A 从正式 Phase 1F 基线 `4b1a3db` 创建，当前尚未接入或驱动电机。
- 不新建仓库或复制工程来代替 `E:\ECHO`。

## 2. 长期阶段顺序

阶段必须依次推进，不能在同一分支提前混入后续硬件：

```text
Phase 1F 赛场可操作性、健康诊断、参数/UI 和持久化门禁
-> Phase 2A AT8236、底盘电机和 GMR 编码器
-> Phase 2B 底盘 ICM42688、速度/航向/位置控制
-> Phase 2C X42S/串口无刷云台、云台 IMU 和控制频率验证
-> Phase 2D 树莓派 4B、单目相机/MaxiCam 通信
-> Phase 3 组合动作、任务状态机、OLED 任务菜单
-> Phase 4 整车压力测试、故障演练和赛场验收
```

只有 Phase 1F 至 Phase 4 全部验收通过，才能报告“最终工程完成”。中间只能报告对应阶段
或子项的真实状态。

## 3. 每个阶段的固定闭环

每个阶段都按以下顺序完成：

1. 设计边界：确认范围、非目标、接口所有权、实时约束和验收标准。
2. 独立实现：只在当前阶段分支/worktree 修改，不混入下一阶段功能。
3. 全量构建：FreeRTOS 与 App 均达到 0 Error / 0 Warning。
4. 安全烧录：只烧录当前应用，执行校验和复位，不擅自全片擦除或改配置区。
5. 单模块板测：先验证一个模块，再接入组合运行。
6. 故障测试：验证断开、超时、坏帧、队列满、非法参数和恢复路径。
7. 连续运行：按 Phase 文档完成规定时长的无断点运行并保存数字。
8. 文档：更新状态、Phase 文档、worklog、README 和必要的学习文档。
9. 提交与标签：验收全部通过后才创建阶段 commit 和 annotated tag。
10. 合入正式工程：保护用户文件，优先 fast-forward，结构化文件语义合并。
11. 创建下一阶段：只能从 `E:\ECHO` 已验收的新基线创建新分支/worktree。

任一环节失败时停在对应层修复并重新验证，不能用代码审查代替构建，也不能用构建代替
板测或连续运行。

## 4. Phase 1F 已完成范围

Phase 1F 已按以下依赖顺序完成核心软件和低功率板测：

1. 统一 `SystemHealthSnapshot` 和稳定故障目录，不新增 HealthTask。
2. ServiceTask 作为唯一健康快照写入者。
3. OLED、1 Hz Health 遥测和 debugger Watch 读取同一健康语义。
4. 建立参数元数据表，让 UART 与 OLED 共用范围、校验和周期边界应用。
5. OLED 提供 Overview、RTOS、COMM、DEVICE、PARAM 五页。
6. 采集工具分别统计 100 Hz Control 帧与 1 Hz Health 帧。
7. 完成故障注入、120 秒联合回归和至少 10 分钟无断点运行。
8. Flash、物理 ADC 五键和硬件看门狗分别记录为 `deferred`，没有伪报通过。

Phase 1F 没有接入电机、编码器、IMU、云台、树莓派或真实运动 PID。下一步只能从正式
Phase 1F 标签创建 Phase 2A，并继续遵守逐阶段闭环。

## 5. 开始一次工作会话

开始修改前依次执行：

```powershell
git status --short --branch
git diff
git diff --cached
git worktree list
```

然后读取：

1. `AGENTS.md`
2. `docs/PROJECT_STATUS.md`
3. 当前 Phase 文档
4. 本次任务直接相关的源码、硬件资料和工具

先识别用户已有改动和构建噪声，再决定修改范围。禁止把未识别的 dirty 文件自动还原或
暂存。

## 6. 构建与测试层级

证据必须明确标注所在层级：

```text
代码审查
-> 编译通过
-> 烧录及校验通过
-> 单板功能实测通过
-> 故障注入与恢复通过
-> 规定时长连续运行通过
```

每个子阶段先编译，再烧录和板测。高频任务不得等待 UART、OLED、Flash 或无界外设轮询；
所有等待必须有超时，队列和 ring 必须有 drop、overflow 或 high-water 诊断。

## 7. 硬件安全门

涉及电机、云台、轮组、4S 电池或其他高功率输出前，必须取得用户明确确认且用户人在现场。
动作测试前必须：

- 架空机构或轮组。
- 默认输出为零。
- 准备物理断电。
- 确认板卡没有异常发热。
- 禁止把 DAPLink 3V3 与另一已供电 3.3 V 电源并联。

普通低功率 UART/OLED/SWD 测试也要先确认接线和供电条件。没有明确许可不得执行破坏性
Flash 注入、全片擦除或配置区修改。

## 8. 文档如何进入正式工程

一次普通工作会话结束时，先在当前阶段工作树或文档整理区更新真实状态，不立即覆盖
`E:\ECHO`。阶段验收完成后执行：

1. 根据保存的构建、烧录、串口和 Watch 证据填写新的 Phase worklog。
2. 更新 `docs/PROJECT_STATUS.md`、当前 Phase 文档和 README 索引。
3. 必要时更新 `docs/learning`，记录可复用的原理和排障方法。
4. 将文档逐文件语义合入当前阶段分支，禁止整目录盲目覆盖。
5. 检查文档中的数字、链接、commit、tag 和 deferred 项与真实状态一致。
6. 阶段验收通过后创建 commit 和 annotated tag。
7. 把阶段提交和文档安全合入 `E:\ECHO` 的正式 `main`。

因此，“每次完成后放到 E 盘”指阶段完成并验收后的正式归档。中间会话和未通过门禁的
结果先保留在阶段工作树/文档整理区，不能提前污染正式基线。

## 9. Git 安全

- 不自动 push。
- 不使用 `git add .`、`git add -A` 或 `git commit -am`。
- 只显式暂存本阶段文件，并检查暂存文件清单和 `git diff --cached --check`。
- 不删除 stash、备份、分支、标签或 worktree。
- 不使用 `git reset --hard` 或 `git checkout -- <file>` 破坏用户改动。
- 正式工程的受保护用户文件必须语义保留。
- Phase 1F 的 branch 与 annotated tag 同名。脚本和人工核对必须使用完整 ref，禁止依赖
  会产生 ambiguous 警告的短名称：

```powershell
git rev-parse refs/heads/phase-1f-operability-diagnostics
git rev-list -n 1 refs/tags/phase-1f-operability-diagnostics
git rev-parse refs/heads/phase-2a-at8236-chassis-encoder
```

## 10. 语言与报告

与用户沟通、工程说明、阶段报告、验收结论和 worklog 默认使用中文。代码标识符、API、
协议字段、命令、路径和工具原始输出保留原文。没有证据的项目写“未执行”或 `deferred`，
不得补写、猜测或借用其他阶段的数字。
