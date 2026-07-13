# ECHO directory boundaries

阶段 1A 只建立边界，不搬动已经验证的文件，也不新增硬件功能。

| 目录 | 负责内容 | 当前状态 |
| --- | --- | --- |
| `app/tasks` | FreeRTOS 任务入口与调度胶水 | 预留 |
| `app/missions` | 赛题状态机和任务插件 | 预留 |
| `app/ui` | OLED 页面、按键事件和参数编辑 | 预留 |
| `bsp/include` | 与具体天猛星硬件相关的稳定接口 | 预留 |
| `bsp/source` | GPIO、定时器、UART、I2C、PWM 等硬件适配 | 预留 |
| `module/device` | AT8236、X42S、ICM42688、OLED 等设备协议 | 预留 |
| `module/control` | PID、底盘和云台控制算法 | 预留 |
| `module/estimation` | 姿态、滤波和状态估计 | 预留 |
| `module/perception` | 灰度与视觉结果的解释层 | 预留 |
| `module/service` | 状态快照、参数、故障、日志和执行器抽象 | 预留 |
| `tests` | 分阶段硬件与模块验收代码 | 预留 |

依赖方向固定为：

```text
App -> Module -> BSP -> TI DriverLib / SysConfig
```

当前 `app/main.c`、`app/main-blinky.c`、`freertos`、`keil` 和
`platform/generated` 保持原位。阶段 1B 再建立最小任务骨架，并逐个加入 Keil。
