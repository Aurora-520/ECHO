# VSCode DAPLink Debugging

## Connections

Use the known-good Tianmengxing board.

~~~text
DAPLink GND    -> board GND
DAPLink SWDIO  -> PA19
DAPLink SWCLK  -> PA20
DAPLink nRESET -> board nRESET, recommended
~~~

Power the board separately. Do not connect a DAPLink 3V3/VCC output to an already powered 3.3V rail. A pin explicitly labelled VTref or TVCC may be connected as a voltage reference.

## Build and flash

In VSCode:

~~~text
Terminal -> Run Task -> ECHO: Build + Flash (DAPLink)
~~~

This task builds the ECHO application, programs ECHO.hex with OpenOCD, verifies Flash, and resets the target.

The debug configuration also programs `ECHO.hex`. `ECHO.axf` is used only for
source lines, symbols, variables, and call stacks.

## Start a debug session

1. Open the Run and Debug view with Ctrl+Shift+D.
2. Select ECHO: Debug (DAPLink + OpenOCD).
3. Press F5.
4. The application is rebuilt, programmed, and stopped at main.

Useful keys:

~~~text
F9        Toggle breakpoint
F5        Start debugging or continue from the current stop
F10       Step over
F11       Step into
Shift+F11 Step out of the current function
Pause     Halt the whole MCU at its current instruction
Ctrl+Shift+F5 Restart the current debug session
Shift+F5 Stop debugging
~~~

## Breakpoint behavior

A breakpoint stops the whole CPU when execution reaches its source line. It does
not stop only one FreeRTOS task. The RTOS tick, interrupts, queues, and all other
tasks are also paused.

With no breakpoint:

1. Start debugging with F5; the temporary entry breakpoint stops at `main`.
2. Press F5 again; the program runs continuously and the LED blinks.

With a breakpoint at `main-blinky.c:208`:

1. The LED toggle on line 207 executes first.
2. The CPU stops on line 208.
3. Pressing F5 runs for one more second, toggles the LED again, and stops again.

This means one F5 can turn the LED on and the next F5 can turn it off. That is
the expected breakpoint behavior.

Use F10 when a highlighted line calls a function that you do not want to enter.
Use F11 when you want to inspect that function line by line. Avoid single-step
judgements about real-time timing because the debugger changes the timing.

## Debug output

`SIGINT` means the Pause button interrupted the running CPU. It is not a firmware
fault by itself.

GNU GDB can print `RW_IRAM2 outside of ELF segments` or `pc ... not in symtab`
while reading an ArmClang AXF. Programming uses the complete HEX file, so these
messages do not prevent normal source breakpoints. Connection failures such as
`Could not find MEM-AP`, `Failed to read device ID`, or `Target not examined`
are different and must be investigated.

The pyOCD configuration is retained as a fallback. The OpenOCD configuration is the primary path because it includes an MSPM0-specific Flash driver and low-power debug handling.

## Verified hardware

The following chain was tested successfully:

~~~text
Cortex-Debug compatible launch
-> GNU Arm GDB 14.2
-> xPack OpenOCD 0.12.0+dev
-> DAPLink CMSIS-DAP v2
-> MSPM0G3507 Cortex-M0+
~~~

Verified operations:

- target detection at 1MHz SWD;
- Flash programming and verification;
- HEX programming with AXF source symbols;
- hardware breakpoint at main;
- repeated breakpoint hits at RTOS ticks 1000 and 2000;
- PB22 output toggling at each breakpoint hit;
- PC and SP register reads;
- target detach and resume.

## Motor safety

Do not use breakpoints with powered motors until a hardware-safe output path exists. Halting the CPU stops the control task, but PWM or UART-controlled actuators may retain their last command.

Before debugging motor code:

- lift drive wheels off the ground;
- disable motor power or hold the driver enable line inactive;
- keep a physical emergency stop available;
- make enable pins default to the safe state in hardware;
- never rely on the debugger disconnect action as an emergency stop.
