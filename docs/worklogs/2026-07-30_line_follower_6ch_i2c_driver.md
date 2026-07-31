# 2026-07-30 6-channel I2C line follower driver

## Scope

- Branch: `codex/i2c-six-channel-driver`
- Baseline: `origin/main` at `5be299c41005d1fbabf690717284d1c3ce58cdb7`
- Added an independent 6-channel I2C device driver under `module/device/`.
- Added `bsp/source/bsp_reflectance_i2c6.c` as a drop-in backend for the
  existing `bsp_reflectance.h` API.
- No chassis PID, line-following policy, H mission logic, OLED, UART1 vision,
  UART2 wireless, UART3 stepper, SysConfig pin assignment, or task state
  machine source was changed.
- Checked-in Keil project files are not modified. To integrate on another
  machine, replace the project entry for `bsp_reflectance.c` with
  `bsp_reflectance_i2c6.c` and add `module/device/line_follower_6ch.c`.

## Source Documents

- Local vendor PDF: `D:\BaiduNetdiskDownload\1.6...pdf`
- Local vendor PDF: `D:\BaiduNetdiskDownload\2...quick-start.pdf`
- Public web search on 2026-07-31 did not find a matching public 6-channel
  register manual. The committed register map below is taken from the local
  PDFs only, not inferred from other line-follower modules.

## Hardware Notes

- Product family: Hiwonder 6-channel I2C line follower module, quick-start
  example name `LineFollowerLearn6CH`.
- Supply: `DC 5V`
- Current: about `85 mA`
- Interface: I2C
- Fixed 7-bit I2C address: `0x5C`
- Formal ECHO I2C bus remains `PA0=SDA`, `PA1=SCL`, shared with OLED/IMU.
- Do not add 5 V pull-ups to the formal 3.3 V MCU I2C bus. Confirm the module
  level behavior or use level shifting before tying it to the shared bus.

## Register Map

| Register | Meaning | Type | Length | Endian |
| --- | --- | --- | --- | --- |
| `5` | all digital sensor results | `uint8_t` | 1 byte | n/a |
| `6` | channel 1 analog value | `uint16_t` | 2 bytes | little-endian |
| `8` | channel 2 analog value | `uint16_t` | 2 bytes | little-endian |
| `10` | channel 3 analog value | `uint16_t` | 2 bytes | little-endian |
| `12` | channel 4 analog value | `uint16_t` | 2 bytes | little-endian |
| `14` | channel 5 analog value | `uint16_t` | 2 bytes | little-endian |
| `16` | channel 6 analog value | `uint16_t` | 2 bytes | little-endian |
| `18` | channel 1 threshold | `uint16_t` | 2 bytes | little-endian |
| `20` | channel 2 threshold | `uint16_t` | 2 bytes | little-endian |
| `22` | channel 3 threshold | `uint16_t` | 2 bytes | little-endian |
| `24` | channel 4 threshold | `uint16_t` | 2 bytes | little-endian |
| `26` | channel 5 threshold | `uint16_t` | 2 bytes | little-endian |
| `28` | channel 6 threshold | `uint16_t` | 2 bytes | little-endian |

Analog values and thresholds are raw unitless counts from the module. The
driver also exposes `margin = raw - threshold` once thresholds are valid.
The digital result is exposed as the raw mask from register `5` plus
per-channel helper bits `bit0..bit5`.

No chip-ID or firmware-version register is documented in the available
materials, so initialization verifies the address and documented register reads
instead of reading an ID register.

## Driver API

- `LineFollower6_Init()`: resets diagnostics, reads thresholds, reads one
  sensor frame, and returns whether both succeeded.
- `LineFollower6_Update()` / `LineFollower6_ReadAll()`: reads register `5`
  and the six analog registers, then updates the snapshot.
- `LineFollower6_ReadThresholds()`: reads register `18` through `29`.
- `LineFollower6_Service(now_us, sample)`: periodic, non-burst service helper.
  Each call performs at most one I2C transaction; one full six-channel frame is
  produced every `8000 us` when the bus is healthy.
- `LineFollower6_GetSnapshot()`: copies a coherent diagnostics snapshot.
- `LineFollower6_MarkOffline()`: lets an upper layer force offline state after
  detecting a shared-bus fault.
- `LineFollower6_ParseSample()` and `LineFollower6_ParseThresholds()`: pure
  parsing helpers used by host tests.

Diagnostics include `online`, `initialized`, `sample_count`, `success_count`,
`failure_count`, init counters, threshold counters, `reconnect_count`,
`offline_count`, `consecutive_failure_count`, `service_call_count`,
`service_deferred_count`, `last_i2c_result`, `last_register`, and
`last_error`.

## Drop-in Reflectance Replacement

`bsp/source/bsp_reflectance_i2c6.c` keeps the old API:

- `BSP_Reflectance_Init()`
- `BSP_Reflectance_Service(bsp_reflectance_sample_t *sample)`
- `g_bsp_reflectance_diag`

The existing control, telemetry, UI, and H-mission callers can keep including
`bsp_reflectance.h`. The teammate should replace the Keil project source
`bsp_reflectance.c` with `bsp_reflectance_i2c6.c`; the two files must not be
compiled together because they intentionally export the same BSP symbols.

The replacement backend expands six physical readings into the legacy eight
positions by linear interpolation, preserving the existing 8-channel consumer
contract. Default channel order is
`LINE_FOLLOWER6_CHANNEL_ORDER_1_TO_6` in
`module/device/line_follower_6ch_config.h`, so the backend builds directly
without an extra Keil macro. If the installed module is physically reversed,
change only:

```c
#define LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER \
    LINE_FOLLOWER6_CHANNEL_ORDER_6_TO_1
```

Because the optical geometry and count scale are different from the old gray
sensor, reflectance calibration should be recollected after replacement.

## Fault Behavior

- Missing module or I2C failure sets `online=0` and increments failure
  counters.
- Offline retry period is `500 ms`.
- No application task blocks waiting for this driver; the periodic service
  performs at most one I2C transaction per call.
- The driver does not issue emergency stop and does not print repeatedly.
- Existing OLED/IMU sharing on PA0/PA1 is unchanged by default project files.

## Validation

Host fixed-frame test:

```powershell
gcc -std=c99 -Wall -Wextra -Werror `
  -Imodule/device -Ibsp/include `
  tests/device/line_follower_6ch_test.c `
  module/device/line_follower_6ch.c `
  bsp/source/bsp_reflectance_i2c6.c `
  -o .\line_follower_6ch_test.exe
.\line_follower_6ch_test.exe
```

Result on 2026-07-31: `line_follower_6ch_test: PASS`

Covered cases:

- little-endian raw value decoding,
- threshold decoding,
- margin calculation,
- digital mask helper bits,
- invalid frame length handling,
- I2C NACK/offline reporting,
- reconnect after a failed read,
- threshold read failure diagnostics,
- five-minute 125 Hz service simulation with zero synthetic errors,
- old `bsp_reflectance.h` drop-in backend sample mapping.

Keil compatibility build was run on a temporary verification copy whose Keil
projects were locally adapted for this computer's SDK path and whose
application project replaced `bsp_reflectance.c` with
`bsp_reflectance_i2c6.c`.

2026-07-31 result:

- `freertos_ECHO`: `0 Error(s), 0 Warning(s)`
- `ECHO`: `0 Error(s), 0 Warning(s)`
- SysConfig pre-build also completed after running outside the Codex sandbox.
  It emitted the existing project-configuration warning about disabled project
  configuration file generation, unrelated to this driver.

The final checked-in formal project does not include the new backend source by
default, so main-car behavior remains unchanged until the teammate explicitly
swaps the BSP source entry.

## Hardware Validation Status

The Windows registry shows DAPLink VID/PID `VID_0D28&PID_0204`, including the
previous expected UID `963B1FD7B0108F3E67BC1E895C397CCA`, but this session did
not receive a serial COM port from Windows and no DAPLink mass-storage drive
was mounted. Because there was no usable UART capture path, no final live I2C
scan, 180-second register log, unplug/replug log, or physical channel-direction
confirmation is claimed in this worklog.

Before mounting on the formal car, run the included diagnostic image or an
equivalent board test and record:

1. I2C scan ACK at `0x5C`.
2. Register `5`, `6..17`, and `18..29` reads match the table above.
3. All six channels change over black/white targets.
4. Physical left/right direction matches the selected order macro.
5. Several minutes of periodic reads report the intended frame rate and stable
   error counts.
6. Unplug sets `online=0` without stalls; reconnect increments
   `reconnect_count` and resumes `online=1`.

Known limitation: without a documented chip-ID register, the driver cannot
distinguish this module from another I2C device at `0x5C` that happens to
respond with compatible register lengths.
