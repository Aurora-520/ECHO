# ECHO 当前交接

```yaml
handoff_schema: 1
updated_at: YYYY-MM-DDTHH:MM:SS+08:00
updated_by: human-or-agent
status: in_progress|blocked|ready_for_review|completed
```

## 1. 权威位置

```text
正式工程：
开发 worktree：
当前 branch：
HEAD：
基线 commit/tag：
origin/main：
```

## 2. 当前目标

- 本轮目标：
- 明确非目标：
- 验收标准：

## 3. 进度

### 已完成

- [ ]

### 进行中

- [ ]

### 未开始

- [ ]

## 4. Git 与文件所有权

```text
工作树状态：
暂存区：
未跟踪文件：
stash/备份：
```

| 文件 | 状态 | 修改目的 | 所有者/来源 | 接手注意事项 |
| --- | --- | --- | --- | --- |
| | | | | |

## 5. 实现与决定

| 项目 | 当前实现/决定 | 原因 | 尚未证明的假设 |
| --- | --- | --- | --- |
| | | | |

## 6. 验证证据

| 层级 | 命令/条件 | 结果 | 证据路径 | 时间 |
| --- | --- | --- | --- | --- |
| 代码审查 | | not run | | |
| 全量构建 | | not run | | |
| 烧录/校验 | | not run | | |
| 单板功能 | | not run | | |
| 故障注入 | | not run | | |
| 连续运行 | | not run | | |

## 7. 硬件现场状态

```text
主控板：unknown
供电：unknown
已连接外设：unknown
电机/云台/4S：unknown
机构是否架空：unknown
物理断电是否准备：unknown
DAPLink/串口占用：unknown
异常发热：unknown
```

未在现场重新确认前，禁止把历史接线描述当作当前物理事实。

## 8. 后台进程与运行状态

```text
OpenOCD/GDB/pyOCD：unknown
串口占用：unknown
开发服务器/采集脚本：unknown
目标 MCU：unknown
```

## 9. 问题、阻塞与风险

- 阻塞项：
- 已知风险：
- deferred 门禁：
- 失败后的恢复状态：

## 10. 下一步精确动作

1. 下一条只读核对：
2. 下一处允许修改的文件：
3. 下一条验证命令：
4. 需要用户确认的硬件动作：

## 11. 禁止操作

- 不自动 push。
- 不覆盖或还原未知 dirty 文件。
- 不删除 stash、备份、分支、tag 或 worktree。
- 不在未确认现场安全门时产生运动或高功率输出。
- 补充本任务特有禁止项：

## 12. 相关资料

- 当前 Phase 文档：
- worklog：
- 原始证据：
- 硬件手册：
- 关键源码：

## 13. 交接自检

- [ ] Git 状态与本文一致。
- [ ] 每个 dirty 文件的来源和目的已说明。
- [ ] 测试层级没有混写或夸大。
- [ ] 硬件状态已现场确认，或明确标为 unknown。
- [ ] 后台进程和 MCU 运行状态已核对。
- [ ] 下一步可以由新对话直接执行。
- [ ] 所有危险动作都列出确认门槛。
