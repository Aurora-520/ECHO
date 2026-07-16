# 2026-07-15 ECHO 实时交接机制

性质：normal

## 目标

建立不依赖聊天记忆的长期交接入口，使新对话能在阶段中途、失败后、硬件测试间隙或阶段
切换时恢复真实工程状态。

## 修改

- `AGENTS.md`：把实时交接加入新任务必读顺序，并规定强制更新时间和字段。
- `docs/handoff/README.md`：说明交接文件职责、状态词、生命周期和并行对话规则。
- `docs/handoff/HANDOFF_TEMPLATE.md`：提供 Git、进度、验证、硬件、进程、风险和下一步模板。
- `docs/handoff/CURRENT_HANDOFF.md`：建立 Phase 2A 起点的首份实时交接。
- `docs/handoff/history/README.md`：规定阶段和里程碑归档方式。

未修改主对话正在维护的架构、工作流程、项目状态、field check 和接手清理 worklog 内容。

## 关键决定

- `PROJECT_STATUS.md` 继续表示最近已确认阶段基线。
- `CURRENT_HANDOFF.md` 表示基线之后的实时进行中状态，包括 dirty 文件和失败。
- 新对话必须先读实时交接，再用 Git、进程和硬件现场检查验证，不能只相信文档时间戳。
- 交接文件本身允许在阶段中保持 dirty；不得把它当作构建噪声还原。
- 同一 worktree 多对话并行时，发现同文件所有权冲突必须停止写入。

## 验证

- 交接引用路径存在性：passed。
- `CURRENT_HANDOFF.md` 必填结构检查：passed。
- `git diff --check`：passed；只有既有 Windows 行尾提示，没有空白错误。
- 构建、烧录、板测：not run，本次仅文档与工作流程机制。

## 硬件状态

- 未配置、连接或驱动 AT8236、电机、编码器、云台或 4S 高功率输出。
- 未烧录、复位或改变板上固件。
- 当前现场接线和供电仍需在下一次硬件动作前重新确认。

## 下一步

- 当前接手清理和实时交接机制由主任务统一审查，不自动提交或 push。
- 后续每次实质工作按 `AGENTS.md` 更新 `CURRENT_HANDOFF.md`。
- Phase 2A 设计冻结后更新实时交接，并在阶段验收完成时生成 history 归档。
