param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateSet("kp", "ki", "kd", "target")]
    [string]$Parameter = "kp",
    [double]$Value = 0.0,
    [ValidateRange(1, 4294967294)]
    [uint32]$TransactionId = 0,
    [ValidateRange(100, 5000)]
    [int]$TimeoutMilliseconds = 1000
)

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

function Find-ParameterAck {
    param(
        [byte[]]$Data,
        [uint32]$ExpectedTransactionId,
        [uint16]$ExpectedParameterId
    )

    $offset = 0
    $ack = $null
    $crcErrors = 0
    $invalidAcks = 0
    while (($offset + 16) -le $Data.Length) {
        if (($Data[$offset] -ne 0xA5) -or
            ($Data[$offset + 1] -ne 0x5A)) {
            $offset++
            continue
        }

        $version = $Data[$offset + 2]
        $frameType = $Data[$offset + 3]
        $payloadLength = [BitConverter]::ToUInt16($Data, $offset + 4)
        if (($version -ne 1) -or ($payloadLength -gt 128)) {
            $offset++
            continue
        }
        $frameLength = 16 + $payloadLength
        if (($offset + $frameLength) -gt $Data.Length) {
            break
        }

        $expectedCrc = Get-Crc16Ccitt -Data $Data -Offset ($offset + 2) -Length (12 + $payloadLength)
        $receivedCrc = [BitConverter]::ToUInt16(
            $Data, $offset + 14 + $payloadLength)
        if ($expectedCrc -ne $receivedCrc) {
            $crcErrors++
            $offset++
            continue
        }

        if (($frameType -eq 3) -and
            ($payloadLength -eq 16) -and
            ([BitConverter]::ToUInt32($Data, $offset + 14) -eq
                $ExpectedTransactionId)) {
            $parameterId = [BitConverter]::ToUInt16(
                $Data, $offset + 18)
            $status = $Data[$offset + 20]
            $reserved = $Data[$offset + 21]
            $appliedValue = [BitConverter]::ToSingle(
                $Data, $offset + 22)
            $isFinite = -not (
                [single]::IsNaN($appliedValue) -or
                [single]::IsInfinity($appliedValue)
            )
            if (($parameterId -ne $ExpectedParameterId) -or
                ($reserved -ne 0) -or ($status -gt 4) -or
                (-not $isFinite)) {
                $invalidAcks++
            }
            else {
                $ack = [pscustomobject]@{
                    TransactionId = $ExpectedTransactionId
                    ParameterId = $parameterId
                    Status = $status
                    AppliedValue = $appliedValue
                    ApplySequence = [BitConverter]::ToUInt32(
                        $Data, $offset + 26)
                }
            }
        }
        $offset += $frameLength
    }

    return [pscustomobject]@{
        Ack = $ack
        CrcErrors = $crcErrors
        InvalidAcks = $invalidAcks
    }
}

if ($TransactionId -eq 0) {
    $TransactionId = [uint32](Get-Random -Minimum 1 -Maximum 2147483647)
}

$parameterIds = @{
    kp = 1
    ki = 2
    kd = 3
    target = 4
}
$parameterId = [uint16]$parameterIds[$Parameter]
if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value)) {
    throw "Value must be finite."
}
if (($Parameter -ne "target") -and (($Value -lt 0) -or ($Value -gt 1000))) {
    throw "kp/ki/kd must be in [0, 1000]."
}
if (($Parameter -eq "target") -and (($Value -lt -10000) -or ($Value -gt 10000))) {
    throw "target must be in [-10000, 10000]."
}

$requestedValue = [single]$Value
$command = New-Object byte[] 28
$command[0] = 0xA5
$command[1] = 0x5A
$command[2] = 1
$command[3] = 2
[BitConverter]::GetBytes([uint16]12).CopyTo($command, 4)
[BitConverter]::GetBytes([uint32]1).CopyTo($command, 6)
[BitConverter]::GetBytes([uint32]0).CopyTo($command, 10)
[BitConverter]::GetBytes($TransactionId).CopyTo($command, 14)
[BitConverter]::GetBytes($parameterId).CopyTo($command, 18)
$command[20] = 1
$command[21] = 0
[BitConverter]::GetBytes($requestedValue).CopyTo($command, 22)
$commandCrc = Get-Crc16Ccitt -Data $command -Offset 2 -Length 24
[BitConverter]::GetBytes($commandCrc).CopyTo($command, 26)

$portObject = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$portObject.ReadBufferSize = 1048576
$portObject.ReadTimeout = 100
$received = [System.IO.MemoryStream]::new()

$ack = $null
$parseResult = $null
$attemptsUsed = 0
try {
    $portObject.Open()
    $portObject.DiscardInBuffer()
    Start-Sleep -Milliseconds 20

    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $attemptsUsed = $attempt
        $received.SetLength(0)
        $received.Position = 0
        $portObject.Write($command, 0, $command.Length)

        $deadline = [DateTime]::UtcNow.AddMilliseconds(
            $TimeoutMilliseconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            $available = $portObject.BytesToRead
            if ($available -gt 0) {
                $buffer = New-Object byte[] $available
                $read = $portObject.Read($buffer, 0, $available)
                $received.Write($buffer, 0, $read)
                $parseResult = Find-ParameterAck -Data $received.ToArray() -ExpectedTransactionId $TransactionId -ExpectedParameterId $parameterId
                if ($null -ne $parseResult.Ack) {
                    $ack = $parseResult.Ack
                    break
                }
            }
            Start-Sleep -Milliseconds 2
        }

        if (($null -ne $ack) -and ($ack.Status -ne 4)) {
            break
        }
        if (($null -ne $ack) -and ($attempt -lt 3)) {
            $ack = $null
        }
    }
}
catch [System.UnauthorizedAccessException] {
    throw "Cannot open $Port. Close UartAssist and any other serial monitor first."
}
finally {
    if ($portObject.IsOpen) {
        $portObject.Close()
    }
    $portObject.Dispose()
}

$data = $received.ToArray()
$received.Dispose()
$parseResult = Find-ParameterAck -Data $data -ExpectedTransactionId $TransactionId -ExpectedParameterId $parameterId
if (($null -eq $ack) -and ($null -ne $parseResult.Ack)) {
    $ack = $parseResult.Ack
}
$crcErrors = $parseResult.CrcErrors
$invalidAcks = $parseResult.InvalidAcks

$ackStatus = if ($null -eq $ack) { "TIMEOUT" } else { $ack.Status }
$appliedValue = if ($null -eq $ack) { $null } else { $ack.AppliedValue }
$applySequence = if ($null -eq $ack) { $null } else { $ack.ApplySequence }
$valueMismatch = ($null -ne $ack) -and ($ack.Status -eq 0) -and
    ([Math]::Abs([double]$ack.AppliedValue -
        [double]$requestedValue) -gt 0.0001)
[pscustomobject]@{
    Port = $Port
    BaudRate = $BaudRate
    Parameter = $Parameter
    RequestedValue = $requestedValue
    TransactionId = $TransactionId
    Attempts = $attemptsUsed
    AckStatus = $ackStatus
    AppliedValue = $appliedValue
    ApplySequence = $applySequence
    ValueMismatch = $valueMismatch
    ParserCrcErrors = $crcErrors
    InvalidAcks = $invalidAcks
} | Format-List

if (($null -eq $ack) -or ($ack.Status -ne 0) -or
    ($crcErrors -ne 0) -or ($invalidAcks -ne 0) -or $valueMismatch) {
    exit 2
}
