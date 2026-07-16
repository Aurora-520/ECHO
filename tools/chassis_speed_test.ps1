[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [Parameter(Mandatory = $true)]
    [ValidateRange(-120.0, 120.0)]
    [double]$TargetRpm,
    [ValidateRange(1, 30)]
    [int]$DurationSeconds = 5,
    [ValidateRange(1, 4294967294)]
    [uint32]$Sequence = 0,
    [string]$OutputDirectory = "",
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmWheelSuspended,
    [switch]$ConfirmBothMotorsConnected,
    [switch]$ConfirmCurrentLimitedSupply,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

if ([Math]::Abs($TargetRpm) -lt 0.1) {
    throw "TargetRpm must have magnitude of at least 0.1 rpm."
}
if (-not $ConfirmUserPresent -or
    -not $ConfirmWheelSuspended -or
    -not $ConfirmBothMotorsConnected -or
    -not $ConfirmCurrentLimitedSupply -or
    -not $ConfirmPhysicalDisconnectReady) {
    throw "Speed tests require every physical safety confirmation."
}
if ($Sequence -eq 0) {
    $Sequence = [uint32](Get-Random -Minimum 1 -Maximum 2147483647)
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $EchoPaths.ProjectRoot `
        "tests\artifacts\phase2a-speed-$timestamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$rawPath = Join-Path $OutputDirectory "capture.bin"
$csvPath = Join-Path $OutputDirectory "control.csv"
$jsonPath = Join-Path $OutputDirectory "capture.json"
$resultPath = Join-Path $OutputDirectory "speed-result.json"
$captureTool = Join-Path $PSScriptRoot "telemetry_capture.ps1"
$hexPath = Join-Path $EchoPaths.ProjectRoot "keil\Objects\ECHO.hex"

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

$targetDeciRpm = [int16][Math]::Round(
    $TargetRpm * 10.0, [MidpointRounding]::AwayFromZero)
$durationMs = [uint16]($DurationSeconds * 1000)
$command = New-Object byte[] 36
$command[0] = 0xA5
$command[1] = 0x5A
$command[2] = 1
$command[3] = 5
[BitConverter]::GetBytes([uint16]20).CopyTo($command, 4)
[BitConverter]::GetBytes([uint32]1).CopyTo($command, 6)
[BitConverter]::GetBytes([uint32]0).CopyTo($command, 10)
[BitConverter]::GetBytes(
    [Convert]::ToUInt32("4543484F", 16)).CopyTo($command, 14)
[BitConverter]::GetBytes(
    [Convert]::ToUInt32("BABCB7B0", 16)).CopyTo($command, 18)
[BitConverter]::GetBytes($Sequence).CopyTo($command, 22)
[BitConverter]::GetBytes($targetDeciRpm).CopyTo($command, 26)
[BitConverter]::GetBytes($targetDeciRpm).CopyTo($command, 28)
[BitConverter]::GetBytes($durationMs).CopyTo($command, 30)
[BitConverter]::GetBytes([uint16]1).CopyTo($command, 32)
$commandCrc = Get-Crc16Ccitt -Data $command -Offset 2 -Length 32
[BitConverter]::GetBytes($commandCrc).CopyTo($command, 34)

$serialPort = [System.IO.Ports.SerialPort]::new(
    $Port, $BaudRate, [System.IO.Ports.Parity]::None, 8,
    [System.IO.Ports.StopBits]::One)
$serialPort.ReadBufferSize = 1MB
$serialPort.ReadTimeout = 100
$captureSeconds = $DurationSeconds + 3
$capture = [System.IO.MemoryStream]::new($captureSeconds * 8192)
$readBuffer = New-Object byte[] 4096
$sent = $false

try {
    $serialPort.Open()
    $serialPort.DiscardInBuffer()
    Start-Sleep -Milliseconds 100
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.Elapsed.TotalSeconds -lt $captureSeconds) {
        if (-not $sent -and $stopwatch.Elapsed.TotalMilliseconds -ge 500) {
            $serialPort.Write($command, 0, $command.Length)
            $sent = $true
        }
        while ($serialPort.BytesToRead -gt 0) {
            $count = [Math]::Min($serialPort.BytesToRead,
                $readBuffer.Length)
            $read = $serialPort.Read($readBuffer, 0, $count)
            $capture.Write($readBuffer, 0, $read)
        }
        Start-Sleep -Milliseconds 2
    }
}
catch [System.UnauthorizedAccessException] {
    throw "Cannot open $Port. Close every other serial monitor first."
}
finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
    $serialPort.Dispose()
}

if (-not $sent) {
    throw "The speed command was not sent."
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
$boostRows = @($speedRows | Where-Object {
    (([uint32]$_.flags -band 64) -ne 0)
})
$trackingRows = @($speedRows | Where-Object {
    (([uint32]$_.flags -band 128) -ne 0)
})
$expectedSpeedFrames = $DurationSeconds * 100
$minimumSpeedFrames = [Math]::Max(1, $expectedSpeedFrames - 2)
$maximumSpeedFrames = $expectedSpeedFrames + 2
$lastSpeedIndex = -1
for ($index = 0; $index -lt $rows.Count; $index++) {
    if ((([uint32]$rows[$index].flags -band 32) -ne 0)) {
        $lastSpeedIndex = $index
    }
}
$trailingSafeFrames = if ($lastSpeedIndex -ge 0) {
    $rows.Count - $lastSpeedIndex - 1
} else { 0 }
$durationComplete = ($speedRows.Count -ge $minimumSpeedFrames) -and
    ($speedRows.Count -le $maximumSpeedFrames) -and
    ($trailingSafeFrames -ge 10)
$tailCount = [Math]::Min(100, $trackingRows.Count)
$tailRows = @($trackingRows | Select-Object -Last $tailCount)
$leftStats = $tailRows | Measure-Object -Property measurement `
    -Average -Minimum -Maximum
$rightStats = $tailRows | Measure-Object -Property control_output `
    -Average -Minimum -Maximum
$leftStart = -1
$rightStart = -1
for ($index = 0; $index -lt $speedRows.Count; $index++) {
    if ($leftStart -lt 0 -and
        [Math]::Abs([double]$speedRows[$index].measurement) -ge 1.0) {
        $leftStart = $index * 10
    }
    if ($rightStart -lt 0 -and
        [Math]::Abs([double]$speedRows[$index].control_output) -ge 1.0) {
        $rightStart = $index * 10
    }
}

$ack = $summary.LatestActuatorAck
$health = $summary.LatestHealth
$ackMatches = ($null -ne $ack) -and
    ([uint32]$ack.Sequence -eq $Sequence) -and
    ([int]$ack.LeftElectricalPermille -eq [int]$targetDeciRpm) -and
    ([int]$ack.RightElectricalPermille -eq [int]$targetDeciRpm) -and
    ([int]$ack.DurationMs -eq [int]$durationMs) -and
    ([int]$ack.Status -eq 0) -and
    ([int]$ack.Reserved -eq 1)
$healthClean = ($null -ne $health) -and
    ($health.ActuatorOutputPermitted -eq 0) -and
    ($health.ActiveIssueMask -eq "0x00000000") -and
    ($health.StickyIssueMask -eq "0x00000000") -and
    ($health.DeadlineMissCount -eq 0) -and
    ($health.PublishDropCount -eq 0) -and
    ($health.TransportDropCount -eq 0) -and
    ($health.SerialTxDropCount -eq 0) -and
    ($health.SerialRxOverflowCount -eq 0) -and
    ($health.I2cErrorCount -eq 0)
$steadyToleranceRpm = if ([Math]::Abs($TargetRpm) -lt 8.0) {
    1.5
} else {
    [Math]::Max(1.0, [Math]::Abs($TargetRpm) * 0.03)
}
$steadyTrackingValid = ($tailRows.Count -gt 0) -and
    ([Math]::Abs($leftStats.Average - $TargetRpm) -le
        $steadyToleranceRpm) -and
    ([Math]::Abs($rightStats.Average - $TargetRpm) -le
        $steadyToleranceRpm)
$resultPassed = $ackMatches -and $healthClean -and
    ($boostRows.Count -gt 0) -and ($trackingRows.Count -gt 0) -and
    ($leftStart -ge 0) -and ($rightStart -ge 0) -and
    $durationComplete -and $steadyTrackingValid

$wheelCircumferenceM = [Math]::PI * 0.065
$result = [pscustomobject]@{
    Result = if ($resultPassed) { "passed" } else { "failed" }
    Port = $Port
    TargetRpm = $TargetRpm
    DurationSeconds = $DurationSeconds
    Sequence = $Sequence
    CommandCrc = ('0x{0:X4}' -f $commandCrc)
    HexSha256 = (Get-FileHash -LiteralPath $hexPath `
        -Algorithm SHA256).Hash
    AckMatches = $ackMatches
    SpeedFrames = $speedRows.Count
    ExpectedSpeedFrames = $expectedSpeedFrames
    DurationComplete = $durationComplete
    TrailingSafeFrames = $trailingSafeFrames
    BoostFrames = $boostRows.Count
    TrackingFrames = $trackingRows.Count
    LeftStartMs = $leftStart
    RightStartMs = $rightStart
    StartSkewMs = if ($leftStart -ge 0 -and $rightStart -ge 0) {
        [Math]::Abs($leftStart - $rightStart)
    } else { -1 }
    LeftAverageRpm = [Math]::Round($leftStats.Average, 3)
    RightAverageRpm = [Math]::Round($rightStats.Average, 3)
    LeftMinimumRpm = [Math]::Round($leftStats.Minimum, 3)
    LeftMaximumRpm = [Math]::Round($leftStats.Maximum, 3)
    RightMinimumRpm = [Math]::Round($rightStats.Minimum, 3)
    RightMaximumRpm = [Math]::Round($rightStats.Maximum, 3)
    SteadyToleranceRpm = [Math]::Round($steadyToleranceRpm, 3)
    SteadyTrackingValid = $steadyTrackingValid
    LeftAverageMps = [Math]::Round(
        $leftStats.Average * $wheelCircumferenceM / 60.0, 4)
    RightAverageMps = [Math]::Round(
        $rightStats.Average * $wheelCircumferenceM / 60.0, 4)
    ControlRateHz = $summary.ControlRateHz
    HealthRateHz = $summary.HealthRateHz
    CrcErrors = $summary.CrcErrors
    SequenceGaps = $summary.SequenceGaps
    DeadlineMissCount = $summary.DeadlineMissCount
    EncoderIsrLateCount = if ($null -ne $health.EncoderIsrLateCount) {
        [uint32]$health.EncoderIsrLateCount
    } else { 0 }
    HealthClean = $healthClean
    OutputDirectory = $OutputDirectory
}
$result | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8
$result | Format-List

if (-not $resultPassed) {
    exit 2
}
