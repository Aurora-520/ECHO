# Phase 2A 左轮无动力编码器实现日志

日期：2026-07-15（Asia/Shanghai）

## 目标

在不实现或驱动 AT8236 PWM 的前提下，先完成 370 左轮 GMR 编码器的硬件 QEI 代码、
安全门禁、诊断和可观测性，为随后无动力手转板测建立可编译基线。

## 起点

- worktree：`C:\Users\Auror\ECHO-phase2a-work`
- branch：`refs/heads/phase-2a-at8236-chassis-encoder`
- HEAD：`4b1a3dbef3c96b1b627c90d3c10566e3c6a0ec2f`
- 基线 tag：`refs/tags/phase-1f-operability-diagnostics`
- 工作树已有未提交的接手清理、field check 和实时交接修改，全部保留。

## 用户确认的硬件映射

- 左轮固定使用 D153B Motor A：`+ -> AOUT1/AO1`、`- -> AOUT2/AO2`、
  `A -> E1A`、`B -> E1B`。
- 右轮固定使用 D153B Motor B：`+ -> BOUT1/BO1`、`- -> BOUT2/BO2`、
  `A -> E2A`、`B -> E2B`。
- 开发电机为 12 V MG370 GMR、1:34.014，标称 500 PPR；最终 513X 尚未到货。

## 关键决定

- 左轮先用唯一原生 QEI 外设 TIMG8：E1A->PA29/PHA，E1B->PA30/PHB。
- 硬件按每个合法 AB Gray 状态变化计数，即 x4。左轮实测确认减速前高边沿率后，右轮方案
  修正为 PB6 上升沿 x1、PB7 方向输入，禁止软件 x4。
- SystemTask 100 Hz 扩展 16 位硬件计数为 64 位累计 count，现阶段不计算 rpm 或里程。
- QEI 非法跳变进入统一 SystemHealth，故障名 `ENC QEI`，出现后保持 sticky。
- D153B 编码器信号没有电平转换；PA29/PA30 不是 5 V tolerant，直连被门禁阻止。
- 左右轮是同级传感器；左轮只是第一只标定轮，安装负号只进入单一 sign 配置。

## 修改文件

- `config/ECHO.syscfg`
- `platform/generated/ti_msp_dl_config.c/.h`（仅由 SysConfig 生成）
- `bsp/include/bsp_encoder.h`
- `bsp/source/bsp_encoder.c`
- `app/main.c`
- `app/tasks/system_task.c`
- `module/service/telemetry.h`
- `module/service/system_health.h/.c`
- `keil/ECHO.uvprojx`
- Phase 2A、架构、状态、接线和实时交接文档

## 构建证据

| 项目 | 结果 |
| --- | --- |
| SysConfig | 0 error；1 条 STOP/STANDBY retention 提示 |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning |
| 程序尺寸 | Code=53992, RO=2968, RW=28, ZI=16164 |
| HEX SHA-256 | `2ED98801EF1506AD4AC85C20B88CCF6431D7DE3AD153CA1DB29E656DF764986F` |

SysConfig 连续运行报告 generated 文件 unchanged。当前固件不进入 STOP/STANDBY；进入这些
低功耗模式前仍必须实现 QEI retention save/restore。

## 板测结果

- 用户在场确认 D153B VM/4S 未上电，AO1/AO2 动力线断开绝缘，只手转左轮。
- 用户确认编码器信号为 3.3 V，E1A/E1B 已接 PA29/PA30。
- DAPLink program 完成；目标 CRC 算法超时后，脚本回读 56,992 B 并验证 SHA-256 一致，
  随后 `reset run`。
- 5 秒启动基线：Control 512、Health 5，100 Hz/1 Hz；CRC/gap/drop/deadline/QEI fault 为 0。
- 两次向前粗略一圈净计数 `+77,523`、`+76,749`；第二次首个连续段 `+69,207`。
- 向后粗略一圈净计数 `-74,323`，误正向 `+75`；向前为正，左轮 sign 冻结为 `+1`。
- 120 秒静止：12,196 个 Control 样本的 encoder delta 全为 0，净漂移 0。
- 120 秒 Health：122 帧，CRC/gap/drop/deadline/I2C/active/sticky/QEI fault 全为 0。
- 全程 actuator output permitted=0；没有连接或驱动 AT8236、电机、VM 或 4S。

## 待确认与下一步

1. 实现 PB6/PB7 右轮软件 x1 解码，并做同轴硬件 x4/软件 x1 负载和漏计数对照。
2. 速度/里程闭环前以多圈平均精确冻结 370 输出轴 CPR；当前 provisional 值为 68,028。
3. 完成 AT8236 默认零输出、coast/brake、方向和命令超时后，再申请单电机点动许可。

本日志证明左轮无动力编码器子阶段通过，不证明右编码器、AT8236、电机控制、Phase 2A
或最终工程完成。
