# 2026-07-16 MPU6050 无人值守稳定性

性质：normal

## 目标与范围

- 在不驱动电机、不触碰正式 `E:\ECHO` 或 dirty Phase 2A 工作树的前提下，验证 DAPLink、COM4、
  OLED 和总线地址 `0x68` 的 MPU6xxx 在当前 FreeRTOS 架构中的长时间稳定性。
- 保存分段证据，检查共享 I2C、UART、Health、deadline、栈、heap、ring 和时间戳回绕。
- 完成 reset 后独立复核，更新状态、交接、Spike、调试/集成手册和日志。
- 不执行电机、高功率、带电拔插、真实物理断线、Motion、温度循环或正式算法验收。

## 开始状态

- 正式工程：`E:\ECHO`，只读，未修改。
- spike：`C:\Users\Auror\ECHO-mpu6050-spike-work`，分支
  `codex/mpu6050-hardware-spike`，HEAD `d39f7e2f6d8cdbb02cc2ee4675a885b7cf84137f`。
- 禁止触碰：`C:\Users\Auror\ECHO-phase2a-work`。
- 既有 dirty：`app/tasks/display_task.c` 行尾噪声、
  `platform/generated/ti_msp_dl_config.c` 两行空白；均未还原、暂存或覆盖。
- 固件：FreeRTOS/App 全量构建 0 Error / 0 Warning；Code `57220`、RO `3068`、RW `28`、
  ZI `16236`；HEX SHA-256
  `66A1263E853BB9217399A2FA605D0D48822C219EB276EB57060E34F72783A328`。

## 修改

- 新增 `tools/unattended_soak_test.ps1`，按 10–900 秒有界段串行调用 Static field check，使用单一
  COM owner，并原子更新 `soak-progress.json`/`soak-result.json`。
- 工具 AST 和 3 x 20 秒自测通过；脚本 SHA-256
  `A62AC05EB7CA21D3749CDC5037EB0351F50BE8D493631B13418C9EF218BE9756`。
- 没有修改固件、引脚、任务、协议或执行器行为。

## 硬件状态

- DAPLink 只接 GND、SWDIO、SWCLK、nRESET；供电保持原 USB/DAPLink 低功率条件。
- OLED 与 IMU 共用 I2C0 PA1/SCL、PA0/SDA；IMU 总线地址为 `0x68`。地址不能代替
  `WHO_AM_I`；历史记录当前模块为 `WHO_AM_I=0x70`。
- 未接电机、云台、树莓派或高功率输出；Health 的 actuator output permitted 始终为 0。

## 验证

### 前置回归

- 协议压力覆盖非法 ID、NaN/Inf、越界、坏 CRC、截断、BUSY、duplicate 和连续 50 次调参。
- 5 次 `reset -> calibration -> 20 s` 通过。
- 单次 15 分钟为 `91453/915` Control/Health，全部门禁 0。
- soak 工具 3 x 20 秒自测为 `6107/61`，3/3 通过。

### 27000 秒无人值守 soak

主体由 Windows 一次性计划任务执行，补充段由同一工具在主体结束、未复位 MCU 的条件下继续。

```text
请求采集：25200 s + 1800 s = 27000 s，30 x 900 s
主体：2026-07-16 02:55:37 -> 10:04:38 +08:00，主机墙钟 25740.823 s
补充：2026-07-16 10:07:26 -> 10:37:58 +08:00，主机墙钟 1832.629 s
两次 runner 主机墙钟合计：27573.452 s
证据根：C:\Users\Auror\AppData\Local\Temp\echo-unattended-soak-20260716-overnight
```

| 指标 | 结果 |
| --- | ---: |
| 完成段 | 30 / 30，全部 passed |
| Control / Health / READY | 2743306 / 27432 / 2743306 |
| CRC / gap / duplicate / out-of-order | 0 / 0 / 0 / 0 |
| deadline / I2C error / active / sticky | 0 / 0 / 0 / 0 |
| period / max execution / max jitter | 9998-10002 / 20 / 2 us |
| OLED / actuator permitted | 全段 online / 全段 0 |
| 最低 stack / heap | 94 words / 3056 B |
| TX ring high-water / quiet max | 464 B / 38777 us |
| publish/transport/TX drop/RX overflow | 0 / 0 / 0 / 0 |

跨段 sequence 和 uptime 单调。主体段 2、7、11、16、21、25 与补充段 2 共 7 次 32 位微秒
时间戳回绕，period、jitter 和 deadline 均连续。8 个段重新打开串口时从帧中间接入，共跳过
212 B 前导非同步字节，单次最多 48 B；没有伴随 CRC 或 sequence gap，因此属于捕获边界。

30 段结果 JSON 的加权统计：

| 量 | mean | stddev |
| --- | ---: | ---: |
| Gyro X | -0.056074762 dps | 0.047037499 dps |
| Gyro Y | 0.013271149 dps | 0.037879724 dps |
| Gyro Z | 0.025727453 dps | 0.034651981 dps |
| Accel norm | 1.018024726 g | 0.003618586 g |

这些值证明静止链路稳定，但三轴段均值存在慢变化。当前遥测没有同步温度和原始六轴，不能拟合
温度模型，也不能把长稳通过写成姿态或长期 yaw 精度通过。

结果 SHA-256：

```text
主体 soak-result.json     5771198413DC34D261402D8A50DE8C3785F0D2D4147A739666292E45EB56B499
补充 soak-result.json     35414E39E227E01C42A87E492FE9A2051F090771BD960261D5B4350E91E9F799
```

六份关键结果 JSON 和 `SHA256SUMS.csv` 已归档到
`C:\Users\Auror\ECHO-mpu6050-spike-data\2026-07-16_unattended-stability`。完整 raw/CSV 仍位于上述
`AppData\Local\Temp` 证据根，约 410 MB，可能被系统临时文件清理；摘要归档不依赖该临时目录。

### reset 后清洁复核

- 删除计划任务 `Codex-ECHO-MPU6050-Soak-20260716`，确认原 runner/OpenOCD 已退出。
- 10:44 的 DAPLink `reset run` 后结果位于主证据根的 `final-static-20s`：`2032/20`，全部
  READY，所有门禁 0，结果 SHA-256
  `ED9BCB36DB6717163F6002D27A67DACC207A06EFBE855DB40BC044C4FFE2237E`。
- 11:14 再次独立 `reset run`，未改写 Flash；输出
  `C:\Users\Auror\AppData\Local\Temp\echo-mpu6050-20260716-final-clean-111409`。
- 第二次主机 `21.611 s`，Control/Health `2038/20`，全部 READY；CRC/gap/duplicate/out-of-order、
  deadline、I2C、active/sticky、drop 全 0，OLED online，输出禁止。
- period `9999-10001 us`、max execution `20 us`、max jitter `1 us`；最低栈 `94 words`、
  heap `3056 B`、ring `344 B`。
- X/Y/Z mean `-0.001662/0.006787/-0.002731 dps`，stddev
  `0.028896/0.029570/0.033066 dps`；accel norm `1.018698 +/- 0.003394 g`；结果 SHA-256
  `34DFF736AF31D4772710C782F48C4C3CD9F8348D8350BE7408B92C0AA3342DA9`。

## 问题与判断

长稳前两份 20 秒证据必须保留：

- `%TEMP%\echo-mpu6050-20260716-015609`：READY `0/2037`、I2C error 270、OLED offline、
  active/sticky `0x00009800`。
- `%TEMP%\echo-mpu6050-20260716-015731`：READY `54/2036`、I2C error 61、OLED offline、
  active/sticky `0x00009800`。

后续复位循环、30 段长稳和两次 reset 后清洁复核均未复现。现有证据只能支持“未定位的启动/
共享 I2C 瞬态”，不能写成已修复。正式线束需要核实并联上拉、去耦、共地和示波器上升沿。

## 风险与下一步

- 状态保持 `bench_passed / pairwise_passed`，不是 `integration_ready`，也不是 Phase 2B 完成。
- 真实物理断开/恢复、Motion、三轴方向、六面标定、温度循环和正式整车线束仍未执行。
- 算法工作恢复后必须重新采集冷启动到热稳定、多温点、多姿态、三轴正反转、六面加速度和
  外部航向参考，再 A/B 温补、四元数、Mahony/Madgwick、Allan 和必要的 KF/EKF。
- 六轴 IMU 没有外部航向观测时，长期 yaw 不可观测，复杂滤波不能单独消除物理限制。

## 结束状态

- 计划任务已删除，COM4/DAPLink 已释放；固件 reset 后两次 20 秒复核通过。
- 用户已明确授权 commit/push；本次只提交 soak 工具和稳定性文档，不创建阶段 tag、不合入 main。
- 正式 `E:\ECHO`、Phase 2A、既有 `display_task.c` 行尾噪声、SysConfig 生成空白、stash 和备份均未修改或暂存。
