# ECHO 工程红线

红线不是为了让代码“看起来分层”，而是为了确保赛场临时修改不会让多个模块争抢硬件、
把通信延迟带进控制环，或让故障无法定位。违反红线的需求必须先调整设计，再写代码。

## 1. 依赖只能向下

```text
App / Mission -> Module / Service -> BSP -> DriverLib / SysConfig
```

错误：

```c
/* app/tasks/control_task.c */
DL_Timer_setCaptureCompareValue(...);
```

正确：

```c
Actuator_SetCommand(&command);      /* App 只提交语义命令 */
/* 最终由唯一后端通过 BSP_PWM 或 BSP_UART 输出 */
```

原因：引脚、Timer、DMA 或电机后端变化时，不应修改比赛任务。

## 2. BSP 不判断比赛模式

错误：

```c
if (g_mission_mode == MISSION_TRACK) {
    DL_GPIO_setPins(...);
}
```

正确：BSP 只接收“设置电平、启动 DMA、读取计数”等板级请求。Mission 在 App 层决定目标。

原因：同一个 UART/PWM/Timer 必须能被台架测试、不同赛题和不同执行器后端复用。

## 3. 设备驱动不读取 UI

错误：电机驱动读取按键或 OLED；SSD1306 驱动判断当前任务；ICM42688 驱动读取串口命令。

正确：UI 产生事件，Mission/参数服务校验并应用；设备驱动只实现设备协议。

原因：驱动必须能在没有 OLED、没有按键或没有串口上位机时独立工作。

## 4. 算法不接触传输通道

错误：PID 内部接收 UART、读取按键、发送 OLED 字符或调用 FreeRTOS 队列。

正确：

```c
PidOutput PID_Update(PidState *state,
                     const PidParams *params,
                     float target,
                     float measurement,
                     float dt_s);
```

原因：控制器应是可台架测试的确定性函数。同一个算法可由编码器、IMU 或视觉状态驱动。

## 5. 一个执行器只有一个写入者

禁止不同任务同时修改：

- 底盘 PWM 或方向 GPIO
- X42S UART 命令
- 串口无刷云台命令
- 云台 STEP/DIR 脉冲

错误：OLED 菜单、树莓派接收任务和 ControlTask 都能直接发电机命令。

正确：所有来源只提交目标请求；唯一 ControlTask/ActuatorService 仲裁、限幅并输出。

原因：否则最后生效的命令取决于抢占时机，故障无法复现，也无法保证急停覆盖所有来源。

## 6. 控制路径不得被慢外设阻塞

高频任务中禁止：

- 等待 UART 把整帧物理发送完
- 全屏刷新 OLED
- 写 Flash
- 无超时轮询 I2C/SPI/UART
- 使用不可界定执行时间的日志格式化

正确：控制任务非阻塞发布快照/命令，低优先级任务处理显示、遥测和持久化。队列满时执行
明确降级并计数，不能无限等待。

## 7. ISR 只做最小工作

ISR 允许清状态、搬运字节、更新时间戳和通知任务。禁止在 ISR 中解析完整协议、运行 PID、
刷新 OLED、写 Flash、调用阻塞 API 或进行长循环。

若 ISR 调用 FreeRTOS `FromISR` API，优先级必须满足 FreeRTOS 端口约束；不调用 RTOS API
的高优先级 ISR 也必须保持短小。

## 8. 共享数据必须有所有权和一致性规则

- 单写者、多读者：用只读快照和版本/奇偶序列。
- 多来源命令：通过队列或集中仲裁器。
- ISR 到任务：ring buffer、任务通知或队列，明确 overflow 策略。
- 参数更新：先校验为 pending，只在控制周期边界应用。

`volatile` 只影响编译器访问，不能自动提供原子性、互斥或多字段一致性。
`g_rtos_diag` 等诊断全局只供观察，禁止作为控制命令通道。

## 9. 时间和单位必须显式

- 名称带单位：`period_us`、`timeout_ticks`、`speed_mm_s`。
- FreeRTOS Tick 用于调度，1 MHz 时基用于测量，不能混算。
- 周期任务使用绝对周期延迟，避免 `vTaskDelay()` 累积漂移。
- 32 位时间差用无符号模减处理回绕；有符号早晚比较只在半回绕范围内有效。
- 1 MHz 是内部时钟给出的标称尺度，不宣称为高精度计量时钟。

## 10. 任务和内存必须可诊断

- 新任务默认静态创建并记录分配栈、历史最小剩余栈、运行次数和超期。
- 新队列/ring 必须记录容量、high-water、overflow/drop。
- 不为每个设备盲目创建任务；按周期、优先级和数据所有权划分。
- 动态内存若确实需要，必须说明生命周期、失败策略和碎片风险。
- 栈高水位单位为 word；MSPM0 当前每个 `StackType_t` 为 4 B。

## 11. 故障必须有界并进入安全状态

- 外设等待必须超时，失败要返回错误并计数。
- OLED、遥测等非关键服务故障不能拖停控制环。
- 执行器通信失联、传感器过期或任务超期必须触发零输出/禁能策略。
- 首个关键故障上下文应保持 sticky，不能被后续连锁错误覆盖。
- 看门狗只能作为最后防线，不能代替超时和执行器安全门。

## 12. 模式切换必须经过安全过渡

禁止按键或树莓派命令直接把运行中的执行器从任务 A 交给任务 B。

推荐状态：

```text
RUN_A -> STOP_REQUESTED -> OUTPUT_SAFE -> RESET_CONTROLLERS -> ARM_B -> RUN_B
```

切换时清理 PID 积分、轨迹和过期命令；启动默认保持零输出，明确 ARM 后才允许运动。

## 13. 生成物和配置不得偷偷分叉

- `platform/generated` 只能由 SysConfig 生成，禁止手改。
- 波特率、引脚、任务周期和缓冲容量要集中配置，不在多个 `.c` 中复制魔法数字。
- App、Mission、control、estimation、service 和 device 不得包含物理 `PAx/PBx`、pin mask、IOMUX、
  Timer instance 或 DMA channel；这些只允许存在于 SysConfig、板级资源映射和 BSP。
- 引脚采用编译时配置。更换到 MCU 支持的候选引脚时，只调整集中映射、重新生成并重新验收；
  禁止通过 OLED/UART/任务在比赛运行中任意切换 pin mux。
- 候选引脚不支持原 peripheral function 时，必须在 BSP 内更换 backend 并重新评估 IRQ/DMA/时序，
  或明确编译失败；禁止静默退化成空实现或错误 GPIO。
- COM 号属于本机运行参数，不写死为永久工程事实。
- Keil、VSCode、脚本使用同一份本机路径配置；路径同步必须幂等。

## 14. Git 与证据红线

- 有用户改动时禁止 `git add .`、`git add -A`、`commit -am`。
- 禁止自动 push、删除 stash/备份、硬重置或整文件选择 ours/theirs 覆盖结构化配置。
- Keil XML、JSON 和 SysConfig 应按结构语义修改并检查噪声 diff。
- 未编译不能写“构建通过”；未烧录不能写“板上通过”；没有日志的数字不能补写。
- 阶段 tag 只能在该阶段完整验收后创建。

## 15. 代码评审自检

提交前逐项回答：

1. 新 DriverLib 调用是否只在 BSP？
2. 是否出现第二个执行器写入者？
3. 高频任务或 ISR 是否新增无界等待？
4. 共享数据的所有者和溢出策略是否明确？
5. 新任务/缓冲是否有诊断和容量依据？
6. 故障是否会进入已定义的安全状态？
7. 赛题变化是否只需改 Mission、参数或模块组合？
8. 实测结论能否由日志、快照或提交证明？
9. 物理引脚/Timer/DMA/IRQ 是否只在集中资源层出现，换脚是否不需要修改上层？
10. 新模块是否更新集成资源账本并完成共享邻居和上一阶段回归？
