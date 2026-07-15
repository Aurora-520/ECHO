# Phase 1F：统一健康、参数元数据与可验证操作界面

## 1. 为什么只允许一个健康快照写入者

RTOS、UART、参数、OLED 和 I2C 都已经有自己的诊断计数。统一健康层不应复制一套状态机，
而是按固定周期读取这些 owner 的事实，再生成一个稳定快照：

```text
module diagnostics
-> ServiceTask every 100 ms
-> SystemHealthSnapshot
-> OLED / 1 Hz Health / debugger Watch
```

只有 ServiceTask 写快照，读者用临界区复制和偶数 update sequence 判断一致性。快照只供观察，
不承担控制任务之间的命令通信，因此不会变成隐藏事件总线。

## 2. active、sticky 和 first fault 不是同一件事

- active：故障当前仍存在；瞬态计数变化保持短时间，确保人和 1 Hz 遥测能看到。
- sticky：故障发生过，恢复后仍留证；可恢复项需要明确命令才能清除。
- first critical fault：首个关键故障，不能被 clear recoverable 或后续连锁错误覆盖。

本次 OLED 软件离线证明 active/sticky 会同时出现，恢复后只有 active 清零；Overview 的长按
命令只能清除已恢复且 recoverable 的 bit。关键故障解除后 first fault 仍保留。

## 3. 协议边界不要先转换成小 enum

ArmClang 可用最小存储宽度表示 enum。若 wire ID `0x0101` 先转换成只登记了 1..4 的
`parameter_id_t`，高位可能在函数 ABI 边界丢失，错误地变成合法 ID 1。

正确顺序：

```text
uint16 wire id
-> 用 uint16 在 metadata 表中查找
-> 未命中立即 BAD_PARAMETER
-> 命中后才转换成内部 enum
```

这类错误编译不会报警，必须用超出 8 位但低字节看似合法的 ID 做板上测试。

## 4. 元数据表消除 UART/OLED 规则分叉

`kp/ki/kd/target` 的 default、min、max、step、units、flags 和 version 只登记一次。UART 和
OLED 都调用同一个 staging API；NaN/Inf、unknown、range 和 BUSY 也由同一处判断。

staging 成功不等于已应用。SystemTask 是 applied 参数的唯一写入者，只在 100 Hz 控制周期
边界取出一个 pending 事务。恢复默认也是一个原子事务，不能逐参数产生中间配置。

## 5. 栈问题要看调用链峰值

健康刷新最初在 ServiceTask 栈上同时创建两份完整快照，使剩余栈降到 63 words。它没有
溢出，但已经越过 64-word 告警门槛。由于 ServiceTask 是唯一调用者，两份工作副本改成函数
私有静态缓冲，最终 Service 栈余量恢复到 128 words。

不要通过降低告警阈值掩盖峰值，也不要只看平均局部变量大小。完整快照、编码 frame、格式化
缓冲和嵌套调用都会叠加。

## 6. 混合遥测必须检查两种 sequence

加入 1 Hz Health 后，全局发送序列中会穿插 Control、Health 和 ACK。采集器需要同时统计：

```text
global sequence：判断任何完整帧是否丢失
Control count/timestamp：计算 100 Hz
Health count/timestamp：计算 1 Hz
```

如果只筛 Control 再用全局 sequence，Health 插帧会被误判为 gap；如果只统计所有 frame，
Control 频率又会被算成约 101 Hz。

Health 最终为 128 B 完整帧，已经达到当前 `SerialTx_TryWrite` 原子写上限。以后增加字段应
升级 schema、拆帧或调整有诊断保护的传输上限，不能继续无边界追加。

## 7. 故障注入和最终连续运行要分开

Debugger 写变量会短暂停 MCU，因此适合验证状态语义，不适合证明实时周期。流程应是：

1. 用软件开关注入 OLED offline 或关键 fault，验证 active/sticky/first 和恢复。
2. reset run 清空注入与 RAM 参数。
3. 断开 OpenOCD，以 UART 进行 120 秒和至少 10 分钟无断点运行。
4. 让 Health 帧直接携带栈、heap、I2C、refresh 和 quiet，避免结束后暂停才补证据。

Flash、物理 ADC 五键和硬件看门狗是独立硬件门禁。没有地址、电路或 reset 行为证据时写
`deferred`，不能把接口存在或代码审查写成 passed。
