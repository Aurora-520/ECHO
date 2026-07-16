[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [Parameter(Mandatory = $true)]
    [ValidateSet("Left", "Right", "Both")]
    [string]$Motor,
    [Parameter(Mandatory = $true)]
    [ValidateSet("LogicOnly", "MotorPowered")]
    [string]$Mode,
    [ValidateRange(-500, 500)]
    [int]$ElectricalPermille = 50,
    [ValidateRange(50, 1000)]
    [int]$DurationMs = 200,
    [ValidateRange(3, 10)]
    [int]$CaptureSeconds = 4,
    [ValidateRange(1, 4294967294)]
    [uint32]$Sequence = 0,
    [string]$OutputDirectory = "",
    [switch]$ConfirmVmDisconnected,
    [switch]$ConfirmMotorOutputsDisconnected,
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmWheelSuspended,
    [switch]$ConfirmOtherMotorDisconnected,
    [switch]$ConfirmBothMotorsConnected,
    [switch]$ReverseRight,
    [switch]$ConfirmCurrentLimitedSupply,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

if ($ElectricalPermille -eq 0) {
    throw "ElectricalPermille must be non-zero for a pulse test."
}
if ($ReverseRight -and $Motor -ne "Both") {
    throw "ReverseRight is only valid when Motor is Both."
}
if ($Mode -eq "LogicOnly") {
    if (-not $ConfirmVmDisconnected -or
        -not $ConfirmMotorOutputsDisconnected) {
        throw "LogicOnly requires VM disconnected and all motor outputs disconnected."
    }
}
else {
    if (-not $ConfirmUserPresent -or
        -not $ConfirmWheelSuspended -or
        -not $ConfirmCurrentLimitedSupply -or
        -not $ConfirmPhysicalDisconnectReady) {
        throw "MotorPowered requires every physical safety confirmation."
    }
    if ($Motor -eq "Both") {
        if (-not $ConfirmBothMotorsConnected) {
            throw "Both motors require ConfirmBothMotorsConnected."
        }
    }
    elseif (-not $ConfirmOtherMotorDisconnected) {
        throw "Single-motor tests require ConfirmOtherMotorDisconnected."
    }
}

if ($Sequence -eq 0) {
    $Sequence = [uint32](Get-Random -Minimum 1 -Maximum 2147483647)
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $EchoPaths.ProjectRoot "tests\artifacts\phase2a-pulse-$timestamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$rawPath = Join-Path $OutputDirectory "capture.bin"
$csvPath = Join-Path $OutputDirectory "control.csv"
$jsonPath = Join-Path $OutputDirectory "capture.json"
$resultPath = Join-Path $OutputDirectory "pulse-result.json"
$captureTool = Join-Path $PSScriptRoot "telemetry_capture.ps1"
$hexPath = Join-Path $EchoPaths.ProjectRoot "keil\Objects\ECHO.hex"

foreach ($requiredPath in @($captureTool, $hexPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
}

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

function Set-I16 {
    param([byte[]]$Data, [int]$Offset, [int16]$Value)

    [BitConverter]::GetBytes($Value).CopyTo($Data, $Offset)
}

$leftPermille = [int16]0
$rightPermille = [int16]0
if ($Motor -eq "Left") {
    $leftPermille = [int16]$ElectricalPermille
}
elseif ($Motor -eq "Right") {
    $rightPermille = [int16]$ElectricalPermille
}
else {
    $leftPermille = [int16]$ElectricalPermille
    $rightPermille = if ($ReverseRight) {
        [int16](-$ElectricalPermille)
    } else {
        [int16]$ElectricalPermille
    }
}

$command = New-Object byte[] 36
$command[0] = 0xA5
$command[1] = 0x5A
$command[2] = 1
$command[3] = 5
[BitConverter]::GetBytes([uint16]20).CopyTo($command, 4)
[BitConverter]::GetBytes([uint32]1).CopyTo($command, 6)
[BitConverter]::GetBytes([uint32]0).CopyTo($command, 10)
$magic = [Convert]::ToUInt32("4543484F", 16)
$magicInverse = [Convert]::ToUInt32("BABCB7B0", 16)
[BitConverter]::GetBytes($magic).CopyTo($command, 14)
[BitConverter]::GetBytes($magicInverse).CopyTo($command, 18)
[BitConverter]::GetBytes($Sequence).CopyTo($command, 22)
Set-I16 -Data $command -Offset 26 -Value $leftPermille
Set-I16 -Data $command -Offset 28 -Value $rightPermille
[BitConverter]::GetBytes([uint16]$DurationMs).CopyTo($command, 30)
[BitConverter]::GetBytes([uint16]0).CopyTo($command, 32)
$commandCrc = Get-Crc16Ccitt -Data $command -Offset 2 -Length 32
[BitConverter]::GetBytes($commandCrc).CopyTo($command, 34)

$serialPort = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serialPort.ReadBufferSize = 1MB
$serialPort.ReadTimeout = 100
$capture = [System.IO.MemoryStream]::new($CaptureSeconds * 8192)
$readBuffer = New-Object byte[] 4096
$sent = $false

try {
    $serialPort.Open()
    $serialPort.DiscardInBuffer()
    Start-Sleep -Milliseconds 100

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.Elapsed.TotalSeconds -lt $CaptureSeconds) {
        if (-not $sent -and $stopwatch.Elapsed.TotalMilliseconds -ge 500) {
            $serialPort.Write($command, 0, $command.Length)
            $sent = $true
        }
        do {
            $available = $serialPort.BytesToRead
            if ($available -le 0) {
                break
            }
            $count = [Math]::Min($available, $readBuffer.Length)
            $read = $serialPort.Read($readBuffer, 0, $count)
            if ($read -le 0) {
                break
            }
            $capture.Write($readBuffer, 0, $read)
        } while ($serialPort.BytesToRead -gt 0)
        Start-Sleep -Milliseconds 2
    }
    $stopwatch.Stop()

    do {
        $available = $serialPort.BytesToRead
        if ($available -le 0) {
            break
        }
        $count = [Math]::Min($available, $readBuffer.Length)
        $read = $serialPort.Read($readBuffer, 0, $count)
        if ($read -gt 0) {
            $capture.Write($readBuffer, 0, $read)
        }
    } while ($read -gt 0)
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
    throw "The pulse command was not sent."
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
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$motorFlag = if ($Motor -eq "Left") {
    [uint32]8
} elseif ($Motor -eq "Right") {
    [uint32]16
} else {
    [uint32]24
}
$activeRows = @($rows | Where-Object {
    (([uint32]$_.flags -band $motorFlag) -eq $motorFlag)
})
$expectedActiveFrames = [int][Math]::Ceiling($DurationMs / 10.0)
$minimumActiveFrames = [Math]::Max(1, $expectedActiveFrames - 2)
$maximumActiveFrames = $expectedActiveFrames + 2

$leftAbsoluteCounts = 0.0
$rightAbsoluteCounts = 0.0
foreach ($row in $rows) {
    $leftAbsoluteCounts += [Math]::Abs(
        [double]::Parse($row.measurement, $culture))
    $rightAbsoluteCounts += [Math]::Abs(
        [double]::Parse($row.control_output, $culture))
}

$lastActiveIndex = -1
for ($index = 0; $index -lt $rows.Count; $index++) {
    if ((([uint32]$rows[$index].flags -band $motorFlag) -eq $motorFlag)) {
        $lastActiveIndex = $index
    }
}
$trailingSafeFrames = if ($lastActiveIndex -ge 0) {
    $rows.Count - $lastActiveIndex - 1
} else {
    0
}

$ack = $summary.LatestActuatorAck
$health = $summary.LatestHealth
$ackMatches = ($null -ne $ack) -and
    ([uint32]$ack.Sequence -eq $Sequence) -and
    ([int]$ack.LeftElectricalPermille -eq [int]$leftPermille) -and
    ([int]$ack.RightElectricalPermille -eq [int]$rightPermille) -and
    ([int]$ack.DurationMs -eq $DurationMs) -and
    ([int]$ack.Status -eq 0)
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
$activeWindowValid = ($activeRows.Count -ge $minimumActiveFrames) -and
    ($activeRows.Count -le $maximumActiveFrames) -and
    ($trailingSafeFrames -ge 10)
$logicEncoderStill = ($leftAbsoluteCounts -eq 0.0) -and
    ($rightAbsoluteCounts -eq 0.0)
$resultPassed = $ackMatches -and $healthClean -and $activeWindowValid
if ($Mode -eq "LogicOnly") {
    $resultPassed = $resultPassed -and $logicEncoderStill
}

$result = [pscustomobject]@{
    Result = if ($resultPassed) { "passed" } else { "failed" }
    Mode = $Mode
    Port = $Port
    Motor = $Motor
    ElectricalPermille = $ElectricalPermille
    LeftElectricalPermille = $leftPermille
    RightElectricalPermille = $rightPermille
    ReverseRight = [bool]$ReverseRight
    DurationMs = $DurationMs
    Sequence = $Sequence
    CommandCrc = ('0x{0:X4}' -f $commandCrc)
    HexSha256 = (Get-FileHash -LiteralPath $hexPath -Algorithm SHA256).Hash
    AckMatches = $ackMatches
    ActuatorAckFrames = $summary.ActuatorAckFrames
    ActiveControlFrames = $activeRows.Count
    ExpectedActiveFrames = $expectedActiveFrames
    TrailingSafeFrames = $trailingSafeFrames
    LeftAbsoluteCounts = $leftAbsoluteCounts
    RightAbsoluteCounts = $rightAbsoluteCounts
    EncoderResponseDetected =
        (($leftAbsoluteCounts + $rightAbsoluteCounts) -gt 0.0)
    LogicEncoderStill = $logicEncoderStill
    ControlRateHz = $summary.ControlRateHz
    HealthRateHz = $summary.HealthRateHz
    CrcErrors = $summary.CrcErrors
    SequenceGaps = $summary.SequenceGaps
    DeadlineMissCount = $summary.DeadlineMissCount
    HealthClean = $healthClean
    OutputDirectory = $OutputDirectory
}
$result | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8
$result | Format-List

if (-not $resultPassed) {
    exit 2
}
