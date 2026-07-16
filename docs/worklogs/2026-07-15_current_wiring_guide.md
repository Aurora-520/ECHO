# ECHO 当前功能接线指南工作日志

日期：2026-07-15（Asia/Shanghai）

## 目标

建立一份可持续更新的 ECHO 总接线指南，覆盖当前代码已有功能，而不只记录 AT8236 电机
端子。后续 Phase 2B 至 Phase 4 的接线继续追加到同一入口。

## 信息来源

- `config/ECHO.syscfg` 与 `platform/generated/ti_msp_dl_config.h`：当前已生成的 OLED、UART、
  LED 引脚，以及正在配置的左 QEI 引脚。
- `docs/HARDWARE_TOOLCHAIN_SOURCES.md`：天猛星、DAPLink SWD 和串口已验证事实。
- `docs/PHASE1E_OLED_UI.md`、Phase 1F 文档和 worklog：OLED、串口、UI 与低功率板测证据。
- `docs/phases/PHASE2A_AT8236_CHASSIS_ENCODER.md` 和当前 BSP 草案：编码器电平门禁、当前
  引脚规划和尚未实现项。
- 用户当前确认：左轮使用 AT8236 A 通道，右轮使用 B 通道；电机及 AB 相端子映射。

## 修改

- 新增 `docs/hardware/ECHO_WIRING_GUIDE.md`，作为长期唯一接线入口。
- 汇总已验收 SWD、UART1、SSD1306 OLED 和 PB22 板载 LED 接线。
- 明确物理五键未实现，当前 OLED UI 只使用 debugger 虚拟按键，不虚构按键引脚。
- 记录左轮 `AO1/AO2/E1A/E1B` 和右轮 `BO1/BO2/E2A/E2B` 的用户确认映射。
- 明确左 QEI 的 `PA29/PA30` 仅为当前未验收代码配置，D153B 编码器 5 V 信号在电平转换
  冻结前禁止直连 MCU。
- 把右编码器和四路电机 PWM 引脚标为设计预留，当前固件不会驱动。
- 为 Phase 2B、2C、2D 和后续整车功能保留更新规则，不提前分配接线。

## 硬件与验证状态

- 本次只新增文档，没有修改源码、SysConfig、generated 文件或 Keil 工程。
- 没有构建、烧录、复位、物理接线、上电或驱动电机。
- Phase 1F 已验收接线事实继承其保存证据；Phase 2A 代码和接线仍为进行中/未验收。
- 实际现场接线、供电和人员状态为 `unknown`，任何硬件动作前必须重新确认。

## 检查

- Markdown 结构、路径与术语一致性：通过；所引用的工程文档均存在。
- `git diff --check -- docs/hardware/ECHO_WIRING_GUIDE.md docs/worklogs/2026-07-15_current_wiring_guide.md`：
  通过；只有 Windows 行尾提示，无空白错误。
- 构建/烧录/板测：`not run`，纯文档修改不以其他层级伪报通过。
