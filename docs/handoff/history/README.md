# ECHO 交接归档

本目录保存阶段结束或重要里程碑时的实时交接快照。归档文件只记录当时已经形成证据的事实，
不得在后续阶段静默改写历史结果。

命名格式：

```text
YYYY-MM-DD_phase-name.md
```

归档前应确认：

- commit、annotated tag、分支和正式合入状态准确；
- 构建、烧录、板测和连续运行证据路径存在；
- deferred、not run 和失败项没有被改写为 passed；
- dirty 文件、stash、备份和硬件最终状态已记录；
- `CURRENT_HANDOFF.md` 随后为下一阶段重新建立。
