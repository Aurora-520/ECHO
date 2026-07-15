# ECHO Learning 文档索引

`docs/learning` 保存跨阶段仍然有效的原理、排障方法和模块调试结论，不保存完整聊天记录，也不
替代一次性 worklog。

## 必读入口

- `DEBUGGING_PLAYBOOK.md`：统一模块调试手册、历史经验和模块状态索引。
- `INTEGRATION_PLAYBOOK.md`：引脚迁移、资源账本、模块组合合同和防退化回归门禁。
- `MODULE_DEBUG_RECORD_TEMPLATE.md`：每一个小模块完成前必须填写的模板。
- `PHASE1F_OPERABILITY_DIAGNOSTICS.md`：统一健康、参数、混合遥测和连续运行方法。

开始任务时只读取当前模块相关条目，不要求每次加载整本手册。发现新的可复用问题时，先在当次
worklog 保存事实，再更新调试手册；发现旧结论错误时增加勘误，不静默改写历史证据。

模块条目和 worklog 的关系：

```text
原始数据/仪器观察
-> 当次 worklog：记录当时发生了什么
-> DEBUGGING_PLAYBOOK：提炼以后如何识别、排查、修复和预防
-> INTEGRATION_PLAYBOOK：登记资源、引脚迁移、组合冲突、降级和回归
-> PROJECT_STATUS/实时交接：只保留当前状态和下一步
```
