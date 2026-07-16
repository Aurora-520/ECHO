# ECHO MPU6050 Spike 当前交接

```yaml
handoff_schema: 1
updated_at: 2026-07-16T18:50:00+08:00
updated_by: Codex
status: bench_passed
```

## 1. 位置与边界

```text
正式工程：E:\ECHO
正式已验收基线：4b1a3db / refs/tags/phase-1f-operability-diagnostics
正式 Phase 2A 工作树：C:\Users\Auror\ECHO-phase2a-work（保持 dirty，不得覆盖）
本 spike 工作树：C:\Users\Auror\ECHO-mpu6050-spike-work
branch：refs/heads/codex/mpu6050-hardware-spike
checkpoint code commit：ff207b5
push status record commit：70c1880
current HEAD：本文件所在的 unattended stability checkpoint
upstream：origin/codex/mpu6050-hardware-spike
push：用户已明确授权；本文件所在 checkpoint 推送到 upstream
```

本分支仅用于备用 MPU6050 低功率硬件验证，不是正式 Phase 2B。它不直接合入正式 main，
不创建阶段 tag；仅以独立 checkpoint 分支保存并推送已验收证据。

用户于 2026-07-16 要求先完成本次无人值守稳定性收尾，再恢复独立算法工作树
`C:\Users\Auror\ECHO-mpu6050-algorithm-work`，并明确要求重新烧录和采集新数据，不局限于旧数据。
本 spike 仍只保留非阶段证据，不合入正式 main。

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
- 2026-07-16 再次完成 FreeRTOS/App full rebuild，均为 0 Error / 0 Warning；HEX SHA-256 保持
  `66A1263E853BB9217399A2FA605D0D48822C219EB276EB57060E34F72783A328`。
- 五次独立 DAPLink reset 后的 20 秒 Static 均通过；协议压力后的 30 秒复核记录
  `ParameterErrorCount=7`、`ApplySequence=53`，通信、I2C 与 Health 门禁为 0。
- reset 后 15 分钟基线通过：Control/Health `91453/915`，全部 Control valid+READY，
  CRC/gap/deadline/I2C/active/sticky 全 0。
- 无人值守静态 soak 累计请求采集 27000 秒，共 30 个 900 秒段；两次 runner 主机墙钟合计
  `27573.452 s`，Control/Health
  `2743306/27432`，全部 Control READY，CRC/gap/duplicate/out-of-order/deadline/I2C/
  active/sticky 全 0；最小栈 94 words、heap 3056 B、ring 464 B、quiet max 38777 us。
- soak 后 DAPLink `reset run` 成功；最终 20 秒 Static 为 `2032/20` Control/Health，全部门禁
  为 0，参数 sequence/error 0/0，执行器输出许可 0。
- 11:14 再次独立 `reset run` 后 20 秒 Static 为 `2038/20`，全部 READY，所有门禁 0，
  X/Y/Z mean `-0.001662/0.006787/-0.002731 dps`。
- 30 段加权 X/Y/Z mean 为 `-0.056074762/0.013271149/0.025727453 dps`，总体标准差为
  `0.047037499/0.037879724/0.034651981 dps`；加速度模长
  `1.018024726 +/- 0.003618586 g`。当前遥测无同步温度，禁止据此拟合温补。

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
- 初始两次 20 秒检查分别出现 READY `0/2037`、I2C error `270`，以及 READY `54/2036`、
  I2C error `61`；两次 OLED offline、active/sticky `0x00009800`。随后 30 秒、60 秒、五次 reset、
  15 分钟基线和 27000 秒 soak 均保持 I2C error 0。失败原始目录已保留，当前只将其定性为
  启动期瞬态线索，不宣称根因已经由示波器或物理断线测试冻结。

## 4. 硬件状态

- 用户确认四线接线正确：3V3、GND、PA1/SCL、PA0/SDA。
- 电机任务暂停；本 spike 不使用 PB8/PB9，不产生电机输出。
- 当前总线地址为 `0x68`，历史接线记录为第一块 `WHO_AM_I=0x70`；总线地址与身份寄存器不是
  同一概念。DAPLink `reset run` 后稳定应答并采样，没有损坏证据。
- 27000 秒静态 soak 后再次 `reset run`，20 秒 Static 清洁复核通过；板卡仍无运动或高功率输出。

## 5. 构建证据

```text
FreeRTOS: 0 Error / 0 Warning
App:      0 Error / 0 Warning
Code=57220, RO=3068, RW=28, ZI=16236
HEX SHA-256=66A1263E853BB9217399A2FA605D0D48822C219EB276EB57060E34F72783A328
```

SysConfig 重新生成使 `platform/generated/ti_msp_dl_config.c` 出现两行空白尾随空格；禁止手改
generated，本文件不应作为功能修改暂存。

稳定性证据根目录：
`C:\Users\Auror\AppData\Local\Temp\echo-unattended-soak-20260716-overnight`。其中
`soak-result.json` 为 25200 秒主体，`supplement-1800\soak-result.json` 为补充 1800 秒，
`final-static-20s\mpu6050-result.json` 为 reset 后清洁复核。初始瞬态、五次 reset、协议压力后
复核和 15 分钟基线的逐项路径见 2026-07-16 worklog。

## 6. 下一步

1. 在独立算法工作树增加原始六轴、同步温度和明确时间戳的采集帧，不扩充已满 128 B 的 Health 帧。
2. 重新采集冷启动到热稳定、多温点、多姿态、三轴正反转、六面加速度和外部航向参考。
3. 以 host replay A/B 温补、四元数、Mahony/Madgwick、Allan 和必要的 KF/EKF，再决定板上实现。
4. 有独立插头、跳帽或串联开关后，补做真实物理断开/恢复和 I2C 上升沿测试。

以上均为恢复本 spike 后的待办，不是当前电机工作开始前的阻塞项。恢复时必须从本交接和
`docs/spikes/MPU6050_HARDWARE_SPIKE.md` 继续，不得把当前证据写成 Phase 2B 完成。

## 7. 长稳补充审计

- 主体 28 段与补充 2 段跨段 sequence、uptime 均单调；7 次 32 位微秒时间戳回绕保持周期
  `9998-10002 us`、max jitter `2 us`、deadline 0。
- 8 个段在重新打开串口时跳过总计 212 B 前导非同步字节，单次最多 48 B；没有伴随 CRC 或
  sequence gap，属于从帧中间接入的捕获边界现象。
- 两份结果 SHA-256：主体
  `5771198413DC34D261402D8A50DE8C3785F0D2D4147A739666292E45EB56B499`，补充
  `35414E39E227E01C42A87E492FE9A2051F090771BD960261D5B4350E91E9F799`。
- 六份关键结果 JSON 与 SHA-256 清单已归档到
  `C:\Users\Auror\ECHO-mpu6050-spike-data\2026-07-16_unattended-stability`；完整 raw/CSV 仍在临时
  证据根，可能被系统清理。
