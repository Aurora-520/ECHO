# Phase 1E: SSD1306 OLED and Debug UI

## Scope

Phase 1E adds a non-critical OLED diagnostics path without changing the
100 Hz control loop or the Phase 1D telemetry protocol.

Included:

- I2C0 on PA0 (SDA) and PA1 (SCL)
- SSD1306 128x64 display probing at addresses 0x3C and 0x3D
- static 1024-byte framebuffer
- static low-priority DisplayTask
- two diagnostics pages
- debugger-injected five-key input
- bounded I2C waits and offline retry
- UART quiet-window coordination around each OLED transfer burst

Not included:

- physical keys or ADC resistor ladder
- Flash parameter persistence
- motor, encoder, IMU, or gimbal control
- I2C DMA or interrupt-driven transfers

## Wiring

| OLED | MSPM0G3507 |
| --- | --- |
| VCC | 3.3 V |
| GND | GND |
| SDA | PA0 |
| SCL | PA1 |

The MSPM0 SysConfig tool rejects internal pull-ups for the I2C open-drain
pin configuration. The current four-pin OLED module works without added
pull-ups, so it most likely includes board-level pull-ups. The accepted
configuration is 400 kHz with I2C clock divider 1 and TPR 9.

The tested hardware still uses temporary jumper wires and has no added
decoupling capacitor at the OLED connector. This is an explicit hardware
risk, not a reason to block software validation. For competition hardware,
use short wiring and a solid ground, place one 4.7 kohm pull-up from SDA to
3.3 V and one from SCL to 3.3 V when the module does not already provide
them, and add local decoupling close to the display.

## Layer boundaries

- `bsp_i2c` is the only module that calls I2C DriverLib.
- `ssd1306` owns the framebuffer and display protocol.
- `ui_input` owns key events and debugger injection.
- `diagnostic_page` formats read-only snapshots.
- `SerialTx` owns the UART quiet-window mechanism.
- `DisplayTask` initializes, retries, renders, refreshes, and coordinates
  the display and serial services.
- SystemTask and ServiceTask never call the display driver.

Neither `ssd1306` nor `bsp_i2c` includes or calls `serial_tx`. This keeps the
device and BSP layers independent of the telemetry service.

Every BSP I2C wait uses the 1 MHz timebase and returns an error. A missing
or disconnected OLED is recorded as offline and retried once per second;
it must not halt FreeRTOS.

## Task configuration

| Task | Priority | Static stack | Period |
| --- | ---: | ---: | ---: |
| SystemTask | idle + 2 | 256 words | 10 ms |
| ServiceTask | idle + 1 | 256 words | 2 ms |
| Telemetry | idle + 1 | 256 words | event driven |
| DisplayTask | idle | 256 words | 500 ms online, 1000 ms offline retry |

Before initialization or a full-screen refresh, DisplayTask waits up to
about 5 ms for the UART ring, DMA, and physical TX line to become idle. It
checks once per tick. A failed acquisition is deferred by 7 ms so it does
not lock to the 10 ms telemetry phase. After eight consecutive deferrals,
the refresh is recorded as skipped and DisplayTask returns to its normal
500 ms period. A deferred or skipped refresh is not an OLED failure and
does not mark the display offline.

SSD1306 initialization includes one full-screen refresh. DisplayTask releases
the quiet window after a successful initialization and delays before the
next normal refresh, so two full-screen transfers are never placed in one
quiet window.

At 400 kHz a full refresh takes about 38.5 ms on the tested board. Control
and telemetry tasks keep their higher priorities; only UART DMA starts are
held while the display owns the quiet window. Telemetry frames continue to
enter the existing 1024-byte ring.

## Virtual keys

Add this symbol to a Keil or VSCode Watch window:

`g_ui_debug_key_request`

Write one of these values while the target is running:

| Value | Key |
| ---: | --- |
| 1 | UP |
| 2 | DOWN |
| 3 | LEFT |
| 4 | RIGHT |
| 5 | OK |

DisplayTask consumes a valid nonzero value once and clears it to zero.
UP/LEFT select the previous page; DOWN/RIGHT select the next page.

Useful Watch symbols:

- `g_display_task_diag`
- `g_serial_tx_diag`
- `g_ssd1306_diag`
- `g_bsp_i2c_diag`
- `g_rtos_diag.display_stack_min_free_words`

Halting the CPU during an active I2C transaction can intentionally trigger
a transfer timeout after resume because the hardware timebase keeps running.
Reset the board after breakpoint-based I2C tests before judging failure
counters.

## Build

Run:

```text
ECHO: Rebuild FreeRTOS + App
ECHO: Build + Flash (DAPLink)
```

The SysConfig pre-build step regenerates the I2C pin and peripheral code.
Do not edit `platform/generated/ti_msp_dl_config.c` by hand.

The accepted generated values are:

- `DL_I2C_CLOCK_DIVIDE_1`
- `DL_I2C_setTimerPeriod(..., 9)`
- `OLED_I2C_BUS_SPEED_HZ 400000`

## Fault isolation evidence

The original UART corruption was strictly associated with active periodic
OLED refreshes. The control task itself did not miss deadlines.

| OLED refresh | UART | Duration | Result |
| --- | ---: | ---: | --- |
| enabled | 460800 | 600 s | CRC 297, sequence gaps 302 |
| enabled | 230400 | 60 s | CRC 14, sequence gaps 14 |
| disabled | 230400 | 120 s | 12194 frames, CRC 0, gaps 0, 100 Hz, 10000 us period, deadline misses 0 |
| enabled after jumper-wire rework | 230400 | 120 s | 671377 bytes, 9632 valid frames, CRC 2232, gaps 2564 in 197 events, 78.975 Hz, deadline misses 0 |
| disabled through RAM switch, no reflash | 230400 | 60 s | 6100 frames, CRC 0, gaps 0, 100 Hz, 10000 us min/max, deadline misses 0 |

The RAM test changed `g_display_debug_refresh_enable` from 1 to 0 while the
firmware was running. A reset restores the default value of 1.

These results prove that the UART-only path is sound and that corruption
appears only while I2C is active. The exact electrical contribution of
jumper-wire crosstalk or ground bounce versus peripheral concurrency is not
separately proven. The quiet window removes the harmful overlap on the
current temporary hardware without adding cross-layer dependencies or
enlarging the serial ring.

## Final verification

AC6 full rebuild:

- FreeRTOS: 0 errors, 0 warnings
- application: 0 errors, 0 warnings
- Code 35848 bytes, RO data 1128 bytes, RW data 28 bytes, ZI data 14492 bytes
- total ROM 37004 bytes, total RW 14520 bytes

OLED and UI checks:

- OLED online at address `0x3C`
- 400 kHz I2C accepted by the physical display
- virtual `RIGHT=4` consumed once, cleared to zero, and changed page 0 to 1

Concurrent OLED and telemetry checks at 230400 baud:

| Duration | Captured bytes | Valid frames | CRC | Gaps | Rate | Period | Deadline misses |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| 60 s | 341432 | 6097 | 0 | 0 | 100 Hz | 9999-10001 us | 0 |
| 120 s | 682752 | 12192 | 0 | 0 | 100 Hz | 9999-10001 us | 0 |
| 120 s, debugger kept attached for the final snapshot | 682976 | 12196 | 0 | 0 | 100 Hz | 9999-10001 us | 0 |

Final same-run diagnostic snapshot:

- OLED refreshes: 419 successful, 0 failed, 0 offline events
- I2C writes: 67058 successful, 0 errors or recoveries
- display quiet windows: 419 acquired, 419 released, active 0
- maximum quiet window: 38529 us
- serial writes dropped: 0
- serial ring high water: 280 bytes
- DMA stall/restart/abort: 0/0/0
- control period min/max: 9999/10001 us
- control deadline misses: 0
- stack minimum free words: System 199, Service 225, Telemetry 192,
  Display 166, Idle 104, Timer 104
- heap free/minimum-ever-free: 3056/3056 bytes
- RTOS fault code: 0

On this DAPLink/OpenOCD setup, creating a new debug attachment can reset the
target. Keep OpenOCD attached during a run when the final RAM diagnostics
must belong to exactly the same boot.

## Acceptance

The phase tag is created only after:

- Keil AC6 full build reports zero errors and zero warnings.
- The physical OLED is detected and displays both pages.
- virtual keys are consumed exactly once.
- telemetry remains 100 Hz with zero CRC errors and zero sequence gaps.
- the control task reports zero deadline misses.
- serial writes are not dropped and the ring high-water mark stays below
  850 bytes.
- the maximum quiet window stays below 150000 us and acquired equals
  released.
- a 120-second concurrent OLED and telemetry run completes without OLED,
  I2C, DMA, CRC, or sequence-gap failures.
