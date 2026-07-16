[CmdletBinding()]
param(
    [string]$Port = "COM4",
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(-120.0, 120.0)]
    [double]$InitialRpm = 10.0,
    [ValidateRange(-120.0, 120.0)]
    [double]$FinalRpm = 120.0,
    [ValidateRange(500, 10000)]
    [int]$StepAtMilliseconds = 2000,
    [ValidateRange(3, 30)]
    [int]$DurationSeconds = 8,
    [string]$OutputDirectory = "",
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmWheelSuspended,
    [switch]$ConfirmBothMotorsConnected,
    [switch]$ConfirmCurrentLimitedSupply,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

if (-not $ConfirmUserPresent -or
    -not $ConfirmWheelSuspended -or
    -not $ConfirmBothMotorsConnected -or
    -not $ConfirmCurrentLimitedSupply -or
    -not $ConfirmPhysicalDisconnectReady) {
    throw "Speed step tests require every physical safety confirmation."
}
if ([Math]::Abs($InitialRpm) -lt 0.1 -or
    [Math]::Abs($FinalRpm) -lt 0.1 -or
    [Math]::Abs($FinalRpm - $InitialRpm) -lt 0.1) {
    throw "InitialRpm and FinalRpm must be distinct non-zero targets."
}
$durationMs = $DurationSeconds * 1000
if ($StepAtMilliseconds -ge ($durationMs - 1000)) {
    throw "StepAtMilliseconds must leave at least one second at FinalRpm."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $EchoPaths.ProjectRoot `
        "tests\artifacts\phase2a-step-$timestamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$rawPath = Join-Path $OutputDirectory "capture.bin"
$csvPath = Join-Path $OutputDirectory "control.csv"
$jsonPath = Join-Path $OutputDirectory "capture.json"
$resultPath = Join-Path $OutputDirectory "step-result.json"
$captureTool = Join-Path $PSScriptRoot "telemetry_capture.ps1"

function Get-Crc16Ccitt {
    param([byte[]]$Data, [int]$Offset, [int]$Length)

    [uint32]$crc = 0xFFFF
    for ($index = 0; $index -lt $Length; $index++) {
        $crc = $crc -bxor ([uint32]$Data[$Offset + $index] -shl 8)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            } else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return [uint16]$crc
}

function New-SpeedCommand {
    param([uint32]$Sequence, [double]$TargetRpm, [uint16]$DurationMs)

    [int16]$targetDeciRpm = [Math]::Round(
        $TargetRpm * 10.0, [MidpointRounding]::AwayFromZero)
    $command = New-Object byte[] 36
    $command[0] = 0xA5
    $command[1] = 0x5A
    $command[2] = 1
    $command[3] = 5
    [BitConverter]::GetBytes([uint16]20).CopyTo($command, 4)
    [BitConverter]::GetBytes($Sequence).CopyTo($command, 6)
    [BitConverter]::GetBytes([uint32]0).CopyTo($command, 10)
    [BitConverter]::GetBytes(
        [Convert]::ToUInt32("4543484F", 16)).CopyTo($command, 14)
    [BitConverter]::GetBytes(
        [Convert]::ToUInt32("BABCB7B0", 16)).CopyTo($command, 18)
    [BitConverter]::GetBytes($Sequence).CopyTo($command, 22)
    [BitConverter]::GetBytes($targetDeciRpm).CopyTo($command, 26)
    [BitConverter]::GetBytes($targetDeciRpm).CopyTo($command, 28)
    [BitConverter]::GetBytes($DurationMs).CopyTo($command, 30)
    [BitConverter]::GetBytes([uint16]1).CopyTo($command, 32)
    $crc = Get-Crc16Ccitt -Data $command -Offset 2 -Length 32
    [BitConverter]::GetBytes($crc).CopyTo($command, 34)
    return $command
}

$initialSequence = [uint32](Get-Random -Minimum 1 -Maximum 2000000000)
$finalSequence = $initialSequence + [uint32]1
$initialCommand = New-SpeedCommand -Sequence $initialSequence `
    -TargetRpm $InitialRpm -DurationMs ([uint16]$durationMs)
$finalDurationMs = [uint16]($durationMs - $StepAtMilliseconds)
$finalCommand = New-SpeedCommand -Sequence $finalSequence `
    -TargetRpm $FinalRpm -DurationMs $finalDurationMs

$serialPort = [System.IO.Ports.SerialPort]::new(
    $Port, $BaudRate, [System.IO.Ports.Parity]::None, 8,
    [System.IO.Ports.StopBits]::One)
$serialPort.ReadBufferSize = 1MB
$serialPort.ReadTimeout = 100
$captureSeconds = $DurationSeconds + 3
$capture = [System.IO.MemoryStream]::new($captureSeconds * 8192)
$readBuffer = New-Object byte[] 4096
$initialSent = $false
$finalSent = $false

try {
    $serialPort.Open()
    $serialPort.DiscardInBuffer()
    Start-Sleep -Milliseconds 100
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.Elapsed.TotalSeconds -lt $captureSeconds) {
        if (-not $initialSent -and
            $stopwatch.Elapsed.TotalMilliseconds -ge 500) {
            $serialPort.Write($initialCommand, 0, $initialCommand.Length)
            $initialSent = $true
        }
        if ($initialSent -and -not $finalSent -and
            $stopwatch.Elapsed.TotalMilliseconds -ge
                (500 + $StepAtMilliseconds)) {
            $serialPort.Write($finalCommand, 0, $finalCommand.Length)
            $finalSent = $true
        }
        while ($serialPort.BytesToRead -gt 0) {
            $count = [Math]::Min(
                $serialPort.BytesToRead, $readBuffer.Length)
            $read = $serialPort.Read($readBuffer, 0, $count)
            $capture.Write($readBuffer, 0, $read)
        }
        Start-Sleep -Milliseconds 2
    }
}
finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
    $serialPort.Dispose()
}
if (-not $initialSent -or -not $finalSent) {
    throw "Both speed commands were not sent."
}
[System.IO.File]::WriteAllBytes($rawPath, $capture.ToArray())
$capture.Dispose()

& $captureTool -InputPath $rawPath -CsvPath $csvPath -JsonPath $jsonPath |
    Out-Host
if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
    throw "Telemetry validation failed with exit code $LASTEXITCODE."
}

$summary = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
$rows = @(Import-Csv -LiteralPath $csvPath)
$speedRows = @($rows | Where-Object {
    (([uint32]$_.flags -band 32) -ne 0)
})
$stepIndex = -1
for ($index = 1; $index -lt $speedRows.Count; $index++) {
    if ([Math]::Abs([double]$speedRows[$index].setpoint - $FinalRpm) -lt 0.05 -and
        [Math]::Abs([double]$speedRows[$index - 1].setpoint - $InitialRpm) -lt 0.05) {
        $stepIndex = $index
        break
    }
}
if ($stepIndex -lt 0) {
    throw "The target transition was not found in telemetry."
}

$delta = $FinalRpm - $InitialRpm
$threshold90 = $InitialRpm + 0.9 * $delta
function Find-T90Ms {
    param([string]$Property)

    for ($index = $stepIndex; $index -lt $speedRows.Count; $index++) {
        $value = [double]$speedRows[$index].$Property
        if (($delta -gt 0.0 -and $value -ge $threshold90) -or
            ($delta -lt 0.0 -and $value -le $threshold90)) {
            return ($index - $stepIndex) * 10
        }
    }
    return -1
}

$postRows = @($speedRows | Select-Object -Skip $stepIndex)
$tailRows = @($postRows | Select-Object -Last 200)
$leftT90 = Find-T90Ms -Property "measurement"
$rightT90 = Find-T90Ms -Property "control_output"
$leftPeak = if ($delta -gt 0.0) {
    ($postRows | Measure-Object measurement -Maximum).Maximum
} else { ($postRows | Measure-Object measurement -Minimum).Minimum }
$rightPeak = if ($delta -gt 0.0) {
    ($postRows | Measure-Object control_output -Maximum).Maximum
} else { ($postRows | Measure-Object control_output -Minimum).Minimum }
$leftOvershoot = if ($delta -gt 0.0) {
    ($leftPeak - $FinalRpm) / [Math]::Abs($delta) * 100.0
} else {
    ($FinalRpm - $leftPeak) / [Math]::Abs($delta) * 100.0
}
$rightOvershoot = if ($delta -gt 0.0) {
    ($rightPeak - $FinalRpm) / [Math]::Abs($delta) * 100.0
} else {
    ($FinalRpm - $rightPeak) / [Math]::Abs($delta) * 100.0
}
$ack = $summary.LatestActuatorAck
$health = $summary.LatestHealth
$result = [pscustomobject]@{
    InitialRpm = $InitialRpm
    FinalRpm = $FinalRpm
    StepAtMilliseconds = $StepAtMilliseconds
    LeftT90Ms = $leftT90
    RightT90Ms = $rightT90
    T90SkewMs = if ($leftT90 -ge 0 -and $rightT90 -ge 0) {
        [Math]::Abs($leftT90 - $rightT90)
    } else { -1 }
    LeftOvershootPct = [Math]::Round(
        [Math]::Max(0.0, $leftOvershoot), 2)
    RightOvershootPct = [Math]::Round(
        [Math]::Max(0.0, $rightOvershoot), 2)
    LeftTailMeanRpm = [Math]::Round(
        ($tailRows | Measure-Object measurement -Average).Average, 3)
    RightTailMeanRpm = [Math]::Round(
        ($tailRows | Measure-Object control_output -Average).Average, 3)
    LatestAckSequence = if ($null -ne $ack) { $ack.Sequence } else { 0 }
    ExpectedAckSequence = $finalSequence
    EncoderIsrLateCount = if ($null -ne $health) {
        $health.EncoderIsrLateCount
    } else { 0 }
    ActiveIssueMask = if ($null -ne $health) {
        $health.ActiveIssueMask
    } else { "missing" }
    StickyIssueMask = if ($null -ne $health) {
        $health.StickyIssueMask
    } else { "missing" }
    CrcErrors = $summary.CrcErrors
    SequenceGaps = $summary.SequenceGaps
    DeadlineMissCount = $summary.DeadlineMissCount
    OutputDirectory = $OutputDirectory
}
$result | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8
$result | Format-List
