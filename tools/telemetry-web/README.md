# ECHO Telemetry Web Tool

Start the local server from the project root:

    powershell -ExecutionPolicy Bypass -File .\tools\serve_telemetry.ps1

Open http://localhost:8765 in Microsoft Edge or Chrome, then:

1. Close UartAssist and every other COM4 user.
2. Click Connect and select the DAPLink serial port.
3. Keep the default 460800 baud setting.
4. Enable the desired channels below the chart.
5. Use the RAM parameter panel for temporary tuning.

The browser must ask for the serial port because Web Serial requires a user
gesture. The page cannot silently claim COM4.

## Protocol

- Sync: A5 5A
- Version: 1
- Control telemetry: type 1, 40-byte payload, 56-byte frame
- Parameter set: type 2, 12-byte payload, 28-byte frame
- Parameter ACK: type 3, 16-byte payload, 32-byte frame
- CRC: CRC16-CCITT-FALSE over version through payload
- Integer and float fields: little-endian

Parameter changes update RAM only. They are validated and applied by
SystemTask at the 10 ms control-cycle boundary. No Flash write occurs.

## Command-line tools

Capture a bounded CSV session without opening the web page:

    powershell -ExecutionPolicy Bypass -File .\tools\telemetry_capture.ps1 -Port COM4 -BaudRate 460800 -DurationSeconds 600 -CsvPath .\logs\telemetry.csv

Send one RAM parameter update. The port stays open for up to three attempts
with the same transaction ID, so a retry cannot apply the value twice:

    powershell -ExecutionPolicy Bypass -File .\tools\parameter_set.ps1 -Port COM4 -BaudRate 460800 -Parameter kp -Value 2.5

Run protocol fault and recovery checks on a connected board:

    powershell -ExecutionPolicy Bypass -File .\tools\protocol_stress_test.ps1 -Port COM4 -BaudRate 460800 -TimeoutMilliseconds 1000

The capture tool limits an in-memory session to 900 seconds. Its gap count is
the number of missing frames, with 32-bit sequence and timestamp wraparound.
