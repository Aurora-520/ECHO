# 2026-07-15 ECHO 模块调试手册回溯补录

性质：retrospective

## 目标与范围

- 从现有文件系统中的 Phase 1A-1F、Phase 2A 和 MPU6050 spike 记录提炼长期调试方法。
- 建立每一个可独立验收小模块必须填写的统一调试模板和完成门禁。
- 不修改驱动、任务、SysConfig、构建产物或当前硬件状态。
- 不重新解释或补造没有原始证据的 Phase 1D 验收数字。

## 开始状态

```text
唯一正式工程：E:\ECHO
文档实施工作树：C:\Users\Auror\ECHO-mpu6050-spike-work
branch：refs/heads/codex/mpu6050-hardware-spike
基线：4b1a3db / refs/tags/phase-1f-operability-diagnostics
正式 Phase 2A 工作树：C:\Users\Auror\ECHO-phase2a-work，保持 dirty、未修改
```

开始时 MPU spike 已有未提交的 IMU、I2C、UART、任务、工具和文档修改。本次只增加或修改独立
长期文档，没有还原、覆盖、暂存或提交既有修改。

## 读取的主要来源

- `AGENTS.md`、`docs/PROJECT_STATUS.md`、`docs/worklogs/README.md`
- Phase 1A baseline 与硬件验证
- Phase 1B FreeRTOS skeleton
- Phase 1C clock/timebase
- Phase 1E OLED/UI 与 UART quiet window
- Phase 1F Phase 文档、learning 和最终 worklog
- Phase 2A 当前交接、Phase 文档、左右编码器、AT8236 logic PWM、Motor Profile worklog
- MPU6050/MPU6500-compatible spike 文档、worklog、实时交接和当前任务记录

## 修改

- 新增 `docs/learning/DEBUGGING_PLAYBOOK.md`：
  - 定义小模块范围、证据等级、通用排障流程和完成规则；
  - 回溯补录 P1A、P1B、P1C、P1D、P1E、P1F、Phase 2A 和 MPU spike；
  - 明确传感器、BSP、service、task、telemetry 和主机解析必须分层判断；
  - 明确 A/B 一次只改变一个主要变量；
  - 保留 not run、deferred、证据限制和恢复方法。
- 新增 `docs/learning/MODULE_DEBUG_RECORD_TEMPLATE.md`：统一 18 个记录章节和完成 checklist。
- 新增 `docs/learning/README.md`：建立 learning 索引及文档流转关系。
- 更新 `AGENTS.md`：每个小模块未更新调试手册时禁止报告完成。
- 更新 `docs/worklogs/README.md`：worklog 完成不再自动等于小模块完成。
- 新增本 worklog，记录回溯来源、范围和验证。

## 关键决定

1. 不建立一份按时间顺序的聊天流水账；手册按模块 ID 和问题模式组织。
2. 一次 worklog 保存“当时发生了什么”，调试手册保存“以后如何识别和解决”。
3. 小模块包括 BSP、device、service、estimator/controller、任务、协议、工具、UI 页面、单侧硬件
   通道，不能只在整个 Phase 完成时才补文档。
4. 核心适用项仍为 `not run/failed` 时，只能报告相应证据层级；明确独立门禁才允许写 deferred。
5. Phase 1D 没有保存原始串口数字，因此只保留已验收事实和方法，不借用后续 Phase 数字补写。
6. MPU 静止统计记录为当前 spike 观察，动态、故障恢复和长时间运行仍保持未完成。

## 硬件状态

- 本次没有烧录、复位、改线或驱动硬件。
- 没有连接或改变 VM/4S、电机、云台和高功率输出状态。
- 当前 MPU 和 Phase 2A 的硬件结论仅从既有记录回溯，不新增板测结论。

## 验证

- 新增文件存在性检查：passed。
- 引用的模板、手册、Phase 1F learning 和 worklog 规则路径检查：passed。
- 强制门禁文案在 `AGENTS.md`、worklog 规则和调试手册中均可检索：passed。
- 针对本次文档文件执行 `git diff --check`：passed。
- 构建、烧录、板测：not applicable，本次没有源码或配置语义修改。

## 问题与判断

- 正式 `E:\ECHO` 是长期权威来源，但当前 MPU spike 尚未验收，文档先保存在 spike 工作树；后续应
  按阶段规则逐文件语义合入正式工程，禁止整目录覆盖。
- Phase 2A 工作树包含大量进行中 dirty 修改。本次只读其文档，没有修改或暂存任何 Phase 2A 文件。
- 当前主线程仍可能更新 MPU 实测结论；若新证据改变 UART CRC 或 IMU 判断，应在手册 IMU-S01
  增加勘误或更新 open 项，不能把当前 probable 判断改写为既定根因。

## 风险与下一步

- 手册是 living document，后续每个小模块必须持续填写，不能在阶段末一次性补写。
- 当前 `CURRENT_WORKFLOW.md`、实时交接和 MPU worklog 的最新实测数字仍由正在进行的 spike 任务
  负责同步，本次不与主线程争用这些文件。
- 阶段验收时必须把本制度语义合入正式 `E:\ECHO` 的 `AGENTS.md` 和 `docs/learning`。

## 结束状态

- commit/tag：未创建。
- staged：未执行。
- push：no。
- 驱动、SysConfig、构建产物和正式工程：未修改。
