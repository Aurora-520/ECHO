# 2026-07-15 引脚可迁移与模块组合防退化规则

性质：normal

## 目标与范围

- 建立物理引脚集中、编译时可迁移的长期规则。
- 建立模块资源账本、集成合同、两两组合和上一阶段回归门禁。
- 防止单模块各自通过但最终组合时出现 pin、Timer、DMA、IRQ、任务、协议或安全冲突。
- 本次只修改文档，不重构当前 BSP、SysConfig 或驱动代码。

## 开始状态

```text
实施工作树：C:\Users\Auror\ECHO-mpu6050-spike-work
branch：refs/heads/codex/mpu6050-hardware-spike
基线：4b1a3db / refs/tags/phase-1f-operability-diagnostics
staged：空
```

当前 spike 和正式 Phase 2A 工作树均有进行中 dirty 修改。本次只读审查相关 BSP 和文档，没有
修改 Phase 2A 文件、当前驱动、SysConfig/generated 或硬件状态。

## 审查结果

### 已具备的可迁移基础

- SSD1306、MPU device、Service 和 App 不直接知道 PA0/PA1，只调用 `bsp_i2c`。
- `bsp_i2c` 使用 SysConfig 生成的 instance、PORT、PIN 和 IOMUX 宏。
- 左右编码器 BSP 使用 `LEFT_ENCODER_QEI_INST`、`GPIO_RIGHT_ENCODER_*` 等生成宏。
- Motor Profile 与物理引脚解耦，MG370/513X 共用同一套 BSP 和引脚。

### 当前技术债

- 共享 I2C 生成实例仍以 `OLED_I2C_*` 命名，语义已经不再只属于 OLED。
- UART TX DMA 编译时固定 physical channel 3，迁移需要独立 errata/中断验证。
- 右编码器直接拥有 `GROUP1_IRQHandler`，未来同组 GPIO 中断需要 BSP 统一分发。
- 尚无集中 board resource version 和完整资源账本遥测。
- Phase 2A 与 MPU spike 尚未正式组合，单模块证据不能替代组合验收。

## 修改

- 新增 `docs/learning/INTEGRATION_PLAYBOOK.md`：
  - 规定编译时 pin 迁移模型和换脚验收步骤；
  - 回填当前 MCLK、TIMG12、UART/DMA、I2C、OLED、MPU、QEI、GPIO IRQ、PWM、任务和协议资源；
  - 定义模块集成合同、初始化顺序、降级、安全和资源预算；
  - 定义单模块、两两组合、阶段组合、上一阶段回归和连续运行阶梯；
  - 明确 `integration_ready` 门禁和禁止的拼凑式实现。
- 更新 `AGENTS.md` 和 `ENGINEERING_RED_LINES.md`：上层禁止物理 pin/Timer/DMA/IOMUX，换脚集中
  配置，不支持的组合必须显式失败。
- 更新模块调试模板、调试手册、learning 索引和 worklog 规则：每个小模块必须同时更新调试和
  集成两份长期记录。

## 关键决定

1. 提供“编译时可迁移”，不提供“运行时任意换引脚”。
2. SysConfig pin mux 能力和 MCU 数据手册是候选引脚的硬边界。
3. 如果新引脚需要更换 peripheral backend，上层 API 保持不变，但 BSP、IRQ、DMA、时序和板测
   重新验收。
4. 单模块 `completed` 与组合 `integration_ready` 分开报告。
5. 每个模块至少完成一个共享资源邻居的两两回归，并重跑当前阶段和上一阶段基线。

## 硬件状态

- 未烧录、复位、改线或驱动任何硬件。
- 未改变电机、VM/4S、OLED、MPU 或编码器现场状态。

## 验证

- 文档引用和新文件存在性：passed。
- `AGENTS.md`、模块模板和集成手册中的强制门禁文案检索：passed。
- 本次文档尾随空格检查：passed。
- tracked 文档 `git diff --check`：passed；仅显示既有 LF/CRLF 转换提示。
- 暂存区：空。
- 源码构建/烧录/板测：not applicable，本次没有修改源码或配置。

## 风险与下一步

- 当前代码仍需在各自正式 Phase 中逐项偿还已记录技术债，不能因新增规则就声称完全可换脚。
- 首次实际换脚必须执行 SysConfig、0/0 构建、idle level、单模块、两两组合和基线回归。
- 正式阶段验收时将本规则逐文件语义合入 `E:\ECHO`，禁止整目录覆盖。

## 结束状态

- commit/tag：未创建。
- staged/push：no。
- 代码、SysConfig、Phase 2A dirty 文件和正式工程：未修改。
