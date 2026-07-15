# ECHO MPU6050 Spike 当前交接

```yaml
handoff_schema: 1
updated_at: 2026-07-15T23:57:50+08:00
updated_by: Codex
status: paused
```

## 1. 位置与边界

```text
正式工程：E:\ECHO
正式已验收基线：4b1a3db / refs/tags/phase-1f-operability-diagnostics
正式 Phase 2A 工作树：C:\Users\Auror\ECHO-phase2a-work（保持 dirty，不得覆盖）
本 spike 工作树：C:\Users\Auror\ECHO-mpu6050-spike-work
branch：refs/heads/codex/mpu6050-hardware-spike
HEAD：4b1a3db
```

本分支仅用于备用 MPU6050 低功率硬件验证，不是正式 Phase 2B。Phase 2A 电机/编码器工作暂停但
未撤销；本分支不得直接合入正式 main，不创建阶段 tag，不 push。

用户于 2026-07-15 明确暂停后续算法补偿、三轴 Motion、六面标定和 ICM42688 工作，下一活动
工作树恢复为 `C:\Users\Auror\ECHO-phase2a-work`。本 spike 只创建非阶段 checkpoint 并推送
独立 GitHub 分支，不合入正式 main。

## 2. 已完成

- 确认使用 `PA1=SCL`、`PA0=SDA`，I2C0 400 kHz，与 OLED 共线。
- BSP 新增有界硬件寄存器写后读事务，两个事务都有明确 STOP，最大读取 16 字节。
- 新增 `0x68` 地址探测、复位、配置校验和 14 字节六轴突发读取；按 `WHO_AM_I` 选择
  `MPU6050` 或 `MPU6500-COMPAT` Profile，共用同一设备驱动和 ImuService。
- 新增 ImuService 状态机、300 点连续静止偏置、原始/补偿/低延迟滤波快照。
- ServiceTask 唯一采样写入；SystemTask 只读快照并发布 100 Hz 数据。
- Health 新增 IMU offline/stale，sensor source 指向 ImuService/MPU6050。
- UART DMA 完成语义按 TI 示例改为 `DMA_DONE_TX` 后继续等待 `EOT_DONE`；DMA 启动统一由
  2 ms ServiceTask 执行。
- IMU 的 I2C 访问使用非阻塞短优先 quiet window，与 OLED 共用已验收的 UART/I2C 并发隔离机制。
- 新增静止/转动现场检查工具，并保存原始串口字节用于 CRC 根因分析。
- FreeRTOS/App full rebuild 均为 0 Error / 0 Warning。
- 已烧录并完成 20 秒候选回归：Control/Health 100/1 Hz，CRC/gap/deadline/I2C error、
  active/sticky、quiet mismatch 全部为 0。
- 第一块 `WHO_AM_I=0x70` 已完成 600 秒静止诊断：Control/Health `60961/610`，100/1 Hz，
  CRC/gap/deadline/drop、I2C error、active/sticky 和 quiet mismatch 全部为 0。
- 第二块 `WHO_AM_I=0x68` 已完成真实 600 秒静止诊断：主机墙钟 `607.127 s`，设备数据跨度
  `609.600 s`；Control/Health `60961/609`，100/1 Hz，全部通信、实时性和 Health 门禁为 0。
- 两块 600 秒原始数据已复制到仓库外
  `C:\Users\Auror\ECHO-mpu6050-spike-data\2026-07-15_static-600s`；包含 raw/CSV/JSON、
  对比摘要和 SHA-256 清单。该数据只作算法研究基线，正式优化和验收前必须重新采集。
- 双 Profile 修改后已换回第一块 `WHO_AM_I=0x70` 并完成 20 秒短回归：Control/Health
  `2036/20`，100/1 Hz，CRC/gap/deadline/drop/I2C/Health/quiet 全 0；X/Y/Z 均值
  `-0.001053/0.001467/0.008661 dps`，标准差 `0.030055/0.031623/0.034462 dps`。
- 修复 ImuService 离线重初始化仍发布旧 valid/READY 快照的问题；进入 PROBE 时立即发布
  online/calibrated/valid 全 0 的新快照。field check 同时拒绝四个通道完全冻结的数据。
- 软件故障注入恢复通过：`CALIBRATING -> READY -> INVALID -> VALID_NOT_READY ->
  CALIBRATING -> READY`，111 个 INVALID 帧三轴为 0，最终 Health active=0、sticky 保留 IMU
  offline，CRC/gap/out-of-order/deadline 全 0，OLED 始终在线。
- 第一块最终正常固件 120 秒联合回归通过：主机 `122.587 s`，Control/Health `12195/122`，
  100/1 Hz，全部通信、实时性、I2C、Health 和 quiet 门禁为 0。

## 3. 板测结论与未执行项

- 两块模块都在 `0x68` 地址稳定 ACK。第一块实测 `WHO_AM_I=0x70`，按 MPU6500-compatible
  Profile 处理；第二块实测 `WHO_AM_I=0x68`，按标准 MPU6050 Profile 处理。
- 第二块原始静止陀螺零偏约 `12.715/2.758/0.265 dps`。原先约 `10 dps` 的统一绝对门限会使
  它无法完成 READY，因此 Profile 1 对 `0x68` 使用约 `20 dps` 启动校准门限，对 `0x70`
  保持约 `10 dps`；两者仍要求连续 300 点静止和 0.85-1.15 g 加速度模长。
- 20 秒未刚性固定候选数据：X/Y/Z 均值 `0.004001/-0.002193/-0.000336 dps`，标准差
  `0.031910/0.033893/0.042057 dps`；加速度模长 `1.011909 +/- 0.003862 g`。
- 600 秒静止数据：X/Y/Z 均值 `-0.012977/0.005852/0.010664 dps`，标准差
  `0.030266/0.030946/0.033636 dps`；加速度模长 `1.019505 +/- 0.003692 g`。
- 首分钟到末分钟的 X/Y/Z 均值从 `-0.000376/0.000931/0.000153 dps` 变化到
  `-0.017731/0.003084/0.023218 dps`，表现为上电热稳定相关的缓慢偏置变化。若完全依赖 Z 轴
  角速度纯积分且无外部航向校正，本次总体均值对应约 `6.4 deg/10 min` 的累计偏航误差。
- 第二块 600 秒静止数据：X/Y/Z 均值 `-0.007652/0.006010/-0.002115 dps`，标准差
  `0.023553/0.023628/0.020675 dps`；加速度模长 `1.083473 +/- 0.002243 g`。首 60 秒到
  末 60 秒均值从 `-0.015700/0.004317/-0.002876 dps` 变为
  `-0.011180/0.007783/-0.002605 dps`，Z 轴变化仅 `+0.000271 dps`；总体 Z 均值对应纯积分约
  `-1.3 deg/10 min`。随机噪声和 Z 热漂移都优于第一块，但约 `+8.35%` 的静态加速度模长
  偏差仍需正式六面加速度标定，不能把本次数据当作姿态精度验收。
- 原始字节证明并发 I2C 会截断部分 UART 帧尾；关闭 IMU I2C 后 10 秒恢复 100/1 Hz、CRC/gap 0。
  采样相位和短优先 quiet window 接入后，OLED 开启的 20 秒联合回归恢复全 0。
- 两块模块各有一次 10 分钟静止连续运行证据；双 Profile 和 invalid 快照修复后，第一块
  `0x70` 也已完成 120 秒联合回归，兼容路径门禁关闭。
- 换模块时未复位 MCU 的 20 秒采集复现 `I2C error=295`、OLED/IMU offline 和
  `active/sticky=0x00009800`。此时 100 Hz 遥测仍重复最后一份旧快照并保持 valid/READY flags，
  该安全缺陷已由 invalid 快照修复和自动软件故障测试关闭。
- 面包板模块只能四针整体拔插，单独断 SDA 会破坏当前接线，带电整体拔插也存在短接/错位风险；
  因此物理断开/恢复 deferred，待增加独立插头、跳帽或串联开关后再做，不能用软件注入冒充。
- 尚未执行三轴转动方向冻结。
- 尚未用示波器确认并联上拉与上升沿。

## 4. 硬件状态

- 用户确认四线接线正确：3V3、GND、PA1/SCL、PA0/SDA。
- 电机任务暂停；本 spike 不使用 PB8/PB9，不产生电机输出。
- 当前接线为第一块 `WHO_AM_I=0x70`；DAPLink `reset run` 后稳定应答并采样，没有损坏证据。

## 5. 构建证据

```text
FreeRTOS: 0 Error / 0 Warning
App:      0 Error / 0 Warning
Code=57220, RO=3068, RW=28, ZI=16236
HEX SHA-256=66A1263E853BB9217399A2FA605D0D48822C219EB276EB57060E34F72783A328
```

SysConfig 重新生成使 `platform/generated/ti_msp_dl_config.c` 出现两行空白尾随空格；禁止手改
generated，本文件不应作为功能修改暂存。

## 6. 下一步

1. 采集 Motion 并冻结三轴方向语义。
2. 增加温度时间序列，再决定预热门限或温度补偿；不做
   未经数据支持的在线偏置学习。
3. 有独立插头、跳帽或串联开关后，补做真实物理断开/恢复和 I2C 上升沿测试。
4. 填写最终 spike 文档；不 commit/tag/push，等待决定如何语义移植。

以上均为恢复本 spike 后的待办，不是当前电机工作开始前的阻塞项。恢复时必须从本交接和
`docs/spikes/MPU6050_HARDWARE_SPIKE.md` 继续，不得把当前证据写成 Phase 2B 完成。
