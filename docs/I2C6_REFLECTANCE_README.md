# 6 路 I2C 巡线模块替换灰度传感 README

这个分支提供“6 路 I2C 巡线模块”的正式驱动，并额外提供一个
`bsp_reflectance.h` 兼容后端。队友整合时可以沿用原来的
`BSP_Reflectance_*` 接口，把原灰度传感替换成新 I2C 模块，不需要改
底盘 PID、H 任务、OLED、UART、IMU 或任务状态机。

## 拉取分支

```text
codex/i2c-six-channel-driver
```

主要文件：

```text
module/device/line_follower_6ch.h
module/device/line_follower_6ch.c
module/device/line_follower_6ch_config.h
bsp/source/bsp_reflectance_i2c6.c
tests/device/line_follower_6ch_test.c
docs/worklogs/2026-07-30_line_follower_6ch_i2c_driver.md
```

本分支不提交 Keil 本地工作区文件，不提交 `ECHO.uvmpw`、`.uvoptx`、
`Objects/`、`Listings/` 等本地生成文件。

## 模块信息

- 模块：Hiwonder 6 路 I2C 巡线模块，例程名 `LineFollowerLearn6CH`
- 供电：`5 V`
- 电流：约 `85 mA`
- 7-bit I2C 地址：`0x5C`
- 正式车 I2C 引脚保持不变：`PA0=SDA`，`PA1=SCL`
- 这条 I2C 总线同时挂 OLED 和 IMU

注意：不要把 5 V 上拉直接接到 3.3 V MCU 的 I2C 总线上。上正式车前要
确认模块 I2C 电平行为，必要时加电平转换。

## 寄存器表

```text
5      数字量掩码，uint8_t
6      通道 1 原始值，uint16_t，小端
8      通道 2 原始值，uint16_t，小端
10     通道 3 原始值，uint16_t，小端
12     通道 4 原始值，uint16_t，小端
14     通道 5 原始值，uint16_t，小端
16     通道 6 原始值，uint16_t，小端
18     通道 1 阈值，uint16_t，小端
20     通道 2 阈值，uint16_t，小端
22     通道 3 阈值，uint16_t，小端
24     通道 4 阈值，uint16_t，小端
26     通道 5 阈值，uint16_t，小端
28     通道 6 阈值，uint16_t，小端
```

目前资料没有提供芯片 ID 或固件版本寄存器，所以驱动初始化只能确认
`0x5C` 地址和上述寄存器可读，不能额外读取芯片 ID。

## Keil 替换步骤

在队友机器上的 Keil target 里做最小替换：

1. 从工程里移除或 exclude `bsp/source/bsp_reflectance.c`。
2. 加入 `bsp/source/bsp_reflectance_i2c6.c`。
3. 加入 `module/device/line_follower_6ch.c`。
4. 现有业务代码继续 include `bsp_reflectance.h`，不用改接口。

不要同时编译 `bsp_reflectance.c` 和 `bsp_reflectance_i2c6.c`。两个文件
故意导出相同 BSP 符号：

```c
void BSP_Reflectance_Init(void);
bool BSP_Reflectance_Service(bsp_reflectance_sample_t *sample);
volatile bsp_reflectance_diagnostics_t g_bsp_reflectance_diag;
```

新后端会读取 6 个物理通道，并线性展开成旧控制层使用的 8 个灰度位置，
所以 HMission、遥测、OLED 显示和原来的灰度处理入口可以继续使用
8 通道数据结构。

## 通道方向

默认方向在 `module/device/line_follower_6ch_config.h`：

```c
#define LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER \
    LINE_FOLLOWER6_CHANNEL_ORDER_1_TO_6
```

如果实际安装方向相反，只改这个宏：

```c
#define LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER \
    LINE_FOLLOWER6_CHANNEL_ORDER_6_TO_1
```

不要为了反向去改 PID、循迹、H 任务、OLED、UART、IMU 或底盘控制代码。

## 运行行为

- 每次 `BSP_Reflectance_Service()` 最多做 1 次 I2C 事务。
- 完整帧周期默认 `8000 us`，对应旧灰度扫描的约 125 Hz。
- 模块没接、初始化失败或掉线时，驱动报告 `online=0`。
- 离线重试间隔 `500 ms`。
- 重连后 `reconnect_count` 增加，并恢复采样。
- 驱动不会触发急停，不会刷屏输出，不会主动改任务频率。

诊断信息通过 `LineFollower6_GetSnapshot()` 获取，包括：
`online`、`initialized`、`sample_count`、`success_count`、`failure_count`、
`last_i2c_result`、`last_register`、`last_error`、阈值计数、重连计数和
离线计数。

## 校准要求

替换旧灰度传感后必须重新采集反射率校准。虽然旧 8 通道 API 保留了，
但新模块的物理间距、光学响应和原始值范围都可能不同。

## 本地测试

在仓库根目录运行：

```powershell
gcc -std=c99 -Wall -Wextra -Werror `
  -Imodule/device -Ibsp/include `
  tests/device/line_follower_6ch_test.c `
  module/device/line_follower_6ch.c `
  bsp/source/bsp_reflectance_i2c6.c `
  -o .\line_follower_6ch_test.exe
.\line_follower_6ch_test.exe
```

期望输出：

```text
line_follower_6ch_test: PASS
```

临时 Keil 替换工程已验证：

```text
freertos_ECHO: 0 Error(s), 0 Warning(s)
ECHO:         0 Error(s), 0 Warning(s)
```

正式提交没有修改 Keil 工程文件，所以 main 车默认行为不变。队友需要在
自己的 Keil target 中按上面的步骤替换源文件。

## 上车前硬件检查

1. I2C 扫描确认 `0x5C` ACK。
2. 读取寄存器 `5`、`6..17`、`18..29`。
3. 逐个探头压黑线/白底，确认 6 路原始值变化。
4. 确认物理左右方向，必要时只改方向宏。
5. 连续读取几分钟，记录频率、错误数、掉线/重连表现。
6. 拔掉模块，确认 `online=0`，系统不死机、不急停、不刷屏。
7. 重新接上模块，确认 `reconnect_count` 增加并恢复 `online=1`。

本 Codex 会话里 Windows 没有枚举出可用 DAPLink COM 口，所以没有声明
最终串口/I2C 实机日志；详细边界见 worklog。
