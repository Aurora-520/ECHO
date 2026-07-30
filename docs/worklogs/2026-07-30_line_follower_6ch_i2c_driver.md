# 2026-07-30 6-channel I2C line follower driver

## Scope

- Branch: `codex/i2c-six-channel-driver`
- Baseline: `origin/main` at `5be299c41005d1fbabf690717284d1c3ce58cdb7`
- Added an independent device driver under `module/device/`.
- No control task, H mission, chassis PID, OLED, UART1 vision, UART2 wireless,
  UART3 stepper, SysConfig pin, or chassis parameter code was changed.
- The driver is not called by any existing task, so the formal car remains
  unchanged when the module is absent.

## Source Documents

- `D:\BaiduNetdiskDownload\1.6路巡线传感器介绍.pdf`
- `D:\BaiduNetdiskDownload\2.快速上手\2.快速上手.pdf`

The local PDFs identify the product as a `6路巡线传感器`; the example project
name shown in the quick-start material is `LineFollowerLearn6CH`. The PDFs do
not provide a vendor SKU, silicon chip ID register, or firmware version
register. Web search did not find a public matching register manual beyond the
local documents, so the driver only encodes fields explicitly present in the
PDFs.

## Hardware Notes

- Supply: `DC 5V`
- Current: about `85 mA`
- Interface: I2C
- Fixed 7-bit I2C address: `0x5C`
- Formal ECHO I2C bus remains `PA0=SDA`, `PA1=SCL`, shared with OLED/IMU.
- Do not add 5 V pull-ups to SDA/SCL on the formal 3.3 V MCU bus. Confirm the
  module's I2C level behavior or use level shifting before connecting it to the
  shared bus.

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

Analog values and thresholds are raw unitless sensor counts from the module.
The driver also exposes `margin = raw - threshold` when thresholds have been
read. The digital result is exposed as a raw 6-bit mask plus per-channel helper
bits using the natural bit order `bit0..bit5`; the PDFs do not explicitly state
the bit-to-physical-probe orientation, so control code must confirm this on
hardware before depending on left/right meaning.

## Driver API

- `LineFollower6_Init()`: resets diagnostics, reads thresholds, reads one
  sensor frame, and reports whether both succeeded.
- `LineFollower6_Update()` / `LineFollower6_ReadAll()`: reads register `5`
  through `17` as one 13-byte burst and updates the snapshot.
- `LineFollower6_ReadThresholds()`: reads register `18` through `29` as one
  12-byte burst.
- `LineFollower6_GetSnapshot()`: copies the current snapshot for control or
  diagnostics code.
- `LineFollower6_MarkOffline()`: allows upper layers to force offline state
  after detecting a shared-bus fault.
- `LineFollower6_ParseSensorFrame()` and `LineFollower6_ParseThresholdFrame()`:
  pure parsing helpers used by host tests.

Diagnostics include `online`, `initialized`, `sample_count`, `success_count`,
`failure_count`, init counters, threshold counters, `reconnect_count`,
`offline_count`, `consecutive_failure_count`, `last_i2c_result`,
`last_register`, and `last_error`.

## Validation

- Host fixed-frame parser test: added
  `tests/device/line_follower_6ch_test.c`.
- Host compile command:
  `gcc -std=c99 -Wall -Wextra -Werror -Imodule/device -Ibsp/include tests/device/line_follower_6ch_test.c module/device/line_follower_6ch.c`
- Host test result: `line_follower_6ch_test: PASS`
- Test coverage:
  - little-endian raw value decoding,
  - threshold decoding,
  - margin calculation,
  - digital mask helper bits,
  - invalid frame length handling,
  - I2C NACK/offline reporting,
  - reconnect after a failed read,
  - threshold read failure diagnostics.
- Formal Keil build: not completed in this local Codex copy. The build script
  first failed because `config/local_paths.ps1` was absent; after adding an
  ignored local path file, the checked-in Keil project still referenced the
  machine-specific SDK path
  `D:\sftoware\TI_CCS\mspm0_sdk_2_10_00_04`, while this machine only exposed
  `D:\TI\M0_SDK\mspm0_sdk_2_02_00_05`. The project files were not modified or
  submitted to avoid committing local Keil path churn.

## Deferred Hardware Validation

The requested second-board I2C scan and continuous hardware run were not
performed in this Codex environment because no accessible Tianmengxing board or
wired 6-channel module was available to the session.

Before merging into the formal car, run:

1. I2C scan on the test board and confirm only the expected `0x5C` module
   response for this device.
2. Read register `5`, `6..17`, and `18..29`; verify channel values change with
   each probe over black/white targets.
3. Confirm the digital mask bit order against physical probe positions.
4. Run several minutes of periodic `LineFollower6_Update()` at the intended
   service frequency; record actual rate, `success_count`, `failure_count`,
   `last_i2c_result`, and shared-bus OLED/IMU health.
5. Unplug the module and confirm `online=0` without task stalls, emergency
   stop, OLED spam, or IMU/sample-rate degradation.
6. Reconnect and confirm `reconnect_count` increments and `online=1` recovers.

Known limitation: without a documented chip-ID register, initialization can
only verify that the expected address and register reads respond.
