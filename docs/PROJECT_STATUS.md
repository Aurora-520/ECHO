# ECHO 当前状态

最后核对日期：2026-07-31（Asia/Shanghai）

## 2026-07-31 H4 持续静态回中基线

- H4 已从旧 AB 循迹任务改为正式 `BALL CENTER`：启动后底盘左右输出持续锁零，
  张大头以本次启动角为相对零位，将钢球持续保持在 `0 mm`；首次到位不结束，手动拨离后
  可重复回中，直到任务停止、复位或球控故障。
- MAIN 页显示当前钢球位置，无有效目标时显示 `BALL:--`；H4 正式计时页底部同步显示
  `BALL:+x.xmm`，其他正式任务仍保持原计时布局。
- 88.2 s 扰动实测中视觉约 58.6 Hz，视觉无效 0、球控 fault 0、张大头拒绝命令 0、
  deadline miss 0，底盘左右目标持续为 0。球位置范围约 `-74.7..+35.1 mm`，最终稳定在
  `-5.5 mm`；控制输出达到 `+/-25 deg`，说明静态回中可用但运动稳球仍需底盘加速度前馈。
- FreeRTOS/App 全量构建以及 OLED 补充后的 App 构建均为 0 Error / 0 Warning；应用最终
  `Code=120312, RO=3972, RW=188, ZI=25296`。CMSIS-DAP `2e4c7219` 以 500 kHz 烧录成功，
  按用户要求跳过 Flash 回读。

## 2026-07-31 H3 滚球末端制动与反向稳定性

- 正向目标仍为 +50 mm，+39 mm 仅为锁存制动入口；最终实测在 +46.9 mm、18 mm/s
  切换反向。反向增益提高到 2.6 后，经过 -39.2 mm 时仍为 -49 mm/s，不再停住。
- 最终在 -47.7 mm、0 mm/s 连续稳定 250 ms 后完成，用时 5.519 s；完成后 H3 遥测计时
  冻结，故障 0，视觉持续有效。当前到达容差为 10 mm、速度门限 15 mm/s、稳定时间
  150 ms；没有起滚角学习或掉电保存。换向制动力 260 mdeg/(mm/s) 作为当前最优基线，
  五次复测均自动完成；四次完整过程为 5.110--5.357 s，另一次完整窗口后续复测为 5.079 s。
- UART1 增加 500 ms 上电零字节恢复和原有 80 ms 运行期断流恢复，只重启 UART1 RX，
  UART0/UART2/UART3 不变。
- 重新安装时任务会重新读取当前张大头绝对角作为本次零位，因此不依赖旧安装角；仍要求
  水管初始水平、机械方向一致且控制所需相对倾角不超过 +/-25 deg。
- 控制器主机测试和 App 构建通过，App 0 Error / 0 Warning；250 kHz 烧录成功，当前迭代
  尚未逐字节回读、提交或推送。

## 2026-07-30 相机架装车后的循迹阻尼与 H4 整圈修正（当前机械版收口）

- 加装相机大架后，用户实车观察到直线段左右摇摆。修改前一次连续遥测覆盖 H2、H4 AB
  和 H5/H6 低冲击任务；纠偏目标与车体横摆角速度的相关系数绝对值约为
  `0.76 / 0.75 / 0.87`，纠偏领先车体约 `150 / 80 / 110 ms`，确认主要是循迹差速反复驱动
  车体横摆，不是底盘速度 PID 自发振荡。
- 仅在红外位置接近中心且横摆角速度不高时降低导数预判和最大差速纠偏；进入弯道后恢复原
  参数，并给纠偏目标增加每 16 ms 最多 8 rpm 的斜坡。速度 PID、前馈、电池补偿、PWM
  限幅、通用角度环和安全门控均未修改。
- H4 原实现到 1500 mm 即停车，已改为在 B 点只锁存 `ab_elapsed_ms/ab_passed`；AB 未在
  7.5 s 内通过仍故障停车，通过后切换普通整圈循迹并复用 A 点终点判定。
- 第一轮实车 H2 为 13.55 s；H4 为 14.78 s，AB 在 4.72 s 通过后继续 10.06 s 才结束，
  证明 B 点不停且 AB 小于 8 s。加入纠偏斜坡后，弯道 10 rpm 以上目标突跳从 23 次降至
  2 次，用户确认弯道改善；H2/H4/H5 连续实测为 13.70/14.64/21.85 s。
- 最终 H2 中心直线使用 6 扫描预判和 35 rpm 上限；H5/H6 巡航从 80 降到 70 rpm，按
  已测 21.85 s 比例预计约 24.5--25 s。最终窗口从 70 rpm 整圈中途开始，只记录后半段
  11.65 s，完整 25 s 计时未单独捕获。用户确认任务 1 无需继续追求过稳、AB 段已较稳，
  最后整圈的起步大纠偏来自初始摆放偏差，决定当前机械版到此收口。
- 最终 App `0 Error / 0 Warning`，`Code=112792, RO=3876, RW=188, ZI=24944`；HEX
  SHA-256 `6925FF80A61784EE8C3620D993ED9079D4A102A1DFD90F106CE1C38D58A7E369`。
  CMSIS-DAP `2e4c7219` 烧录和逐字节回读通过，回读 SHA-256
  `89F64398CF0109E58EC699C13A5C7201E525C8831AC2CCD373F607BE350C4F51`。
- 最终遥测为红外 125 Hz、控制/IMU/姿态约 100 Hz、CRC 0、deadline 0、I2C 0、IMU
  ready、任务结束执行器许可关闭。ESP 离线与遥测带宽 drop 仍会触发 Health 告警，但未影响
  控制周期。机械再次修改后必须重新复核重心、万向轮、传感器高度和直线参数。

## 2026-07-30 MaixCAM 视觉恢复到 UART1

- UART1 先前无法接收并非 PA9、IRQ 向量或接收驱动损坏，而是正式配置仍保留临时
  `ECHO_BALL_VISION_USE_UART2=1`。该选择会跳过 `BSP_TfminiUart_Init()`，因此 UART1
  NVIC 和接收环形缓冲未初始化；修复前向 PA9 发送 880 B，板上 UART1 计数始终为 0。
- 正式所有权已恢复为 MaixCAM `UART1 PA9/RX @ 115200`，UART2 继续用于 ESP32 链路。
  UART0、UART2、UART3 底层以及所有 H 任务和控制算法均未修改。
- COM7/UART1 板测连续发送 100 个递增序号、CRC 正确的 22 B 视觉帧：UART1 收到
  `2200/2200 B`，100 帧全部有效；CRC、格式、重复、丢帧、乱序、RX overflow 和 unexpected
  IRQ 均为 0，RX high-water 为 12 B。同期 UART2 ESP TX DMA 继续完成发送，deadline 为 0。
- App 重建 `0 Error / 0 Warning`，`Code=112680, RO=3876, RW=188, ZI=24936`；
  HEX SHA-256 为 `D3F6A113C631EB6A7B97B588DEE9134EAB8C8FFE21D763911C619575A9FF8CE2`，已烧录运行。

## 2026-07-30 OLED/按键显示响应修复

- H3 滚球遥测接入后，UART0 DMA 长时间繁忙。DisplayTask 原来只尝试直接抢占
  quiet window，没有登记 priority request；20 秒板测仅完成 37 次刷新，申请被延后
  1062 次，因此 MAIN 页姿态和按键操作在视觉上近似冻结。
- DisplayTask 现在先调用 `SerialTx_RequestPriorityQuietWindow()`，再通过
  `SerialTx_TryBeginRequestedQuietWindow()` 获取窗口。只修改 OLED/UART 仲裁，不修改
  IMU、控制算法、任务状态机或数据来源。
- 修复后 10 秒板测完成 122 次刷新（约 12.2 Hz），I2C 错误 0、SystemTask deadline 0，
  DisplayTask 最低栈余量 274 words。IMU 本身始终在线，物理按键扫描和 UI 注入事件链正常。
- App 全量重建 `0 Error / 0 Warning`，`Code=110984, RO=3876, RW=188, ZI=24616`；
  HEX SHA-256 为 `AD9ED481422FAAA92D047F16938CE07F8196A46204DBD8A4DB63DB63F80D0D31`。

## 2026-07-30 H3 MaixCAM 钢球视觉闭环（UART2 已实测，视觉连续性待修）

- 视觉权威来源为 `Kyson-Y/ball_detection`，本轮核对最新提交 `510c47c`。UART 协议仍为
  `115200 8N1`、22 字节小端序帧和 CRC16-CCITT-FALSE，字段及固定测试包未变化。
- 最新相机程序请求 `640x480 @ 60 FPS`，队友实测检测和 UART 输出约 `44.4--45.2 Hz`；
  新增 `http://10.5.66.1:8080/status.json`，可观测相机实际控制频率、位置、速度、置信度、
  温度、状态位和 UART 错误。上游 11 项检测、协议、滤波和状态页测试在 Python 3 环境通过。
- 正式固件复用 UART2 `PB16/RX` DMA 接收 MaixCAM UART0 `A16/TX`，`PB15/TX -> A17/RX` 当前非必需；流式解析位置、速度、序号、帧龄、
  置信度和状态位；预测、低置信、参考失配、未标定及温度告警数据均禁止进入闭环。
- H3 已接入 `O -> +50 mm -> -50 mm` 状态机和球位置 PD。最终判定为误差不超过 10 mm、
  速度不超过 15 mm/s 并持续 150 ms；当前调试阶段关闭 H3 总超时。相机最后一帧后超过 120 ms
  即回水平并故障退出，任务完成后保持目标，结果确认或急停时释放张大头后端。
- 当前没有机械原点开关；启动时把张大头当前位置当作水管水平零位。实测前必须手动调水平、
  避开曲柄死点并放球到 O 点。电机方向只允许通过 `H_BALL_MOTOR_POLARITY` 校正。
- 新增 UART type 16 摆杆遥测、CSV 解码和相机/MCU 双端采集工具。固定帧测试以及既有 IMU、
  姿态、TFmini 遥测夹具全部通过。
- UART2 DMA、MCU 协议解析、张大头 `+/-70 deg` 范围和带球小角度动作已实测；相机约
  `33--51 Hz`，UART 错误为 0。`-6 deg` 测试中坐标在 52 ms 内从 `+7.03 mm` 跳到
  `-102.06 mm`，而物理滚动应约 0.7--1.2 s，确认当前空管背景差分会切换目标。
- 最新 FreeRTOS/App 全量构建为 0 Error / 0 Warning；当前 H3 参数为 `Kp=120 mdeg/mm`、
  `Kd=10 mdeg/(mm/s)`、最大 `+/-6 deg`、斜率 `12 deg/s`。视觉连续性修复前禁止宣称
  5 s/1 cm 指标通过，也不得继续自动闭环调参。

## 2026-07-29 H5/H6 平衡优先整圈循迹（代码完成，待实车）

- H5 `LAP CENTER` 与 H6 `LAP HOLD` 已接入相同的低扰动底盘策略；两者未来只在滚球目标位置
  上不同，本阶段均不读取球位置、不驱动摆杆。
- 20 rpm 起步，2.0 s 线性升至 80 rpm，之后整圈恒速；预计 6.14 m 用时约 23--25 s。
  半径 0.5 m 弯道的理论向心加速度约 0.15 m/s^2，为 30 s 限制保留约 5 s 余量。
- 平衡任务使用 12 扫描导数预判和 50 rpm 最大纠偏，并复用 H2 已验证的 A 线防误触、
  五扫描终点确认、80 ms 受控刹车、丢线和扫描陈旧保护；29 s 未完成即故障停机。
- 最新全量构建为 0 Error / 0 Warning；应用大小 `Code=107352, RO=3780, RW=188,
  ZI=24656`，HEX SHA-256 为
  `918CC8C38776EDB62F4CA7F5C01F3D41B7CA4E5E7EE0AA07E396DA92D64FDBDE`。
- 当前未烧录、未实车、未接球；H5/H6 的时间与 ±1 cm 指标均为 `not run`。

## 2026-07-29 H4 AB 段无球行驶（代码完成，待实车）

- 题目 AB 为 1.5 m 直线，要求按键启动后顺时针通过 B，AB 时间不超过 8 s；当前阶段按用户
  要求不读取球姿态，只验证底盘行驶。
- `H4 / AB CENTER` 已接入独立低冲击速度轨迹：20 rpm 起步，1.8 s 线性升至 110 rpm，
  里程达到 1500 mm 后执行 80 ms 受控刹车，7.5 s 未完成则安全失败。
- AB 专用红外参数为 8 扫描导数预判和 35 rpm 最大纠偏；低速时纠偏上限还会随基础速度收紧，
  避免单侧轮接近停转。任务 1 的速度和终点逻辑未修改。
- FreeRTOS/App 全量构建为 0 Error / 0 Warning；应用大小 `Code=107096, RO=3780,
  RW=188, ZI=24648`，HEX SHA-256 为
  `31ACC9FCAB3623F1CE10A616B4A943F07B023D153050AC05297ABDBC68274D43`。
- 当前尚未烧录、尚未执行无球 AB 实车测试，8 s 指标状态为 `not run`。

## 2026-07-29 H 题任务 1 红外循迹正式基线（冻结）

- 任务 1 已完成正式地图顺时针实车循迹和 A 点横线停车，当前版本按用户要求冻结，不再继续调参。
- 8 路反射全扫描为 125 Hz，控制/IMU/姿态为 100 Hz；标定结果支持 Flash 掉电保存，并提供
  `tools/install_reflectance_calibration.ps1` 作为受校验的离线安装入口。
- 行驶策略为首 250 mm 的 100 rpm 柔和起步、弯道 120 rpm、直线 140 rpm、末段 75 rpm；
  使用连续位置估计、导数预判、最近连续黑线簇选择和左右轮差速纠偏。
- 终点按里程和顺时针累计航向提前减速并使能判定；五次扫描窗口中至少两次出现连续 4 路
  黑线即触发 80 ms 受控刹车。当前正式版本不做横线后的 IMU 回正或额外编码器前移。
- 最新 60.7 s 遥测通信无 CRC、丢帧、乱序、deadline 或 I2C 错误，电池为
  15.589--15.686 V；该文件只覆盖最后 2.84 s 运动，因此只作为末段与停车证据。
- 2026-07-29 正式全量构建为 0 Error / 0 Warning，应用大小 `Code=106760, RO=3780,
  RW=188, ZI=24632`，HEX SHA-256 为
  `BB075E80B15E396DA59986467BE4229551B999AF09C44DFB084CCADBD4BCA9AC`。
- 详细记录见 `docs/worklogs/2026-07-29_h2_line_follow_bringup.md`。

## 2026-07-28 513X整车控制阶段收尾

- 513X-4S/GMR 整车已完成两轮连续 12 段落地演示：1600 mm 段实测
  1600.8--1601.3 mm，500 mm 段实测 500.3--500.8 mm，90 度转向最大误差
  0.46 度，360 度自旋最大误差 0.39 度；正常直行最终偏航不超过 0.27 度。
- 最终显式零速命令已纳入 `tools/chassis_demo_rectangle.ps1` 的成功和失败收尾，
  第二轮结束时执行器许可关闭，Health、I2C、deadline 均干净。
- 原地转向达到目标后仍会等待剩余安全时限，直接前后换向也仍有万向轮扰动；用户决定本阶段
  暂不修改，留给未来正式自主任务。控制参数冻结，不再为演示继续调参。
- 2026-07-28 FreeRTOS/ECHO 全量重建为 0 Error / 0 Warning；详细数据见
  `docs/worklogs/2026-07-28_513x_continuous_motion_demo.md`。
- 当前开发分支已形成本地整车集成提交
  `46b509a feat: complete assembled 513x chassis integration`，尚未 push、尚未合入 `main`。
  正式 `E:\ECHO` main 另有用户修改，禁止自动合并或覆盖。
- 下一阶段转回已做过临时线路实测的张大头/ZDT Emm 备用步进。旧第一代 `PB15/PB16`
  与正式 ESP32 UART2 冲突；PCB 首测优先使用第二代 `PB2/PB3`，先只读查询再做有界小运动。

## 0.000 正式 513X 装车基线与电压补偿

- 用户确认当前装车状态转为正式基线：513X-4S/GMR 双电机；左编码器 PA29/PA30，右编码器
  PA25/PA24；电池 ADC 分压 300k/33k。当前 IMU 接在 I2C0：SDA=PA0、SCL=PA1。
- 双轮 `60%/1 s` 架空测试确认两轮均机械向前，稳态约 97.65/93.86 rpm，自动归零正常。
  本车前进原始计数为左负、右正，正式 Profile 编码器符号冻结为左 -1、右 +1；电机输出符号
  仍为左 +1、右 -1。
- 513X-4S Profile v13 以 16.580 V 为闭环控制参考电压；速度闭环 PWM 按 `Vref/Vbat`
  自动补偿，接受 12--18 V，采样超过约 0.5 s 未更新即停机，补偿后绝对占空比不超过 90%。
  开环点动维持直接占空比语义。
- v13 右轮前馈按当前装车实测重拟合为 `371.0 + 2.17*rpm`；同方向非零降速时，控制器会
  清除与目标方向相反的旧积分，并在实测速度尚明显高于新目标时冻结反向积分，进入目标附近后
  自动恢复正常 PI。静止起步的双轮 60% 共同助推不变。
- 当前 IMU 已启用并识别为 MPU6050：从地址 `0x68`、`WHO_AM_I=0x68`。10 秒采集得到
  253 帧 IMU 遥测，器件端 100 Hz 连续采样 2770 次、失败 0 次，300 点静止校准完成并进入
  READY；I2C 错误 0。OLED、TFmini、灰度和 ESP 链路保持关闭。
- 全量构建通过：SysConfig 0 error，应用 0 Error/0 Warning；HEX SHA-256
  `ABD6011458614DFD5C8B2AC3FB2821F81BD73C98FB092143F853989F060D92A6`。DAPLink
  `2e4c7219` 已完成 program、逐字节回读校验和 reset；Flash 二进制回读 SHA-256 为
  `2BE288F884E59772E14AAF0330842A7D62E11789287B560146A6EE507C875497`。
- 静态状态为 Profile v13、电池约 16.57 V、执行器关闭，Active/Sticky/I2C/deadline/drop 全零。
  架空闭环 20 rpm 为 19.967/20.314 rpm，60 rpm 为 60.055/60.124 rpm；30->70 rpm
  尾段为 70.216/70.262 rpm，三次均自动归零、设备 Health 全零。
- 当前架空最低连续闭环速度冻结为 8 rpm：静止起步两轮均在 10 ms 检出运动，起步差 0 ms，
  尾段为 8.023/8.049 rpm。`20->8 rpm` 为 t90 360/390 ms、尾段 7.849/7.935 rpm，
  499/499 个降速有效帧两轮均持续运动且无恢复助推；`8->20 rpm` 为 t90 270/260 ms、
  尾段 20.192/20.055 rpm。5 rpm 仍是脉冲蠕行，不属于平滑连续 PI。
- 当前只验收架空速度环。落地直线、负载、电流、温升与不同电池电压尚待实测；IMU 接回并
  自检通过后才能调航向环。

## 0.00 UART2 与 ESP32-S3 双向 DMA 链路

- 独立 `ESP_LINK_UART` 使用 `PB15/UART2_TX -> ESP32 GPIO18/UART1_RX`、
  `PB16/UART2_RX <- ESP32 GPIO17/UART1_TX`，两端共地，最终配置 `230400 8N1`。
- UART2 RX 使用 `DMA_CH1` Full Channel repeat-single 模式连续写 512 B 缓冲；任务根据
  DMA 剩余计数消费数据，硬件回卷不暂停通道。TX 使用 `DMA_CH2`，FIFO empty 触发可避免
  早期 `ONE_ENTRY` 配置在 FIFO 边界少发一个字节。正常路径不再逐字节读写 UART FIFO。
- UART0 type 12 已升级为 schema 2 / 96 B payload，发布 RX/TX DMA 完成、RX 回卷、EOT、
  IRQ、缓冲峰值和 active/busy 状态；`telemetry_capture.ps1` 已支持解码。
- 两套 ESP32-S3 已把 `tools/esp32/espnow_uart_link.py` 持久化为 `main.py`，按本机 MAC
  自动选择主/从角色；ESP-NOW 固定信道 6，并实现 CRC、序号、ACK、15 ms 无线重试、
  去重、MAC 白名单、8 秒 WDT 和接收缓冲异常自恢复。
- 天猛星测试协议每 20 ms 发 16 B PING；同序号最多在 30/60/90/120 ms 重发，160 ms
  才记一次逻辑 timeout。该重试只用于通信测试，后续正式双车消息仍需按业务语义封装。
- SysConfig 0 error / 1 个既有 warning；FreeRTOS/App 全量构建 0 Error / 0 Warning，
  Code=80,748、RO=3,492、RW=188、ZI=20,028，最终 HEX SHA-256
  `B524E493244F2068F17D38026BDAE7C621F79F33CDC92FE300A2DE5A9EBB6459`。同一镜像已通过
  CMSIS-DAP `4CDD...A99B3` 与 `FAED:4873/2dc1718e` 的 500 kHz program/verify/reset。
- 230400 + 三次重发版本的 180 秒端到端测试中，主机新增 1 次逻辑 timeout、从机 0 次；
  两端无线 timeout/invalid/peer error/TX fail/RX drop 均为 0，UART2 RX overflow 与异常 IRQ
  均为 0。按用户要求不继续追求长测绝对零错误。
- 最终四次重发版本 30 秒短测：主机 1,607 TX/1,607 ACK，从机 1,639/1,639；两端新增
  timeout、CRC、格式错误、异常序号、RX overflow、异常 IRQ 均为 0，RX DMA 保持 active，
  TX DMA done 与 EOT 一致。全程未发送电机命令，执行器保持 0。
- 详细记录见 `docs/worklogs/2026-07-25_esp_uart2_bringup.md` 和
  `docs/worklogs/2026-07-25_espnow_dual_board_link.md`。

## 0.0 右编码器引脚迁移

- 当前右编码器接线改为 `E2A->PA25/U6-5`、`E2B->PA24/U6-6`；左编码器继续使用
  `E1A->PA29/TIMG8_C0`、`E1B->PA30/TIMG8_C1` 硬件 QEI。
- E2A 保持 GPIO 上升沿软件 x1，E2B 保持方向输入；原有解码、方向符号、CPR 和速度控制参数不变。
- 红外地址线已迁至 `PB0/PB1/PB11`，因此 PA24/PA25 不再冲突；SysConfig 未发现重复物理引脚。
- SysConfig 为右编码器生成 `GPIOA`、`DL_GPIO_PIN_25/24`、`GPIOA_INT_IRQn` 和
  `DL_INTERRUPT_GROUP1_IIDX_GPIOA`；启动文件与 BSP 均使用 `GROUP1_IRQHandler`。
- SysConfig 为 0 error / 1 个既有 warning；FreeRTOS 与 App 全量构建均为
  0 Error / 0 Warning，HEX SHA-256 为
  `E90D613C8BE05F2E90DA3EA55EE81649FA94713333D767BE2A20F3E45C445CB8`。
- 按用户要求未烧录、未板测；迁移后仍需在电机动力断开时检查 A/B 电平、正反手转计数和静止零漂移。
- 详细记录见 `docs/worklogs/2026-07-20_right_encoder_pin_relocation.md`。

## 0. 八路红外灰度接入

- 513X 已提交为 `c08fa3d`，尚未 push；PID 网页任务暂停且未删除。
- 灰度板当前接线为 `OUT->PA26/ADC0_CH1`、`AD0->PB0/U6-29`、`AD1->PB1/U6-30`、
  `AD2->PB11/U6-33`；用户确认 OUT 不超过 3.3 V。地址线迁移已构建，尚未烧录或按新线序板测。
- 新增非阻塞八路原始值扫描和 UART type 8 telemetry；最终完整扫描 31.25 Hz、原始遥测 15.625 Hz，未加入黑白标定或循迹控制。
- 旧 PA27/PA24/PA25 接线版本曾以 HEX `9EB459E3B1EE6FB0E52DF82936F4EE975178ECB4414ADCC9F676404B13814E16`
  烧录并快速校验通过；该烧录结论不适用于当前 PB0/PB1/PB11 构建。
- 频率探索覆盖 12.5、15.625、20.833、31.25、62.5 和 125 Hz。设备端 ADC timeout/incomplete/deadline/Health 均为零；无线 COM7 错误率随高频地址切换总体上升。
- 旧接线下 31.25 Hz 的 30 秒测试：Control 99.934 Hz、Reflectance telemetry 15.592 Hz、
  2 CRC/3 gap；电机输出保持零。新接线需先复核通道顺序，再用有线 UART 验证频率。
- 详细证据见 `docs/worklogs/2026-07-17_reflectance_bringup.md`。

## 0.1 513X PID 遥测网页更新

- 开发 worktree `C:\Users\Auror\ECHO-513a-work` 已完成 PID 网页与 96 B Control telemetry 扩展，尚未合入 `E:\ECHO`。
- 网页支持左右目标/实际轮速、左右总输出、左右 P/I/D/前馈曲线选择，20 s 同步窗口和暂停显示。
- 网页支持共享 Kp/Ki/Kd 一次应用并逐项显示 ACK/应用序号，参数只写 RAM。
- 网页支持左右独立 `-100..100 rpm` 持续速度；关闭网页不自动停机，只有 `0/0 rpm`、故障或复位停止。
- 513X Profile v5 最终固件已烧录并静态板测通过；非零持续运动验证 deferred。
- 验证证据见 `docs/worklogs/2026-07-17_pid_telemetry_web_console.md`。

## 0.2 513X-4S 15.9 V PID 网页复调

- 当前板上为 Profile ID 5 / `513X-4S v11`；默认 PID 已持久化为
  `Kp=6`、`Ki=8`、`Kd=0`，15.9 V 前馈为左 `388.5 + 1.93*rpm`、
  右 `375 + 2.70*rpm`。升速目标斜坡为 150 rpm/s，降速独立为 90 rpm/s。
- 用户 CSV 中两次 `10 rpm` 起步峰值为 20.04/21.86 rpm，右轮运行中
  `0 -> 60 rpm` 峰值为 69 rpm。独立启动恢复和测量起点跟踪修复后，v9 完整复测的
  `10 rpm` 峰值均为 12.43 rpm，右轮 `0 -> 60` 峰值 61.71 rpm、t90 260 ms。
- 最终 `Kp=6` A/B：`30 -> 60` t90 320/190 ms、峰值 61.82/62.16 rpm；
  `60 -> 30` t90 550/450 ms、最低 27.10/27.00 rpm；`30 -> 70` t90 300/250 ms、
  峰值 71.46/72.00 rpm，尾段 70.04/70.10 rpm。
- 前馈重拟合后 70 rpm 稳态积分由旧 CSV 约 `+38/+47 permille` 降到
  `+1.1/-4.1 permille`。`Kd=0.1` 未改善右轮降速下冲且拖慢左轮升速，已拒绝。
- 修复速度模式停机后 Control telemetry 字段退回原始编码器计数、网页仍按 rpm/PWM 绘制的
  语义错误；最终停机曲线无虚假尖峰，最后 20 帧目标、速度、输出全零。
- Keil App 0 Error / 0 Warning；最终 HEX
  `E9E9EE2756116538987D83AABDC3DDAB306ECE5596BA39A0C73B5E98E622F839` 已通过
  CMSIS-DAP `2b5d6f2a`、500 kHz 烧录、快速校验和复位。
- 冷启动 apply sequence 0 即报告 `6/8/0`；Health active/sticky/deadline 为零，
  `ActuatorOutputPermitted=0`。落地直线、带载、电流和温升仍未验收。
- 主机 UART 仍受红外地址切换电气耦合影响；最终 Kp A/B 采集有 38 个主机 CRC 错误，
  设备端 active/sticky/deadline 和输出收尾均正常。
- 详细证据见 `docs/worklogs/2026-07-19_513x_4s_pid_web_retune_15v9.md`。

## 1. 权威仓库

- 唯一正式工程：`E:\ECHO`
- 正式分支：`main`
- 最近已验收阶段：Phase 1F
- 最近已验收固件：本文件所在的 Phase 1F 提交
- 阶段标签：`phase-1f-operability-diagnostics`
- 基线父提交：`cb7c4c32783cf7eeeabbbdec4a193aee99077159` / `phase-1e-oled-ui`
- 远端 `origin/main`：`4b1a3db` / Phase 1F，已 push

Phase 1E 与 Phase 1F 已合入正式 main 并推送至 GitHub。Phase 2A 已从正式 Phase 1F 基线
创建独立分支/worktree，完成成果已推送至 `origin/phase-2a-at8236-chassis-encoder`：

```text
branch:   phase-2a-at8236-chassis-encoder
worktree: C:\Users\Auror\ECHO-phase2a-work
start:    4b1a3db / refs/tags/phase-1f-operability-diagnostics
```

Phase 2A 已开始。左右编码器无动力板测已通过；AT8236 默认零输出和 UART 一次性点动安全层
已完成 FreeRTOS/App 0/0 构建、烧录回读校验及四方向 5%/200 ms 无动力逻辑 PWM 板测。
共享底层的 MG370/513A 编译时 Motor Profile 已实现并完成 MG370 0/0 构建，但新 Profile 固件
尚未烧录。物理 PWM 波形、带 VM 单轮点动、方向冻结、故障测试和连续运行尚未完成。

## 2. 必须保留的用户状态

正式工作树原有 4 个用户文件，阶段合入时必须保持其语义，不得自动暂存、覆盖或还原：

```text
ECHO.uvmpw
freertos/keil/freertos_ECHO.uvprojx
keil/ECHO.uvprojx
tools/telemetry-web/README.md
```

Phase 1F 需要在 `keil/ECHO.uvprojx` 中登记 `system_health.c` 和 `bsp_reset.c`。正式合入必须
只增加这两个 `<File>` 节点，同时保留用户已有路径/配置变化，禁止整文件覆盖。

安全恢复点仍保留：

- `stash@{0}: pre-phase1e-user-protected-changes`
- `C:\tmp\ECHO-phase1e-merge-backup-20260715`

不得自动 pop/drop stash，不得删除备份。

## 3. 已完成阶段

| 阶段 | 提交 | 标签 | 结论 |
| --- | --- | --- | --- |
| 1A | `f3a4552` | `phase-1a-baseline` | 可移动工程、Keil/VSCode/DAPLink 链通过 |
| 1B | `212513a` | `phase-1b-rtos-skeleton` | 静态 RTOS 骨架、hook、栈/堆/故障诊断通过 |
| 1C | `cc4b52f` | `phase-1c-clock-timebase` | 80 MHz、1 MHz 时基和 100 Hz 时序诊断通过 |
| 1D | `e7a1ac7` | `phase-1d-telemetry-tuning` | UART DMA 遥测、网页曲线和 RAM 参数协议通过 |
| 1E | `cb7c4c3` | `phase-1e-oled-ui` | SSD1306 UI、I2C 超时恢复和 UART quiet window 通过 |
| 1F | 本文件所在提交 | `phase-1f-operability-diagnostics` | 统一健康、五页 UI、参数元数据、诊断工具与低功率终验通过 |

Phase 1D 仓库没有保存原始串口日志或具体验收数字，后续仍不得补写。Phase 1F 的数字来自
本次保存的 ignored JSON/CSV 和 RAM 快照，摘要见 Phase 文档与 worklog。

## 4. 当前固件行为

- Phase 2A worktree 的当前固件已覆盖下列 Phase 1F/早期 Phase 2A 描述：SystemTask 100 Hz 已运行真实双轮速度控制，不再发布纯测试信号。
- MG370 Profile v8：左/右方向 `+1/-1`，CPR `68028/17007`，速度上限 120 rpm，PWM 上限 900 permille。
- `>=8 rpm` 使用位置式 PI（`Kp=8`、`Ki=18`、`Kd=0`）与每轮前馈；`<8 rpm` 使用短启动脉冲、滞环驱动/滑行和近零 kick/rest 蠕行。
- UART speed 命令支持运行中在 SystemTask 周期边界重定向；命令结束、调度异常和 RTOS fatal 仍强制双路归零。
- Health schema v2 公开右编码器 ISR late 总数；late 边沿沿用最近可靠方向，持续密集迟到才产生 degraded issue。
- MCLK 80 MHz，FreeRTOS Tick 1 kHz，TIMG12 标称 1 MHz、32 位向上计数。
- SystemTask 100 Hz；当前仍发布测试信号，不是真实电机 PID。
- ServiceTask 2 ms 服务 UART/参数，并每 100 ms 成为健康快照的唯一写入者。
- TelemetryTask 非阻塞发送 100 Hz Control、1 Hz Health 和参数 ACK；UART1 PA8/PA9、230400 8N1。
- Health 快照包含 schema、身份、uptime/reset reason、overall/active/sticky/first fault、任务栈、
  deadline、UART/Telemetry、参数、OLED/I2C、future sensor/actuator 安全占位。
- OLED 为 Overview、RTOS、COMM、DEVICE、PARAM 五页；虚拟输入支持 press/long/repeat/timeout。
- `kp/ki/kd/target` 共用一个 metadata 表；UART/OLED 只 staging，SystemTask 周期边界应用。
- 启动最早期通过 BSP 只读捕获 reset cause；当前执行器 armed/output permitted 始终为 0。
- 当前已有左右编码器诊断采样和受限 UART 单次 PWM 命令；默认输出为零，尚无带动力运动证据。
- 执行器命令固定为 CRC、双 magic、单电机、最高 10%、最长 500 ms；SystemTask 唯一写输出。
- 电机型号由 `ECHO_MOTOR_PROFILE_SELECTION` 编译时选择；默认 MG370 v1，禁止 OLED 运行时切换。
- MG370 每轮 Profile 固定左 x4/+1/68,028、右 x1/-1/17,007；电机输出符号和闭环限制仍待实测。
- 513A 关键参数未确认，选择它会产生明确编译错误；不会生成可驱动固件。另一套 513 电机
  保留名称 `513B`，当前不共享或预填 513A 参数。

## 5. Phase 1F 最终构建与实测

| 项目 | 结果 |
| --- | ---: |
| FreeRTOS full rebuild | 0 Error / 0 Warning |
| App full rebuild | 0 Error / 0 Warning |
| 程序尺寸 | Code=53048, RO=2864, RW=28, ZI=15956 |
| HEX SHA-256 | `1A205780BF54C948915A7D29E1DC6C240912C4A4FE4A95499D4B093FF25D3157` |
| DAPLink/OpenOCD | program/verify/reset passed |
| 120 秒 Control / Health | 12194 / 121，100 Hz / 1 Hz |
| 120 秒 CRC / gap / deadline / drop | 0 / 0 / 0 / 0 |
| 120 秒周期 / max execution / jitter | 9998-10002 / 23 / 2 us |
| 120 秒 I2C / OLED / quiet | 43378 success、0 error、271 refresh、271/271 |
| 10 分钟 Control / Health / total | 60953 / 610 / 61563 |
| 10 分钟 CRC / gap / deadline / drop | 0 / 0 / 0 / 0 |
| 10 分钟 I2C / OLED / quiet | 234578 success、0 error、1466 refresh、1466/1466 |
| quiet max / active | 38529 us / 0 |
| TX ring high-water | 280 B |
| 六任务最小剩余栈 | 180 / 128 / 151 / 102 / 104 / 104 words |
| heap minimum | 3064 B |
| Health / active / sticky / actuator permitted | OK / 0 / 0 / 0 |

最终已 `reset run`，3 秒干净状态复核为 parameter sequence=0、parameter errors=0、Health OK、
active/sticky=0、OLED online、输出门锁定。

## 6. Phase 1F 硬件门禁

| 门禁 | 状态 | 说明 |
| --- | --- | --- |
| 物理 ADC 五键 | deferred | 无电阻梯形、引脚冻结和 ADC 实测分布；虚拟输入契约已通过 |
| Flash 掉电保存 | deferred | scatter 未保留参数槽；未执行 Flash 擦写或损坏注入 |
| 硬件看门狗 | deferred | 未完成 owner/window/debug halt/reset cause/执行器安全板测，默认未启用 |

这些 deferred 不允许被描述为通过。它们不阻塞 Phase 2A 的独立台架实现，但真实电机输出前
必须重新审查安全影响；物理菜单、赛场掉电保存和看门狗分别在对应硬件准备完成后闭环。

## 7. 已知风险

- OLED 仍可能是临时杜邦线；正式整车需短线、可靠共地、核实上拉和去耦。
- 两块天猛星中异常发热的板继续禁用。
- OpenOCD 目标端 CRC 算法偶尔无法 halt；脚本的逐字节 Flash 回读 SHA-256 回退已通过。
- COM 号会变化；工具已要求显式 `-Port`，不能把本次 COM4 当永久事实。
- Health frame 已扩展为 132 B，SerialTx 原子写上限同步提高到 160 B；主机解析器兼容旧 112 B 和新 116 B Health payload。
- AT8236 逻辑安全层已建立唯一写入者、默认零输出和限时单次命令，但物理波形与带载失效状态未验收。
- MG370 的起转/最大 PWM、速度/加速度限制、堵转保护阈值和 PID 尚未冻结，闭环输出接口保持锁定。

## 8. 下一步：Phase 2A

Phase 2A 范围仅限 AT8236、底盘电机和 GMR 编码器。开始前：

1. 已确认正式 main 安全合入 Phase 1F，并从该标签创建独立分支/worktree。
2. 记录电机额定电压/电流、减速比、轮径、轮距和编码器计数定义。
3. 用户再次确认现场安全后烧录 MG370 Profile 固件，VM 断电复核 1 Hz Profile 遥测和四路静止低。
4. 用户在场、单轮架空、另一电机断开、限流和物理断电确认后，先做左轮 5%/200 ms 点动。
5. 冻结左右电机方向与运行限制后完成故障、连续运行、最大编码器速率、电流和温升验收。

接手清理已更新工作流程和架构状态，并加固 Phase 1F field check 的累计 I2C 与 UART/OLED
quiet-window 门禁。这些维护不代表 Phase 2A 功能已经实现。

当前左轮编码器配置为 D153B E1A/E1B -> PA29/PA30 TIMG8 硬件 QEI。用户确认实际信号为
3.3 V；向前原始计数为正，provisional 输出轴 CPR 为 68,028，闭环前仍需多圈精确标定。

不得提前混入底盘 IMU、云台或树莓派功能。长期阶段顺序见 `docs/CURRENT_WORKFLOW.md`。

## 9. Phase 2A 当前确认基线（2026-07-16）

| 项目 | 结果 |
| --- | --- |
| 当前板上 HEX | `BB4421DBC943CA4A1EDF7CE6B48448BB5106D8F342FCC1B0BC85465DE0B1E5A5` |
| 构建 | FreeRTOS/App full rebuild 0 Error / 0 Warning；Code=64,768，ZI=16,980 |
| 5 rpm 蠕行 | 10 秒完整；1 秒窗口左 `5.21–5.81`、右 `4.85–5.52 rpm`；Health clean |
| 8 rpm PI 边界 | `8.010/7.991 rpm`；Health clean |
| 60 / 100 rpm | `59.959/60.025`、`100.019/100.018 rpm`；Health clean |
| 120 rpm 连续 | 30 秒、3000/3000；`120.012/120.011 rpm`；约 0.408 m/s；Health clean |
| 10 -> 120 rpm | `t90=280/280 ms`，skew 0 ms，overshoot `7.57%/8.61%` |
| 120 -> 10 rpm | `t90=540/530 ms`，skew 10 ms，同方向降速禁止反向驱动 |
| 147 rpm 探索 | 不采用；编码器 late=217 且闭环模型超出线性区，比赛上限降为 120 rpm |
| 人工阻力扰动 | not run；用户确认上一组没有施加外力，1 CRC / 1 gap 仅为主机串口偶发 |
| 最终静止收尾 | 511 Control / 5 Health / 5 Profile；左右 delta=0；CRC/gap/deadline/active/sticky/I2C=0；output permitted=0 |

当前 120 rpm 上限仅代表轮组架空、12 V、约 0.5 A 限流和现有线材条件。轮组落地后的电流、温升、
直线同步和抗扰仍需独立验收。正式工程 `E:\ECHO` 未修改，当前 worktree 未自动提交、暂存或 push。

## 10. Phase 2A 速度控制更新（2026-07-16 18:26）

- 当前板上固件：MG370 Profile v13，HEX
  `43F7F69B207BA39BBF5C847B935DD05E4EADD682780F1B982CF6C6A3291B5FAF`。
- full rebuild：FreeRTOS/App 0 Error / 0 Warning，Code=65,200，ZI=17,020；DAPLink program/verify/reset passed。
- 普通区 PI 仍为 `Kp=8/Ki=18/Kd=0`；新增有资格门控的负载释放快速退积分，目标变化不会触发该路径。
- 正常启动 50%，机械死点恢复 60% / 最长 80 ms；软件速度/PWM 上限仍为 120 rpm / 900 permille。
- v13 `120->10 rpm`：`t90=550/540 ms`，尾段 `9.956/9.979 rpm`；v13 `10->120 rpm`：`t90=270/280 ms`，超调 `7.97%/7.64%`。
- v13 最终单左轮人工扰动通过：左轮最低 `45.422 rpm`，PWM `507.9->670 permille`，松手峰值 `67.384 rpm`（`12.31%`），820 ms 内进入并持续保持目标 ±3%；右轮均值 `59.999 rpm`。
- 最终静止收尾为 512 Control / 5 Health / 5 Profile，CRC/gap/deadline/drop/I2C/active/sticky/encoder late 全零，输出门禁为 0。
- 当前测试命令已结束并自动归零。正式 `E:\ECHO` 仍未修改；未自动暂存、提交或 push。
- 阶段状态：架空直流电机与编码器调试完成。电流、温升、落地负载、实际补偿、直线同步和带载速度上限 deferred 到后续阶段。

## 11. 513X profile correction and validation (2026-07-17)

- The newly tested motor was corrected from the temporary name 513A to 513X.
- Active profile: ID 2, Model 513X, version 4. IDs 3 and 4 are reserved for
  independent 513A and 513B profiles and remain compile-locked.
- 513X uses GMR 500 PPR, provisional CPR 56000/14000, left/right forward motor
  signs +1/-1, 600 permille startup, 650 permille maximum output, and a 70 rpm
  software limit.
- Closed-loop defaults are Kp=3, Ki=8, Kd=0 and are restored from the selected
  profile after reset.
- Final 70 rpm / 30 s validation passed at 70.014/70.012 rpm with 3000/3000
  frames and clean CRC/gap/deadline/encoder-late/Health diagnostics.
- Final HEX SHA-256:
  `EA016F7F9D6A9B093C978E37359AD7DED31EC9FCB172D4B4D9D7871BEADBA1F9`.
- Control telemetry now includes both left and right normalized PWM with legacy
  40-byte control-frame parser compatibility.
- Valid 60 rpm load tests passed on both wheels. Left load: minimum 18.853 rpm,
  230 ms recovery, 5.01% overshoot. Right load: minimum 27.857 rpm, 150 ms
  recovery, 5.71% overshoot. The untouched wheel stayed near 60 rpm in each run.

## 12. 513X 4S battery profile (2026-07-18)

- The original `MG513X v5` 12 V profile is preserved as profile ID 2. The active build now uses independent `513X-4S v3`, profile ID 5.
- The old profile failed on 4S at a 30 rpm command: approximately 37.1/35.1 rpm with both integrators saturated at -90 permille.
- The final 4S profile retains 600 permille startup and 650 permille maximum output, while steady feedforward is left `409 + 1.09*rpm` and right `401 + 1.62*rpm`.
- Final 30 rpm tail mean is 30.865/30.111 rpm. Final 30 to 70 rpm step has t90 670/600 ms, peak 73.286/71.143 rpm, and 70 rpm tail mean 70.129/70.059 rpm.
- All final device-side Health, deadline, DMA stall, encoder-late, and output-residue checks passed. The final HEX SHA-256 is `D110D1B5DAC13BEDFFC3744CF93412CA949CEF1FC9267713D58061968F7BF0F2`.
- Host UART is not production-clean during combined motor and 125 Hz reflectance operation: 40 CRC/gap errors in the final step and 63 in the final 30 rpm run. Hardware damping and wiring correction remain required.
- Details: `docs/worklogs/2026-07-18_513x_4s_battery_validation.md`.

## 13. 4S supply-voltage ADC bring-up (2026-07-19)

- Supply sensing is implemented on `PB17 / ADC1_CH4`, independently of the
  reflectance input on `PA26 / ADC0_CH1`.
- The required external network is battery positive -> `100 kohm` -> PB17,
  PB17 -> `22 kohm` -> GND, and PB17 -> `100 nF` -> GND. Battery ground and
  controller ground must be common. Never connect the 4S positive terminal
  directly to PB17.
- Firmware samples at 100 Hz, applies a 1/8 IIR filter, and publishes UART type
  9 voltage telemetry at 10 Hz. The conversion formula is
  `raw / 4095 * 3.3 * 122 / 22`.
- Full App rebuild passed with 0 errors and 0 warnings. HEX SHA-256:
  `6BC7F10A6E4A4C1EC118E01DF2B7931DBE409B945A5F398E0238AAC5B5DB2E2C`.
  DAP programming at 500 kHz passed byte-for-byte Flash readback verification.
- The first 10-second static capture received 102 voltage frames at 10 Hz and
  confirmed a 100 Hz ADC sample rate with zero conversion timeouts, zero
  deadlines, zero device-side drops, and actuator output disabled.
- The measured PB17 input was only about 0.58 V, producing an invalid 3.17-3.39
  V battery estimate. This is not a valid 4S reading and indicates that the
  divider is absent, incorrectly wired, or not connected to the battery.
- A subsequent direct low-voltage input test passed: a nominal 1.5 V cell
  applied to PB17 through a 10 kohm safety resistor produced raw 1667-1687,
  average raw 1678.824, and approximately 1.353 V at the ADC input. The 10-second
  peak-to-peak input variation was approximately 16 mV, with zero conversion
  timeouts. The displayed 7.50 V battery value is expected because firmware is
  already configured for the future 100k/22k divider.
- Final voltage accuracy remains uncalibrated until the divider is physically
  verified and a simultaneous multimeter battery reading is supplied.
- Detailed evidence: `docs/worklogs/2026-07-19_supply_voltage_adc_validation.md`.

## 14. 513X-4S PID tuning (2026-07-19)

- `513X-4S` Profile 已从 v3 更新为 v4，冷启动默认 PID 为 `Kp=4`、`Ki=10`、`Kd=0`；12 V `MG513X v5` 未修改。
- 本次 Profile、断言和调参记录已提交为 `294893d`，尚未 push。
- 双轮 `30->70 rpm` 的 t90 为 570/580 ms，峰值 71.679/71.150 rpm，尾段 70.004/70.059 rpm。
- 双轮 `70->30 rpm` 的 t90 为 620/620 ms，下冲 8.84%/4.29%，尾段 30.09/30.32 rpm。
- `Ki=12/14` 与 `Kd=0.1` 均经实机 A/B 后拒绝；继续增大积分会放大降速下冲。
- Keil App 构建 0 Error / 0 Warning；HEX SHA-256 `6642AE8A449EDFA9C6FA09C44501B7B458E3814838A417242089BD2E4715ADF8` 已通过 CMSIS-DAP 烧录、校验和复位。
- 冷启动遥测确认 Profile ID 5 / v4、默认 `4/10/0`、左右输出全零、Health active/sticky/deadline 全零。
- 主机端仍有红外切换耦合造成的 CRC/gap；落地负载、直线同步、电流和温升 deferred。
- 详细证据：`docs/worklogs/2026-07-19_513x_4s_pid_tuning.md`。

## 15. TFmini-S 激光测距接入（2026-07-19）

- 照片板内丝印为 `TFmini V1.8.1`；外形、线序、9 字节测距帧和实机 `0x5A` 回包确认
  按 TFmini-S 驱动。固件版本原始 `07 03 02`，显示为 `2.3.7`。
- 当前 UART1 为 PA8 TX、PA9 RX、115200 8N1；红线必须使用 5.0 V +/-0.1 V，黑线共地，
  黄线 RXD、绿线 TXD，逻辑电平 3.3 V LVTTL。
- 新驱动使用中断环形缓冲和 ServiceTask 流式解析，不新增任务；支持距离、强度、芯片温度、
  帧率、无效状态、校验错误、超时和固件版本诊断。UART0 type 10 以 20 Hz 对外发布。
- FreeRTOS/App full rebuild 0 Error / 0 Warning；76,056 B Flash 逐字节回读 SHA-256
  `7B5FE3ABA8FD12EE37E82819C0FD85A7A8E3F16E61EBE3F93EFA64F64E2A29D9` 一致；
  HEX SHA-256 为 `9FDEE670AA28F049945DE41AE510654FC9CF010BF8A656B792947F25AA463DFF`。
- 20 秒无断点实测为 2007/2007 有效帧、0 无效、0 校验、0 超时、0 UART 溢出，设备帧率
  约 97.9 Hz；SystemTask/ServiceTask 为 2041/20415，deadline/fault 为 0，电机输出全零。
- 10 个一秒静态样本为 103–104 cm，强度 2328–2337，芯片温度 56.00 C。
- TFmini-S 支持 I2C 0x10 / 400 kbit/s，但 PA8/PA9 没有硬件 I2C 复用；未来推荐迁移到
  PA0/PA1 的 I2C0 与 OLED 0x3C 共总线。当前未切换或保存 I2C 设置。
- 详细接线和协议：`docs/hardware/TFMINI_S_LIDAR.md`；过程记录：
  `docs/worklogs/2026-07-19_tfmini_s_uart_bringup.md`。

## 16. TFmini-S I2C + OLED + MPU6xxx 联合驱动（2026-07-19）

- TFmini-S 已通过一次性 UART 命令永久切换并保存为 I2C 模式；当前地址 `0x10`，
  与 OLED `0x3C`、MPU6xxx `0x68` 共用 `PA0/PA1 @ 400 kbit/s`。
- 初始 I2C 全低的根因是 SCL 误接 GND；改回 `PA1/SCL` 后，OLED 与两类传感器均正常。
- TFmini 写取数命令后保留手册要求的 1 ms 延时；MPU 寄存器读取使用无额外延时 API。
- 实机 `WHO_AM_I=0x70`，按 MPU6500-compatible Profile 驱动；配置 100 Hz、+/-500 dps、
  +/-4 g、42 Hz DLPF，启动静止校准 300 点。
- 新增 type 11 IMU 遥测与 issue 19/20 `IMU_OFFLINE/IMU_STALE`；上位机工具可报告
  IMU 遥测率、实际采样率、三轴数据、温度、状态、地址和 `WHO_AM_I`。
- UART DMA 改为 ServiceTask 统一启动且单块不超过 160 B；OLED 刷新改为可续传短块，
  共享总线下不会再用约 40 ms 整屏事务挤掉 IMU 采样。
- 最终 60 秒：IMU `100.000 Hz`、TFmini `49.993 Hz`；IMU sample failure 0，TFmini
  checksum/timeout 0，I2C error 0，OLED online/持续刷新，Health active/sticky 0，
  deadline 和设备端 drop 0，actuator output permitted 0。
- FreeRTOS/App 全量构建 0 Error / 0 Warning，Code=77,372，ZI=19,044；最终 HEX SHA-256
  `FC49A2B41F82353129A71677E0ECADA2CA4EC5FF688FD16A61D199180220386A`，
  CMSIS-DAP `2b5d6f2a` / 500 kHz program/verify/reset 通过。
- 主机 COM9 仍出现 CRC/gap，设备端错误计数为 0，结论仍指向红外地址线切换的电气串扰；
  TFmini 当前对着盲区目标读 0 cm，非零 I2C 距离值仍需移动目标后补测。
- 详细记录：`docs/worklogs/2026-07-19_tfmini_i2c_oled_mpu_combined.md`。

## 17. 红外复用地址线迁移（2026-07-20）

- 八路红外灰度板的模拟输出继续使用 `PA26 / ADC0_CH1`，未改动 ADC 采样链路。
- 74HC4051 地址线改为 `AD0->PB0/U6-29`、`AD1->PB1/U6-30`、
  `AD2->PB11/U6-33`，释放原 `PA27/PA24/PA25`。
- 三根地址线位于同一 GPIOB，现有 `bsp_reflectance` 继续使用一次掩码写入。
- SysConfig 生成确认端口与位号正确；FreeRTOS/App 全量构建 0 Error / 0 Warning。
- 当前 HEX SHA-256 为
  `61F45C67FE54B046A767785E6619B3E8D09D572A8AF839605001810A3CBF675B`。
- 本次尚未烧录或板测，新接线状态为“已配置，待换线验收”。

## 18. 2026-07-27 MPU6050 static precision update

- The installed MPU6050 was characterized with complete unfiltered 100 Hz
  static telemetry. Initial 60.731-second integrated drift was
  `0.0593/0.0648/0.6266 deg` on X/Y/Z.
- A motion-gated stationary bias tracker reduced the comparison drift to
  `0.0348/-0.0554/0.0372 deg`, a 94.1% Z-axis improvement. It cannot update
  while actuator output is active or either wheel is moving.
- Device sampling remains 100 Hz, the software gyro low-pass remains about
  25 Hz, formal control telemetry is 100 Hz, and formal IMU telemetry is 25 Hz.
- Acceleration norm averaged `1.0938 g`; six-face calibration is required and
  no unsafe one-pose scale correction was applied.
- Final HEX SHA-256 is
  `210D6AC1C67730664BA04C4B1EDD26C74559E03F9345CD9FF3CB39924D1E2748`.
  Final device health, I2C, timing, drops, encoder-late count, and motor output
  are clean.

## 19. 2026-07-27 MPU6050 temperature-ramp decision

- A 910.36-second hair-dryer ramp covered die temperature
  `29.927 -> 39.451 -> 33.730 C` and produced 21175 valid raw IMU frames.
- Device sampling stayed at 100 Hz with zero sample failures, I2C errors,
  health issues, deadlines, or actuator output.
- Temperature sensitivity is real, but it is not yet a usable single model.
  Z-axis heating/cooling slopes were `-0.006226/-0.012315 dps/C`; matched
  temperature bins differed by `0.017--0.050 dps`.
- Formal firmware therefore keeps temperature compensation disabled and uses
  the validated motion-gated stationary bias tracker. At room temperature it
  reduced measured Z drift from `0.6266 deg` to `0.0372 deg` per approximately
  60.7 seconds.
- Use rule: leave the chassis stationary for 30--60 seconds after power-on.
  Next priority is six-face accelerometer calibration, followed by the 100 Hz
  attitude/angle estimator. Temperature compensation requires a repeatable
  second slow ramp before reconsideration.

## 20. 2026-07-27 installed MPU6050 six-face calibration

- Accepted six-face means produced accelerometer bias
  `0.008252/-0.013179/0.078644 g` and scale
  `1.000363/0.997827/0.992018` on X/Y/Z.
- Offline six-pose norm range improved from `0.930984--1.089627 g` to
  `1.000289--1.002875 g`; maximum error was about 0.29%.
- Fresh normal-pose hardware validation measured mean norm `1.00409 g` and
  latest norm `1.00019 g`, reducing the previous approximately 8.96% magnitude
  error to about 0.41%.
- Calibration is specific to the installed sensor and lives in
  `config/vehicle_bringup_config.h`. Recalibrate after replacing or remounting
  the IMU.
- Final build, DAPLink program/verify/reset, device health, I2C, timing, drops,
  and actuator safety checks passed. HEX SHA-256 is
  `F8333A274EB99B51C327469AA3171327D03E905E81AFB35CD94361B3129AA3EF`.
- Next step is the 100 Hz attitude/angle estimator and static/dynamic angle
  validation before closing the chassis angle loop.

## 21. 2026-07-28 OLED TUNE and hardware self-check

- TUNE now renders signed values and PWM percentages with the SSD1306 `+` and
  `%` glyphs. PWM remains display-only final actuator output after all control
  processing and safety gating.
- HEALTH has a long-OK `>SCAN` entry for a 1.5-second read-only ZDT discovery.
  `IR:OK MASK:255` is based on the last completed eight-channel scan, not a
  transient partial scan. ZDT1 is `NA` by design because UART2 is the formal
  ESP32 link; ZDT2 is on UART3 PB2/PB3.
- Final formal firmware build: 0 errors / 0 warnings. DAPLink program,
  byte-for-byte readback, and reset passed. Flash readback SHA-256:
  `4B2677D726BD7A964EFE0945A7840065D337CEF45B05FEF75EC3A0E21D9548D4`.
- Final device acceptance: control/IMU/attitude 100 Hz, reflectance 125 Hz,
  no CRC/gap/deadline/drop/I2C errors, active/sticky health masks zero,
  actuator output disabled, and display stack free 277 words.
- A scan can show `FAIL` while the ESP32 is physically absent because the
  formal configuration requires the ESP link. The current observed ESP state
  was `LinkOnline=false`, RX bytes 0, with the infrared and ZDT2 paths healthy.
- Final ZDT exit verification returned `BackendSelected=false` and
  `ShutdownPending=false`; the unavailable UART2/ZDT1 port is ignored by the
  TX-idle completion gate.

## 22. 2026-07-31 UART1 vision restart robustness

- UART1 vision now recovers from either device restarting first. A camera
  restart resets its packet counter to zero; after a stream timeout the MCU
  now accepts the first valid packet as the new sequence baseline.
- The PA9 RX path has an internal pull-up, bounded multi-event ISR draining,
  a 1 ms FIFO polling fallback, and automatic UART1 recovery without changing
  UART0, UART2, UART3, line-following, motor control, or H3 control gains.
- Camera-only restart was accepted immediately from old sequence 9923 to new
  sequence 1 without resetting the MCU. The reverse order also recovered and
  accepted the already-running camera at sequence 30285.
- Final captures measured camera 58.7--58.9 fps and MCU control 98.4--98.8 Hz,
  with CRC 0, out-of-order 0, deadline misses 0, and H3 fault 0. A missing ball
  correctly leaves `control_valid=false`; this is not a UART link failure.
- Full builds passed with 0 errors / 0 warnings and the final programmed image
  passed byte-for-byte readback. Detailed evidence is in
  `docs/worklogs/2026-07-31_uart1_camera_powercycle_recovery.md`.
