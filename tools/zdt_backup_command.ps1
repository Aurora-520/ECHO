[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [ValidateSet("Status", "Select", "Deselect", "Enable", "Disable",
        "Speed", "Position", "Stop")]
    [string]$Action = "Status",
    [ValidateSet("Gen1", "Gen2", "Both")]
    [string]$Axis = "Gen2",
    [ValidateRange(-3000, 3000)]
    [int]$SpeedRpm = 0,
    [ValidateRange(-2147483647, 2147483647)]
    [int]$PositionMillidegrees = 0,
    [ValidateRange(1, 3000)]
    [int]$PositionSpeedRpm = 30,
    [ValidateRange(0, 20000)]
    [int]$AccelerationRpmPerSecond = 500,
    [ValidateSet("RelativeLast", "Absolute", "RelativeCurrent")]
    [string]$PositionMode = "Absolute",
    [ValidateRange(1, 4294967294)]
    [uint32]$Sequence = 0,
    [ValidateRange(0.2, 10)]
    [double]$CaptureSeconds = 1,
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmMechanismSuspended,
    [switch]$ConfirmCurrentLimitedSupply,
    [switch]$ConfirmPhysicalDisconnectReady,
    [switch]$ConfirmBothMotorsConnected,
    [switch]$PassThru
)

$ErrorActionPreference = "Stop"

$operationMap = @{
    Status = 0; Select = 1; Deselect = 2; Enable = 3
    Disable = 3; Speed = 4; Position = 5; Stop = 6
}
$axisMap = @{ Gen1 = 0; Gen2 = 1; Both = 2 }
$positionModeMap = @{ RelativeLast = 0; Absolute = 1; RelativeCurrent = 2 }
$motionAction = $Action -in @("Enable", "Speed", "Position")
if ($motionAction -and
    (-not $ConfirmUserPresent -or
     -not $ConfirmMechanismSuspended -or
     -not $ConfirmCurrentLimitedSupply -or
     -not $ConfirmPhysicalDisconnectReady)) {
    throw "Motion requires all four physical safety confirmations."
}
if ($motionAction -and $Axis -eq "Both" -and
    -not $ConfirmBothMotorsConnected) {
    throw "Both-axis motion requires ConfirmBothMotorsConnected."
}
if ($Action -eq "Speed" -and $SpeedRpm -eq 0) {
    throw "Use Stop instead of a zero-speed command."
}
if ($Sequence -eq 0) {
    $Sequence = [uint32](Get-Random -Minimum 1 -Maximum 2147483647)
}

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

function Get-U16 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToUInt16($Data, $Offset)
}

function Get-U32 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToUInt32($Data, $Offset)
}

function Get-I16 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToInt16($Data, $Offset)
}

function Get-I32 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToInt32($Data, $Offset)
}

$operation = [byte]$operationMap[$Action]
$axisValue = [byte]$axisMap[$Axis]
$modeValue = [byte]$positionModeMap[$PositionMode]
$value = switch ($Action) {
    "Enable" { 1 }
    "Disable" { 0 }
    "Speed" { $SpeedRpm }
    "Position" { $PositionMillidegrees }
    default { 0 }
}

$command = New-Object byte[] 44
$command[0] = 0xA5
$command[1] = 0x5A
$command[2] = 1
$command[3] = 8
[BitConverter]::GetBytes([uint16]28).CopyTo($command, 4)
[BitConverter]::GetBytes([uint32]1).CopyTo($command, 6)
[BitConverter]::GetBytes([uint32]0).CopyTo($command, 10)
$magic = [Convert]::ToUInt32("5A445442", 16)
$magicInverse = [Convert]::ToUInt32("A5BBABBD", 16)
[BitConverter]::GetBytes($magic).CopyTo($command, 14)
[BitConverter]::GetBytes($magicInverse).CopyTo($command, 18)
[BitConverter]::GetBytes($Sequence).CopyTo($command, 22)
$command[26] = $operation
$command[27] = $axisValue
$command[28] = $modeValue
$command[29] = 0
[BitConverter]::GetBytes([int32]$value).CopyTo($command, 30)
[BitConverter]::GetBytes([uint16]$PositionSpeedRpm).CopyTo($command, 34)
[BitConverter]::GetBytes([uint16]0).CopyTo($command, 36)
[BitConverter]::GetBytes([uint32]$AccelerationRpmPerSecond).CopyTo(
    $command, 38)
$commandCrc = Get-Crc16Ccitt -Data $command -Offset 2 -Length 40
[BitConverter]::GetBytes($commandCrc).CopyTo($command, 42)

$serial = [System.IO.Ports.SerialPort]::new(
    $Port, 230400, [System.IO.Ports.Parity]::None, 8,
    [System.IO.Ports.StopBits]::One)
$serial.ReadBufferSize = 1MB
$capture = [System.IO.MemoryStream]::new()
$buffer = New-Object byte[] 4096
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    Start-Sleep -Milliseconds 100
    $serial.Write($command, 0, $command.Length)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $CaptureSeconds) {
        $available = $serial.BytesToRead
        if ($available -gt 0) {
            $count = [Math]::Min($available, $buffer.Length)
            $read = $serial.Read($buffer, 0, $count)
            $capture.Write($buffer, 0, $read)
        }
        Start-Sleep -Milliseconds 2
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}

[byte[]]$bytes = $capture.ToArray()
$capture.Dispose()
$ack = $null
for ($index = 0; $index + 40 -le $bytes.Length; $index++) {
    $payloadLength = Get-U16 $bytes ($index + 4)
    $frameLength = 16 + $payloadLength
    if ($bytes[$index] -ne 0xA5 -or $bytes[$index + 1] -ne 0x5A -or
        $bytes[$index + 2] -ne 1 -or $bytes[$index + 3] -ne 9 -or
        $payloadLength -notin @(24, 104) -or
        $index + $frameLength -gt $bytes.Length) {
        continue
    }
    $expectedCrc = Get-U16 $bytes ($index + 14 + $payloadLength)
    $actualCrc = Get-Crc16Ccitt -Data $bytes -Offset ($index + 2) `
        -Length (12 + $payloadLength)
    if ($expectedCrc -ne $actualCrc -or
        (Get-U32 $bytes ($index + 14)) -ne $Sequence) {
        continue
    }
    $flags = $bytes[$index + 37]
    $ackData = [ordered]@{
        Sequence = $Sequence
        Action = $Action
        Axis = $Axis
        Status = $bytes[$index + 36]
        BackendSelected = [bool]($flags -band 0x01)
        ShutdownPending = [bool]($flags -band 0x02)
        Gen1Online = [bool]($flags -band 0x04)
        Gen2Online = [bool]($flags -band 0x08)
        Gen1Enabled = [bool]($flags -band 0x10)
        Gen2Enabled = [bool]($flags -band 0x20)
        Gen1MotionActive = [bool]($flags -band 0x40)
        Gen2MotionActive = [bool]($flags -band 0x80)
        Gen1ResponseCount = Get-U32 $bytes ($index + 22)
        Gen2ResponseCount = Get-U32 $bytes ($index + 26)
        Gen1TimeoutCount = Get-U16 $bytes ($index + 30)
        Gen2TimeoutCount = Get-U16 $bytes ($index + 32)
        CapturedBytes = $bytes.Length
    }
    if ($payloadLength -eq 104) {
        foreach ($axisIndex in 0..1) {
            $prefix = if ($axisIndex -eq 0) { "Gen1" } else { "Gen2" }
            $axisOffset = $index + 38 + (40 * $axisIndex)
            $ackData["${prefix}TxCommandCount"] = Get-U32 $bytes $axisOffset
            $ackData["${prefix}TxQueryCount"] = Get-U32 $bytes ($axisOffset + 4)
            $ackData["${prefix}InvalidResponseCount"] = Get-U32 $bytes ($axisOffset + 8)
            $ackData["${prefix}PositionReachedCount"] = Get-U32 $bytes ($axisOffset + 12)
            $ackData["${prefix}SpeedLeaseExpiredCount"] = Get-U32 $bytes ($axisOffset + 16)
            $ackData["${prefix}PositionCounts"] = Get-I32 $bytes ($axisOffset + 20)
            $ackData["${prefix}PositionMillidegrees"] = Get-I32 $bytes ($axisOffset + 24)
            $ackData["${prefix}SpeedRpm"] = Get-I16 $bytes ($axisOffset + 28)
            $ackData["${prefix}FirmwareVersion"] = Get-U16 $bytes ($axisOffset + 30)
            $ackData["${prefix}HardwareVersion"] = Get-U16 $bytes ($axisOffset + 32)
            $ackData["${prefix}MotorStatusFlags"] = $bytes[$axisOffset + 34]
            $ackData["${prefix}LastFunction"] = $bytes[$axisOffset + 35]
            $ackData["${prefix}LastReplyStatus"] = $bytes[$axisOffset + 36]
            $ackData["${prefix}Stalled"] = [bool]$bytes[$axisOffset + 37]
            $ackData["${prefix}StallProtected"] = [bool]$bytes[$axisOffset + 38]
        }
    }
    $ack = [pscustomobject]$ackData
    break
}

if ($null -eq $ack) {
    throw "No matching ZDT acknowledgment received on $Port."
}
if ($PassThru) {
    $ack
} else {
    $ack | Format-List
}
if ($ack.Status -ne 0) {
    exit 2
}
