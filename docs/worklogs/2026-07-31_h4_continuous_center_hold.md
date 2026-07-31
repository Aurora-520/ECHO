# 2026-07-31 H4 持续静态回中

## 目标

- H4 作为正式持续回中任务，不通过 TEST 页面启动。
- 底盘始终关闭，球到中心后任务不结束，后续扰动可无限次重新回中。
- OLED MAIN 与 H4 计时页显示实时球位置，无球时显示 `--`。

## 实现

- H4 从循迹任务集合和活动循迹上下文中移除，不再依赖红外标定或扫描。
- H4 启动请求 `BallBalanceService_RequestStartPositionHold(0)`；运行服务持续检查球控状态并
  强制底盘安全关闭；停止时中止球控并保持底盘关闭。
- 静态保持在中心带内使用 quiet hold 控制，偏离中心带后恢复普通搜角以克服静摩擦。

## 验证

- FreeRTOS/App 全量构建及最终 App 构建均为 0 Error / 0 Warning；最终应用大小为
  `Code=120312, RO=3972, RW=188, ZI=25296`。
- CMSIS-DAP `2e4c7219`、500 kHz 烧录成功，按用户要求未做 Flash 回读。
- 原始数据：`tmp/h4_chassis_motion_valid_20260731_215425_ball.csv` 和对应 control/summary。
- 88.2 s 内视觉约 58.6 Hz，vision invalid 0、fault 0、拒绝电机命令 0、deadline 0；
  底盘目标保持 0。最终球位置 -5.5 mm、速度 2 mm/s。
- 人工扰动下球位置峰值 -74.7/+35.1 mm，控制输出曾达到 +/-25 deg；静态回中通过，
  运动稳球需在 H5 增加底盘加速度前馈。
