# UART1 camera power-cycle recovery

Date: 2026-07-31

## Problem

The MaixCAM-to-MSPM0 vision link could enter `VISION FAULT` after the camera
was power-cycled. Wiring and PA9 reception were functional, and resetting the
MCU appeared to repair the link.

The camera restarts its 16-bit packet sequence at zero. The MCU retained the
last sequence from the old stream, so the restarted camera's valid packets
were classified as out of order until its sequence counter caught up.

## Changes

- Reset the sequence baseline on the first valid frame after the old visual
  stream has timed out.
- Added a host regression for sequence 1000, stream timeout, and restart at
  sequence 0.
- Re-applied the PA9 UART1 RX peripheral function and pull-up during recovery.
- Drained multiple pending UART1 events in a bounded ISR loop.
- Added a ServiceTask FIFO polling fallback and context-safe error clearing.
- Enabled only the actual RX interrupt so an unpowered camera cannot generate
  a continuous UART error-interrupt load.

No UART0/UART2/UART3 code or chassis/H3 control parameters were changed by
the UART1 fix.

## Verification

1. Camera-only restart, MCU continuously running:
   old camera sequence 9923, restarted sequence 1 accepted immediately,
   out-of-order count 0. Evidence:
   `tests/artifacts/uart1-fixed-camera-powercycle-20260731`.
2. Final camera restart confirmation:
   camera 58.738 fps, MCU control 98.789 Hz, sequence 655 to 3002, UART0 CRC 0,
   telemetry out-of-order 0, deadline misses 0, H3 fault 0. Evidence:
   `tests/artifacts/uart1-final-restart-confirm-20260731`.
3. Reverse order, MCU reinitialized while camera remained active:
   camera sequence 30285 accepted, camera 58.921 fps, MCU control 98.437 Hz,
   CRC 0, deadline misses 0, H3 fault 0. Evidence:
   `tests/artifacts/uart1-post-swd-recovery-20260731`.
4. UART1 diagnostics after recovery showed RX overflow 0, RX error 0,
   unexpected interrupt 0, and vision out-of-order 0.
5. FreeRTOS and App builds both passed with 0 errors / 0 warnings. Final image
   size: `Code=116440, RO=3948, RW=188, ZI=25144`.
6. Program and byte-for-byte readback passed at 500 kHz. Flash readback
   SHA-256:
   `FF590CAF78EC1CFF5E509A81420763E2075D302E0CF1AF5E588BB625E3683582`.

The camera reported `control_valid=false` during the final no-ball captures
because no ball candidate was present. Packet sequence, frame rate, and fault
telemetry confirm that the UART1 transport itself remained online.

## Debugging note

The wireless CMSIS-DAP can reinitialize this target while attaching even when
the OpenOCD command does not request a reset. Power-order acceptance must be
judged from COM18 telemetry and the camera status endpoint, not from an SWD RAM
snapshot alone.

No files were pushed to GitHub in this step. Keep `ECHO.uvmpw` and
`freertos/keil/freertos_ECHO.uvprojx` out of commits.
