# Phase 1A baseline

记录日期：2026-07-13

## 修改前基线

Keil 全量重建结果：

```text
freertos_ECHO.lib: 0 errors, 0 warnings
ECHO.axf:          0 errors, 0 warnings
Program size:      Code 7684, RO-data 228, RW-data 4, ZI-data 5316 bytes
```

修改前产物 SHA-256：

```text
ECHO.axf          086D79D353CF6A1EB778200A341130E40FE27E55A696353A0281D447531AC757
ECHO.hex          C6913EBD742B7F285DF356FA8AE6D8B6C9857847AB6D169B0A39F4F559C9FD79
freertos_ECHO.lib 0420637836E0AA3D9A30EA74D5910F8BB6EFF4CA4C167D5BE3CE7B82AC92CBA2
```

完整回退包：

```text
E:\ECHO_BACKUPS\ECHO_phase0_working_20260713_2120.zip
SHA-256: 366463072A8023A2A52F18F14C67E4C4D5FBC6A15AEC6853A2B78A1607F289C4
```

## 已确认约束

- 当前 SysConfig 和 `configCPU_CLOCK_HZ` 均为 32 MHz，阶段 1A 不改时钟。
- `configTOTAL_HEAP_SIZE` 仍为 3 KiB，仅满足当前 blink 基线。
- 栈高水位 API 当前未启用，留到阶段 1B 单独修改和验证。
- 主要编译和调试链为 Keil + OpenOCD + DAPLink。
- 项目内 pyOCD 虚拟环境绑定旧绝对路径，不纳入移动验收。
- `platform/generated` 由 SysConfig 生成，不手工修改。

## 阶段 1A 验收结果

原路径 `E:\ECHO` 使用集中路径配置完成全量重建：

```text
freertos_ECHO: 0 errors, 0 warnings
ECHO:          0 errors, 0 warnings
```

随后将工程复制到以下测试路径，并排除全部旧 `Objects` 和 `Listings`：

```text
C:\Users\Auror\AppData\Local\Temp\ECHO_MOVE_TEST_20260713_2055
```

测试副本完成路径同步、环境检查、SysConfig 生成和全量重建，结果仍为
`0 errors, 0 warnings`。同步脚本连续执行两次不会继续改写 Keil、VSCode
或 workspace 文件。

原路径和移动副本的最终 Flash 镜像完全一致：

```text
ECHO.hex SHA-256
C6913EBD742B7F285DF356FA8AE6D8B6C9857847AB6D169B0A39F4F559C9FD79
```

AXF 和静态库包含工程绝对调试路径或归档信息，因此移动后文件哈希可以不同；
HEX 一致才是本阶段判断固件行为未变化的关键证据。

阶段 1A 验收通过。
