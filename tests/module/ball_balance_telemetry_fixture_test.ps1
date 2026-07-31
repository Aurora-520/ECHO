$ErrorActionPreference = "Stop"

function Get-Crc16Ccitt {
    param([byte[]]$Data, [int]$Offset, [int]$Length)

    [uint32]$crc = 0xFFFF
    for ($index = 0; $index -lt $Length; $index++) {
        $crc = $crc -bxor ([uint32]$Data[$Offset + $index] -shl 8)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            }
            else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return [uint16]$crc
}

function New-BallBalancePayload {
    param(
        [uint32]$UpdateSequence,
        [uint32]$ElapsedMs,
        [int16]$MeasuredPositionDecimm
    )

    $payload = New-Object byte[] 72
    [BitConverter]::GetBytes($UpdateSequence).CopyTo($payload, 0)
    [BitConverter]::GetBytes($ElapsedMs).CopyTo($payload, 4)
    [BitConverter]::GetBytes([uint32]5).CopyTo($payload, 8)
    [BitConverter]::GetBytes([uint32]1).CopyTo($payload, 12)
    [BitConverter]::GetBytes([uint32]2).CopyTo($payload, 16)
    [BitConverter]::GetBytes([uint32]0).CopyTo($payload, 20)
    [BitConverter]::GetBytes([int32]-2500).CopyTo($payload, 24)
    [BitConverter]::GetBytes([int32]1000).CopyTo($payload, 28)
    [BitConverter]::GetBytes([int32]-1500).CopyTo($payload, 32)
    [BitConverter]::GetBytes([int32]-1400).CopyTo($payload, 36)
    [BitConverter]::GetBytes([int32]-100).CopyTo($payload, 40)
    [BitConverter]::GetBytes([int16]-500).CopyTo($payload, 44)
    [BitConverter]::GetBytes($MeasuredPositionDecimm).CopyTo($payload, 46)
    [BitConverter]::GetBytes([int16]-12).CopyTo($payload, 48)
    [BitConverter]::GetBytes([int16]-7).CopyTo($payload, 50)
    [BitConverter]::GetBytes([uint16]1001).CopyTo($payload, 52)
    [BitConverter]::GetBytes([uint16]18).CopyTo($payload, 54)
    [BitConverter]::GetBytes([uint16]250).CopyTo($payload, 56)
    $payload[58] = 0x23
    $payload[59] = 4
    $payload[60] = 1
    $payload[61] = 0
    $payload[62] = 0x0D
    [BitConverter]::GetBytes([int16]1250).CopyTo($payload, 64)
    [BitConverter]::GetBytes([int16]85).CopyTo($payload, 66)
    [BitConverter]::GetBytes([int16]73).CopyTo($payload, 68)
    [BitConverter]::GetBytes([int16]91).CopyTo($payload, 70)
    return ,$payload
}

function New-Frame {
    param(
        [byte[]]$Payload,
        [uint32]$Sequence,
        [uint32]$TimestampUs
    )

    $frame = New-Object byte[] (16 + $Payload.Length)
    $frame[0] = 0xA5
    $frame[1] = 0x5A
    $frame[2] = 1
    $frame[3] = 16
    [BitConverter]::GetBytes([uint16]$Payload.Length).CopyTo($frame, 4)
    [BitConverter]::GetBytes($Sequence).CopyTo($frame, 6)
    [BitConverter]::GetBytes($TimestampUs).CopyTo($frame, 10)
    $Payload.CopyTo($frame, 14)
    $crc = Get-Crc16Ccitt -Data $frame -Offset 2 `
        -Length (12 + $Payload.Length)
    [BitConverter]::GetBytes($crc).CopyTo($frame, 14 + $Payload.Length)
    return ,$frame
}

$capturePath = Join-Path $PSScriptRoot "..\..\tools\telemetry_capture.ps1"
$binaryPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-ball-balance-telemetry-fixture.bin"
$jsonPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-ball-balance-telemetry-fixture.json"
$csvPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-ball-balance-telemetry-fixture.csv"
$bytes = [System.Collections.Generic.List[byte]]::new()

try {
    $bytes.AddRange((New-Frame `
        -Payload (New-BallBalancePayload 77 120 ([int16]-490)) `
        -Sequence 10 -TimestampUs 1000000))
    $bytes.AddRange((New-Frame `
        -Payload (New-BallBalancePayload 78 160 ([int16]-493)) `
        -Sequence 11 -TimestampUs 1040000))
    [System.IO.File]::WriteAllBytes($binaryPath, $bytes.ToArray())

    & $capturePath -InputPath $binaryPath `
        -BallBalanceCsvPath $csvPath -JsonPath $jsonPath | Out-Host
    if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
        throw "telemetry_capture.ps1 returned $LASTEXITCODE"
    }

    $summary = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
    if (($summary.ValidFrames -ne 2) -or
        ($summary.BallBalanceFrames -ne 2) -or
        ($summary.BallBalanceRateHz -ne 25.0) -or
        ($summary.LatestBallBalance.UpdateSequence -ne 78) -or
        ($summary.LatestBallBalance.ControlOutputDeg -ne -2.5) -or
        ($summary.LatestBallBalance.MeasuredPositionMm -ne -49.3) -or
        ($summary.LatestBallBalance.PositionErrorMm -ne -0.7) -or
        ($summary.LatestBallBalance.VisionValidAgeMs -ne 18) -or
        ($summary.LatestBallBalance.VisionValid -ne $true) -or
        ($summary.LatestBallBalance.Saturated -ne $false) -or
        ($summary.LatestBallBalance.MotorOnline -ne $true) -or
        ($summary.LatestBallBalance.MotorEnabled -ne $true) -or
        ($summary.LatestBallBalance.FeedforwardDeg -ne 1.25) -or
        ($summary.LatestBallBalance.PlannedAccelMmS2 -ne 85) -or
        ($summary.LatestBallBalance.ImuAccelMmS2 -ne 73) -or
        ($summary.LatestBallBalance.EncoderAccelMmS2 -ne 91)) {
        throw "Ball-balance telemetry summary did not match expected values."
    }
    $csvRows = @(Import-Csv -LiteralPath $csvPath)
    if (($csvRows.Count -ne 2) -or
        ($csvRows[1].measured_position_mm -ne "-49.3") -or
        ($csvRows[1].control_output_deg -ne "-2.5") -or
        ($csvRows[1].motor_online -ne "1") -or
        ($csvRows[1].feedforward_deg -ne "1.25") -or
        ($csvRows[1].planned_accel_mm_s2 -ne "85")) {
        throw "Ball-balance telemetry CSV did not match expected values."
    }
    Write-Output "Ball-balance telemetry fixture: PASS"
}
finally {
    Remove-Item -LiteralPath $binaryPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $jsonPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $csvPath -Force -ErrorAction SilentlyContinue
}
