# ECHO directory boundaries

依赖方向固定为：

~~~text
App -> Module -> BSP -> TI DriverLib / SysConfig
~~~

platform/freertos 是 RTOS 与编译平台适配层，向 App 提供稳定诊断和
故障入口，不包含比赛任务。

| 目录 | 负责内容 | 当前状态 |
| --- | --- | --- |
| app/tasks | FreeRTOS 任务入口与调度胶水 | Phase 1B：SystemTask、ServiceTask |
| app/missions | 赛题状态机和任务插件 | 预留 |
| app/ui | OLED 页面、按键事件和参数编辑 | 预留 |
| bsp/include | 天猛星硬件稳定接口 | Phase 1B：BSP_LED |
| bsp/source | GPIO、定时器、UART、I2C、PWM 等适配 | Phase 1B：PB22 LED |
| platform/freertos | 静态内存回调、故障钩子和 RTOS 诊断 | Phase 1B：已启用 |
| platform/generated | SysConfig 生成文件 | 已启用，禁止手工修改 |
| module/device | AT8236、X42S、ICM42688、OLED 等协议 | 预留 |
| module/control | PID、底盘和云台控制算法 | 预留 |
| module/estimation | 姿态、滤波和状态估计 | 预留 |
| module/perception | 灰度与视觉结果解释 | 预留 |
| module/service | 快照、参数、故障、日志和执行器抽象 | 预留 |
| tests | 分阶段硬件与模块验收 | 预留 |

## Phase 1B 已建立的边界

- main.c 只执行 SysConfig 初始化、统一任务创建和启动调度器。
- App 任务使用 FreeRTOS API，但不调用 DL_GPIO。
- BSP_LED_Toggle() 是 PB22 的唯一手写 DriverLib 调用位置。
- Idle、Timer、System、Service 和心跳队列均由静态对象提供内存。
- g_rtos_diag 只用于监测，不承担任务间控制数据传输。
- TI 默认 AppHooks_freertos.c 和 StaticAllocs_freertos.c 已退出内核库，
  防止 hook 来源不明确。

## 后续仍需遵守

- App 不直接调用 PWM、UART、I2C 或其他 DriverLib。
- BSP 不判断比赛模式。
- PID、滤波器和运动学不读取按键、OLED 或 UART。
- 只有未来的 ControlTask 可以写底盘和云台运动命令。
- platform/generated 只能由 SysConfig 生成。
