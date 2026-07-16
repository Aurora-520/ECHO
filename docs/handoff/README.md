# ECHO 实时交接

本目录保证 ECHO 在开发的任意中间状态都能由新对话或新维护者接手，而不依赖聊天记录。
交接分为 Codex 实时交接和队友操作手册两条入口，不要混用它们的职责。

## 文件职责

- `CURRENT_HANDOFF.md`：给 Codex/高级维护者使用的唯一实时交接入口，描述此刻的开发状态。
- `TEAMMATE_HANDOFF.md`：给单片机基础较少的队友使用，说明架构、环境、接线、构建、DAPLink
  调试、Codex 接手方法和禁止操作。
- `HANDOFF_TEMPLATE.md`：新阶段或重建实时交接时使用的完整模板。
- `history/`：阶段完成或重要里程碑后的只读交接归档。

`docs/PROJECT_STATUS.md` 记录最近已确认的阶段基线；`CURRENT_HANDOFF.md` 记录基线之后的
进行中工作、未提交修改、失败和硬件现场状态。两者必须同时读取。

队友开始操作时先读 `TEAMMATE_HANDOFF.md`，但当前 branch、dirty 文件、硬件现场状态和
下一步仍必须以 `CURRENT_HANDOFF.md` 为准。队友手册是长期方法说明，不替代实时状态。

## 新对话读取顺序

```text
AGENTS.md
-> docs/handoff/CURRENT_HANDOFF.md
-> docs/PROJECT_STATUS.md
-> 当前 Phase 文档
-> 当前任务相关源码和硬件资料
```

然后执行：

```powershell
git status --short --branch
git diff
git diff --cached
git worktree list
```

涉及硬件时还要核对调试器、串口、供电、接线和后台进程。实时检查与交接冲突时，不静默
继续；先把真实状态写回 `CURRENT_HANDOFF.md`。

## 必须更新时间

以下任一事件发生后都要更新实时交接：

1. 开始或改变子任务范围。
2. 修改文件或改变 Git 状态。
3. 完成构建、烧录、板测、故障注入或连续运行。
4. 出现错误、阻塞、重要判断或硬件状态变化。
5. 暂停、结束会话、切换对话或交给其他维护者。
6. 提交、打标签、合入正式工程或创建下一阶段 worktree。

更新必须发生在事实产生后，不能等到阶段结束后凭记忆补写。

## 状态词

- `passed`：有保存的对应层级证据。
- `failed`：已执行且失败，必须记录失败层和恢复状态。
- `not run`：尚未执行。
- `in progress`：已开始但未形成可验收结论。
- `blocked`：缺少输入、权限、硬件或外部决定。
- `deferred`：明确延期并记录关闭门槛。
- `unknown`：当前无法可靠确认，接手者必须先核对。

不得把代码审查写成构建通过，不得把构建通过写成板测通过。

## 归档规则

阶段提交和 annotated tag 创建前，将最终实时交接复制为：

```text
docs/handoff/history/YYYY-MM-DD_phase-name.md
```

归档文件写入最终 commit/tag、验收证据和遗留风险。创建下一阶段后，用模板重建
`CURRENT_HANDOFF.md`，不得让旧阶段的进行中状态继续冒充当前状态。

## 并行对话

同一 worktree 同时存在多个对话时，任何对话修改前都必须重读 `git status`、相关 diff 和
实时交接。不得覆盖不属于自己的 dirty 文件。发现其他对话正在修改同一文件时，暂停该文件
的写入，只做非重叠工作或等待所有权明确。
