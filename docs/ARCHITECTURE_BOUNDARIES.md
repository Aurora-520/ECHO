# ECHO directory boundaries

依赖方向固定为：

~~~text
App -> Module -> BSP -> TI DriverLib / SysConfig
~~~

platform/freertos 是 RTOS 与编译平台适配层，向 App 提供稳定诊断和
故障入口，不包含比赛任务。

| 目录 | 负责内容 | 当前状态 |
| --- | --- | --- |
| app/tasks | FreeRTOS 任务入口与调度胶水 | Phase 1F：System、Service、Telemetry、Display |
| app/missions | 赛题状态机和任务插件 | Phase 3 前预留 |
| app/ui | OLED 页面、按键事件和参数编辑 | Phase 1F：五页诊断 UI、虚拟输入契约 |
| bsp/include | 天猛星硬件稳定接口 | Phase 2A：encoder/PWM；备用张大头双 UART 接口已加入 |
| bsp/source | GPIO、定时器、UART、I2C、PWM 等适配 | UART2/3 分别服务第一/二代张大头，DMA 通道与调试串口隔离 |
| platform/freertos | 静态内存回调、故障钩子和 RTOS 诊断 | Phase 1F：六任务栈、heap、时序和 fault 诊断 |
| platform/generated | SysConfig 生成文件 | 增加 UART2 PB15/PB16、UART3 PB2/PB3；禁止手工修改 |
| module/device | AT8236、张大头、ICM42688、OLED 等协议 | 两代 Emm 串口协议与备用步进状态机已封装 |
| module/control | PID、运动学和控制器 | Phase 2A/2B 前预留，当前无真实 PID |
| module/estimation | 姿态、滤波和状态估计 | Phase 2B 前预留 |
| module/perception | 灰度与视觉结果解释 | Phase 2D 前预留 |
| module/service | 通信、参数、诊断、快照和执行器仲裁 | Phase 2A：SystemHealth 增加 QEI 非法跳变故障 |
| tests | 分阶段硬件与模块验收 | Phase 1F：采集 fixture 与 ignored 板测证据 |

## Phase 1F 已建立的边界

- main.c 只执行 reset cause 捕获、平台/服务初始化、统一任务创建和启动调度器。
- App 任务使用 FreeRTOS 和 Module/BSP API，不直接调用 `DL_*`。
- 手写 GPIO、Timer、UART、DMA 和 I2C DriverLib 调用只存在于 BSP。
- Idle、Timer、System、Service、Telemetry、Display 和长期队列均使用静态内存。
- ServiceTask 是统一健康快照的唯一运行时写入者；OLED、1 Hz Health 和 Watch 是读者。
- SystemTask 是 applied 参数的唯一写入者；UART 与 OLED 只能提交 pending 参数事务。
- `g_rtos_diag` 和 `g_system_health_snapshot` 只用于监测，不承担控制通信。
- SystemTask 在 100 Hz 周期边界采样左轮 QEI；ISR 只累计非法状态跳变，不调用 RTOS。
- 当前执行器门仍锁定，encoder-only 子阶段没有 AT8236/PWM 写入者。
- TI 默认 AppHooks_freertos.c 和 StaticAllocs_freertos.c 已退出内核库，
  防止 hook 来源不明确。

## 后续仍需遵守

- App 不直接调用 PWM、UART、I2C 或其他 DriverLib。
- BSP 不判断比赛模式。
- PID、滤波器和运动学不读取按键、OLED 或 UART。
- Phase 2A 必须先确定唯一执行器写入者，UI、串口和 Mission 只能提交目标请求。
- platform/generated 只能由 SysConfig 生成。

## 云台主备执行器边界

- 串口无刷电机是云台主执行器；当前张大头代码不属于主云台控制链。
- 第一代和第二代张大头均为 Emm 固件 TTL 串口备用后端，不使用 X 固件协议。
- `ZdtStepper_Init()` 只初始化静态状态和 UART；默认 `backend_selected=0`，不查询、不使能、
  不发送运动命令。只有未来执行器仲裁显式选择备用后端后，才允许请求进入设备状态机。
- 上层不得直接调用 BSP UART 或 `ZdtProtocol_Build*()` 发运动帧；必须经过备用后端选择和
  `ZdtStepper_Request*()` 的限频、去重、忙状态与退出停机门禁。
- 从备用后端退出时，状态机对两路依次发送立即停止和失能；无刷与步进后端不得同时输出。
