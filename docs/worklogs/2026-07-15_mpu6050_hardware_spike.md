# 2026-07-15 MPU6050 备用 IMU 硬件 Spike

## 目标

在不修改 dirty Phase 2A、不驱动电机的前提下，验证 MPU6050 作为备用 IMU 的硬件 I2C、
低延迟采样、启动静止偏置和诊断链，为后续 ICM42688 接口设计提供证据。

## 基线与位置

- 基线：`4b1a3db` / `refs/tags/phase-1f-operability-diagnostics`
- worktree：`C:\Users\Auror\ECHO-mpu6050-spike-work`
- branch：`refs/heads/codex/mpu6050-hardware-spike`
- 正式 Phase 2A worktree 保持原 dirty 修改，未被本任务修改。

## 关键决定

- 用户冻结 `PA1/PA0` 给 MPU6050 I2C 使用；OLED 地址不同，允许共线。
- 不使用 H8 PB8/PB9，因此不推翻已冻结的电机引脚。
- 不复制官方软件 I2C/DMP 示例，使用现有 BSP 硬件 I2C 和超时恢复机制。
- 不新增任务；ServiceTask 写快照，SystemTask 只读。
- 启动校准要求约 3 秒连续静止；运动会重启累计。
- 不自动在线学习 yaw 零偏，避免把慢速真实旋转误判为漂移。
- 旧 STM32 工程只参考连续读取、PT1 和四元数思路，不复制阻塞 HAL I2C、固定 200 Hz 或未校验 ID。
- 复用 Phase 1E quiet window 处理当前临时硬件上的 I2C/UART 并发，IMU 采用非阻塞短优先窗口。
- 不复制多套驱动；按 `WHO_AM_I` 选择 Profile 1。`0x68` 使用 MPU6050 温度公式和约 20 dps
  启动静止门限，`0x70` 保留 MPU6500-compatible 温度公式和约 10 dps 门限。

## 修改

- `bsp_i2c`：有界、明确 STOP 的寄存器 write-read 与读诊断。
- `module/device/mpu6050`：寄存器驱动、配置回读和六轴突发采样。
- `module/service/imu_service`：状态机、偏置、滤波、快照和恢复。
- App tasks：接入 ServiceTask/SystemTask，不增加任务。
- Health/Telemetry：sensor issue 与 100 Hz spike 映射。
- `tools/mpu6050_field_check.ps1`：Static/Motion 验收。
- `telemetry_capture.ps1`：可选保存原始串口字节；field check 增加 active/sticky、quiet 配对门禁。
- UART TX：EOT 后结算完成，DMA 启动统一收口到 ServiceTask。
- Spike 文档和实时交接。

## 验证

| 项目 | 结果 |
| --- | --- |
| PowerShell field-check AST | passed |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning |
| App size | Code=57220，RO=3068，RW=28，ZI=16236 |
| 烧录及校验 | passed，OpenOCD program/verify/reset passed |
| 无 IMU I2C A/B | 10 秒 1020/10 Control/Health，100/1 Hz，CRC/gap 0 |
| 相位错开中间结果 | 20 秒 99.558/0.842 Hz，CRC 11、gap 12，证明仅相位错开不够 |
| 最终候选联合回归 | 20 秒 2036/20，100/1 Hz，CRC/gap/deadline/I2C error、active/sticky 全 0 |
| quiet | acquire/release 4644/4644，active 0，max 38777 us |
| 候选静止统计 | X/Y/Z mean 0.004001/-0.002193/-0.000336 dps；stddev 0.031910/0.033893/0.042057 dps |
| 600 秒连续运行 | 60961/610 Control/Health，100/1 Hz，CRC/gap/deadline/I2C error、active/sticky 全 0 |
| 600 秒静止统计 | X/Y/Z mean -0.012977/0.005852/0.010664 dps；stddev 0.030266/0.030946/0.033636 dps |
| 首/末分钟 Z 均值 | 0.000153 / 0.023218 dps；纯积分总体约 6.4 deg/10 min |
| 第二块 20 秒 Profile 门禁 | 2036/21 Control/Health，100/1 Hz，CRC/gap/deadline/I2C/Health/quiet 全 0 |
| 第二块 600 秒墙钟/设备跨度 | 607.127 s / 609.600 s |
| 第二块 600 秒连续运行 | 60961/609 Control/Health，100/1 Hz，CRC/gap/deadline/drop/I2C/Health/quiet 全 0 |
| 第二块 600 秒静止统计 | X/Y/Z mean -0.007652/0.006010/-0.002115 dps；stddev 0.023553/0.023628/0.020675 dps |
| 第二块首/末 60 秒 Z 均值 | -0.002876 / -0.002605 dps；纯积分总体约 -1.3 deg/10 min |
| 第二块加速度模长 | 1.083473 +/- 0.002243 g；正式使用前需六面标定 |
| 第一块双 Profile 短回归 | reset 后 20 秒 2036/20 Control/Health，100/1 Hz，全部门禁为 0 |
| 第一块短回归 X/Y/Z | mean -0.001053/0.001467/0.008661 dps；stddev 0.030055/0.031623/0.034462 dps |
| invalid 快照软件故障恢复 | READY 后强制 3 次失败；111 帧 INVALID 三轴为 0，重新校准后恢复 READY |
| 软件故障状态序列 | CALIBRATING -> READY -> INVALID -> VALID_NOT_READY -> CALIBRATING -> READY |
| 软件故障链路 | 3052/31 Control/Health，100/1 Hz，CRC/gap/out-of-order/deadline 0，最终 active 0 |
| 第一块最终 120 秒回归 | 主机 122.587 s；12195/122 Control/Health，100/1 Hz，全部门禁为 0 |
| 第一块 120 秒 X/Y/Z | mean -0.004778/-0.000418/0.004142 dps；stddev 0.029737/0.030829/0.033293 dps |
| 最终正常 HEX SHA-256 | `66A1263E853BB9217399A2FA605D0D48822C219EB276EB57060E34F72783A328` |

## 仓库外数据归档

两块 600 秒数据已复制到：

```text
C:\Users\Auror\ECHO-mpu6050-spike-data\2026-07-15_static-600s
├── whoami-0x70
│   ├── capture.bin
│   ├── capture.csv
│   ├── capture.json
│   └── mpu6050-result.json
├── whoami-0x68
│   ├── capture.bin
│   ├── capture.csv
│   ├── capture.json
│   └── mpu6050-result.json
├── comparison-summary.json
└── SHA256SUMS.csv
```

- 8 个复制文件已逐个与 `C:\tmp` 原件比较 SHA-256，全部一致。
- 原始串口与大 CSV 不进入 Git；`SHA256SUMS.csv` 用于确认归档未被意外修改。
- 这些数据可用于离线滤波、偏置、噪声和温漂算法的早期比较，但两次测试的固定方式、温度条件
  和安装姿态未完全标准化。任何正式算法选择、参数冻结或阶段验收前都必须按新测试方案重新采集。

## 风险与后续

- 两块模块地址均为 `0x68`，分别实测 `WHO_AM_I=0x70` 和 `0x68`；并联上拉和上升沿尚未
  用示波器确认。
- MPU6050 的轴向与安装方向尚未冻结。
- 单点温度只能保存校准温度，不能形成可信温漂模型。
- 旧 STM32 工程没有校验 ID；本 spike 必须按实测 `WHO_AM_I` 区分标准 MPU6050 与
  MPU6500-compatible，使用各自温度换算，禁止仅凭模块丝印命名。
- 正式底盘 IMU 仍是 ICM42688；本 spike 结果不能替代正式 Phase 2B 验收。
- 两块 600 秒 Static 和第一块修复后 120 秒联合回归已通过；仍需完成三轴 Motion 和轴方向冻结。
- 换模块未复位 MCU 时，20 秒采集为 `I2C error=295`、OLED/IMU offline、
  active/sticky `0x00009800`，Control 数据却完全冻结在旧值并保留 valid/READY。该结果证明
  ImuService 进入离线重初始化时必须主动发布 invalid 快照，主机工具也应拒绝完全冻结的数据。
- 上述 stale 快照缺陷已修复并通过自动软件故障恢复。物理断开恢复仍 deferred：当前模块在面包板上
  只能四针整体拔插，带电拔插存在错位和短接风险；待增加独立插头、跳帽或串联开关后再测。
- 若继续优化 yaw，下一轮应记录温度时间序列，建立预热门限或温度补偿，不能只凭单次均值调常数。

## 暂停交接

- 用户决定暂停算法补偿、三轴 Motion、六面标定、物理断开恢复和 ICM42688，先恢复 Phase 2A
  电机/编码器工作。
- 当前模块状态只能写 `bench_passed / pairwise_passed`，不能写 `completed` 或
  `integration_ready`，也不能写 Phase 2B 已开始或完成。
- 恢复本 spike 时必须重新采集固定方式、安装姿态和温度条件标准化的数据；仓库外两组 600 秒
  数据只作算法研究基线。
- 下一活动工作树：`C:\Users\Auror\ECHO-phase2a-work`。该 worktree 的 dirty 修改未被本 spike
  修改、暂存或覆盖。

## 结束状态

- checkpoint commit：`ff207b5`，`feat: checkpoint MPU6xxx hardware spike`；不创建阶段 tag。
- push：`origin/codex/mpu6050-hardware-spike` 已推送；正式 main 未合入、未 push。
- docs follow-up：`70c1880` 记录 checkpoint 的真实 push 状态；后续只允许修正文档准确性。
- 正式工程 `E:\ECHO`、Phase 2A dirty 工作树、stash 和备份：未修改或删除。
