# 2026-07-15 Phase 2A 接手清理

性质：normal

## 目标与范围

- 修正 Phase 1F 合入后遗留的工作流程和架构状态文档。
- 加固 Phase 1F 一键检查中的累计 I2C 与 UART/OLED quiet-window 门禁。
- 固化 Phase 1F 同名 branch/tag 的完整 ref 用法。
- 不实现 AT8236、电机、编码器或任何运动输出。

## 开始状态

- 正式工程：`E:\ECHO`，`main=4b1a3db`，比 `origin/main` 领先 2，正式工程保持只读。
- 开发工作树：`C:\Users\Auror\ECHO-phase2a-work`。
- 分支：`phase-2a-at8236-chassis-encoder`，开始 HEAD `4b1a3db`。
- 开始时工作树和暂存区为空。
- 正式工程 4 个用户文件、stash 和备份不在本次修改范围内。

## 修改

- `docs/CURRENT_WORKFLOW.md`：记录已创建的 Phase 2A branch/worktree 和完整 ref 命令。
- `docs/ARCHITECTURE_BOUNDARIES.md`：将目录状态和已建立边界更新到 Phase 1F/Phase 2A 起点。
- `tools/phase1f_field_check.ps1`：默认验收增加累计 I2C error、OLED online、quiet 配对、
  quiet active、最大 quiet 时长以及 active/sticky 干净状态检查。
- `AGENTS.md`：禁止用有歧义的 Phase 1F 短 ref。
- `docs/PROJECT_STATUS.md`：记录 Phase 2A 已创建但尚未实现或驱动电机。

`-AllowDegraded` 仍可用于明确的降级检查；quiet acquire/release 配对、quiet active 和最大时长
不因该开关放宽。默认 Phase 1F 验收要求累计 I2C error 为 0、OLED online 且健康状态干净。

## 硬件状态

- 未连接、配置或驱动 AT8236、电机、编码器、云台或 4S 高功率输出。
- 未烧录、复位或改变当前板上固件。

## 验证

- PowerShell AST 解析：passed，`phase1f_field_check.ps1` 无语法错误。
- 现有 telemetry capture fixture：passed，200 Control @ 100 Hz、2 Health @ 1 Hz，
  CRC/gap/duplicate/out-of-order/deadline 均为 0。
- Phase 1F 保存的干净 120 秒 JSON 门禁复核：passed。
- 负例门禁：累计 I2C error、quiet acquire/release 不配对、quiet active 三项均被拒绝。
- 完整 refs：Phase 1F branch、Phase 1F tag 和 Phase 2A branch 均解析到 `4b1a3db`。
- `git diff --check`：passed。
- 硬件构建、烧录和板测：未执行；本次仅文档和工具门禁维护。

## 问题与判断

- 同名 branch/tag 本身不删除或重命名；完整 ref 足以消除脚本和人工核对中的歧义。
- 此清理不改变 `4b1a3db` 固件行为，也不重新定义 Phase 1F 的历史验收数字。
- 结束时只读核对正式工程，除原 4 个受保护文件外还显示
  `platform/generated/ti_msp_dl_config.c` dirty；它属于本次范围外的 SysConfig 本地状态，
  未还原、未暂存、未复制到 Phase 2A 修改中。

## 风险与下一步

- `phase1f_field_check.ps1` 的新门禁仍需在下一次低功率现场回归中走完整硬件路径。
- 下一步冻结 Phase 2A 硬件事实、引脚资源、安全状态机和验收标准，再开始独立实现。

## 结束状态

- 最终 commit/tag：未创建；Phase 2A 尚未验收。
- push：no。
- 正式工程、用户改动、generated 本地状态、stash 和备份：未修改。
