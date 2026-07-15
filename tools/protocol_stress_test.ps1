param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(100, 2000)]
    [int]$TimeoutMilliseconds = 300
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
            } else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return [uint16]$crc
}

function New-ParameterFrame {
    param(
        [uint32]$TransactionId,
        [uint16]$ParameterId,
        [single]$Value,
        [switch]$CorruptCrc
    )

    $frame = New-Object byte[] 28
    $frame[0] = 0xA5
    $frame[1] = 0x5A
    $frame[2] = 1
    $frame[3] = 2
    [BitConverter]::GetBytes([uint16]12).CopyTo($frame, 4)
    [BitConverter]::GetBytes([uint32]1).CopyTo($frame, 6)
    [BitConverter]::GetBytes([uint32]0).CopyTo($frame, 10)
    [BitConverter]::GetBytes($TransactionId).CopyTo($frame, 14)
    [BitConverter]::GetBytes($ParameterId).CopyTo($frame, 18)
    $frame[20] = 1
    $frame[21] = 0
    [BitConverter]::GetBytes($Value).CopyTo($frame, 22)
    $crc = Get-Crc16Ccitt -Data $frame -Offset 2 -Length 24
    [BitConverter]::GetBytes($crc).CopyTo($frame, 26)
    if ($CorruptCrc) {
        $frame[26] = [byte]($frame[26] -bxor 0xFF)
    }
    return ,$frame
}

function Find-Acks {
    param(
        [byte[]]$Data,
        [uint32]$TransactionId
    )

    $acks = New-Object System.Collections.Generic.List[object]
    $offset = 0
    while (($offset + 16) -le $Data.Length) {
        if (($Data[$offset] -ne 0xA5) -or
            ($Data[$offset + 1] -ne 0x5A)) {
            $offset++
            continue
        }
        $version = $Data[$offset + 2]
        $type = $Data[$offset + 3]
        $payloadLength = [BitConverter]::ToUInt16($Data, $offset + 4)
        if (($version -ne 1) -or ($payloadLength -gt 128)) {
            $offset++
            continue
        }
        $frameLength = 16 + $payloadLength
        if (($offset + $frameLength) -gt $Data.Length) {
            break
        }
        $expected = Get-Crc16Ccitt -Data $Data -Offset ($offset + 2) -Length (12 + $payloadLength)
        $received = [BitConverter]::ToUInt16(
            $Data, $offset + 14 + $payloadLength)
        if (($expected -eq $received) -and
            ($type -eq 3) -and ($payloadLength -eq 16) -and
            ([BitConverter]::ToUInt32($Data, $offset + 14) -eq
                $TransactionId)) {
            $acks.Add([pscustomobject]@{
                TransactionId = [BitConverter]::ToUInt32(
                    $Data, $offset + 14)
                ParameterId = [BitConverter]::ToUInt16(
                    $Data, $offset + 18)
                Status = $Data[$offset + 20]
                Reserved = $Data[$offset + 21]
                AppliedValue = [BitConverter]::ToSingle(
                    $Data, $offset + 22)
                ApplySequence = [BitConverter]::ToUInt32(
                    $Data, $offset + 26)
            })
        }
        $offset += $frameLength
    }
    return ,$acks.ToArray()
}

function Send-AndReadAck {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [byte[]]$Frame,
        [uint32]$TransactionId,
        [int]$TimeoutMilliseconds
    )

    $SerialPort.DiscardInBuffer()
    $SerialPort.Write($Frame, 0, $Frame.Length)
    $bytes = New-Object System.Collections.Generic.List[byte]
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $available = $SerialPort.BytesToRead
        if ($available -gt 0) {
            $chunk = New-Object byte[] $available
            $read = $SerialPort.Read($chunk, 0, $available)
            for ($index = 0; $index -lt $read; $index++) {
                $bytes.Add($chunk[$index])
            }
            $acks = Find-Acks -Data $bytes.ToArray() -TransactionId $TransactionId
            if ($acks.Count -gt 0) {
                return ,$acks
            }
        }
        Start-Sleep -Milliseconds 2
    }
    return ,@()
}

function Read-SerialBytes {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$TimeoutMilliseconds
    )

    $bytes = New-Object System.Collections.Generic.List[byte]
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $available = $SerialPort.BytesToRead
        if ($available -gt 0) {
            $chunk = New-Object byte[] $available
            $read = $SerialPort.Read($chunk, 0, $available)
            for ($index = 0; $index -lt $read; $index++) {
                $bytes.Add($chunk[$index])
            }
        }
        Start-Sleep -Milliseconds 2
    }
    return ,$bytes.ToArray()
}

function Assert-Ack {
    param(
        [object[]]$Acks,
        [byte]$ExpectedStatus,
        [uint16]$ExpectedParameterId,
        [string]$Name
    )

    if (($null -eq $Acks) -or ($Acks.Count -eq 0)) {
        throw "$($Name): no ACK"
    }
    $ack = $Acks[$Acks.Count - 1]
    if (($ack.Status -ne $ExpectedStatus) -or
        ($ack.ParameterId -ne $ExpectedParameterId) -or
        ($ack.Reserved -ne 0)) {
        throw "$($Name): unexpected ACK status=$($ack.Status) id=$($ack.ParameterId)"
    }
    return $ack
}

$portObject = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$portObject.ReadBufferSize = 1048576
$portObject.ReadTimeout = 100

try {
    $portObject.Open()

    $invalidIdTx = [uint32]0x71000001
    $frame = New-ParameterFrame -TransactionId $invalidIdTx -ParameterId 0x0101 -Value ([single]1.0)
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $frame -TransactionId $invalidIdTx -TimeoutMilliseconds $TimeoutMilliseconds
    $ack = Assert-Ack -Acks $acks -ExpectedStatus 1 -ExpectedParameterId 0x0101 -Name "invalid parameter id"
    Write-Output "invalid parameter id: status=$($ack.Status)"

    $nanTx = [uint32]0x71000002
    $frame = New-ParameterFrame -TransactionId $nanTx -ParameterId 1 -Value ([single]::NaN)
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $frame -TransactionId $nanTx -TimeoutMilliseconds $TimeoutMilliseconds
    $ack = Assert-Ack -Acks $acks -ExpectedStatus 2 -ExpectedParameterId 1 -Name "NaN"
    Write-Output "NaN: status=$($ack.Status)"

    $infTx = [uint32]0x71000003
    $frame = New-ParameterFrame -TransactionId $infTx -ParameterId 1 -Value ([single]::PositiveInfinity)
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $frame -TransactionId $infTx -TimeoutMilliseconds $TimeoutMilliseconds
    $ack = Assert-Ack -Acks $acks -ExpectedStatus 2 -ExpectedParameterId 1 -Name "Inf"
    Write-Output "Inf: status=$($ack.Status)"

    $negativeKpTx = [uint32]0x71000007
    $frame = New-ParameterFrame -TransactionId $negativeKpTx -ParameterId 1 -Value ([single]-0.1)
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $frame -TransactionId $negativeKpTx -TimeoutMilliseconds $TimeoutMilliseconds
    $ack = Assert-Ack -Acks $acks -ExpectedStatus 2 -ExpectedParameterId 1 -Name "negative KP"
    Write-Output "negative KP: status=$($ack.Status)"

    $targetHighTx = [uint32]0x71000008
    $frame = New-ParameterFrame -TransactionId $targetHighTx -ParameterId 4 -Value ([single]10001.0)
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $frame -TransactionId $targetHighTx -TimeoutMilliseconds $TimeoutMilliseconds
    $ack = Assert-Ack -Acks $acks -ExpectedStatus 2 -ExpectedParameterId 4 -Name "target above maximum"
    Write-Output "target above maximum: status=$($ack.Status)"

    $badCrcTx = [uint32]0x71000004
    $frame = New-ParameterFrame -TransactionId $badCrcTx -ParameterId 1 -Value ([single]2.0) -CorruptCrc
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $frame -TransactionId $badCrcTx -TimeoutMilliseconds 150
    if (($null -ne $acks) -and ($acks.Count -gt 0)) {
        throw "CRC corruption unexpectedly produced an ACK."
    }
    Write-Output "CRC corruption: no ACK"

    $recoveryTx = [uint32]0x71000005
    $recoveryFrame = New-ParameterFrame -TransactionId $recoveryTx -ParameterId 1 -Value ([single]2.0)
    $portObject.DiscardInBuffer()
    $portObject.Write($recoveryFrame, 0, 12)
    Start-Sleep -Milliseconds 80
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $recoveryFrame -TransactionId $recoveryTx -TimeoutMilliseconds $TimeoutMilliseconds
    $ack = Assert-Ack -Acks $acks -ExpectedStatus 0 -ExpectedParameterId 1 -Name "truncated frame recovery"
    Write-Output "truncated frame recovery: sequence=$($ack.ApplySequence)"

    $busyTransactions = @()
    $burst = New-Object byte[] (8 * 28)
    for ($index = 0; $index -lt 8; $index++) {
        $busyTx = [uint32](0x73000000 + $index)
        $busyTransactions += $busyTx
        $busyFrame = New-ParameterFrame -TransactionId $busyTx `
            -ParameterId 2 -Value ([single](0.25 + (0.01 * $index)))
        $busyFrame.CopyTo($burst, $index * $busyFrame.Length)
    }
    $portObject.DiscardInBuffer()
    $portObject.Write($burst, 0, $burst.Length)
    $busyResponse = Read-SerialBytes -SerialPort $portObject `
        -TimeoutMilliseconds ([Math]::Max(1000, $TimeoutMilliseconds))
    $busyApplied = 0
    $busyRejected = 0
    $busyUnexpected = 0
    foreach ($busyTx in $busyTransactions) {
        $busyAcks = Find-Acks -Data $busyResponse -TransactionId $busyTx
        foreach ($busyAck in $busyAcks) {
            if (($busyAck.ParameterId -ne 2) -or ($busyAck.Reserved -ne 0)) {
                $busyUnexpected++
            }
            elseif ($busyAck.Status -eq 0) {
                $busyApplied++
            }
            elseif ($busyAck.Status -eq 4) {
                $busyRejected++
            }
            else {
                $busyUnexpected++
            }
        }
    }
    if (($busyApplied -lt 1) -or ($busyRejected -lt 1) -or
        ($busyUnexpected -ne 0)) {
        throw "Busy burst did not produce valid APPLIED and BUSY responses."
    }
    Write-Output "busy burst: applied=$busyApplied, busy=$busyRejected"

    $duplicateTx = [uint32]0x71000006
    $duplicateFrame = New-ParameterFrame -TransactionId $duplicateTx -ParameterId 1 -Value ([single]2.25)
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $duplicateFrame -TransactionId $duplicateTx -TimeoutMilliseconds $TimeoutMilliseconds
    $first = Assert-Ack -Acks $acks -ExpectedStatus 0 -ExpectedParameterId 1 -Name "duplicate first"
    $acks = Send-AndReadAck -SerialPort $portObject -Frame $duplicateFrame -TransactionId $duplicateTx -TimeoutMilliseconds $TimeoutMilliseconds
    $second = Assert-Ack -Acks $acks -ExpectedStatus 0 -ExpectedParameterId 1 -Name "duplicate second"
    if ($first.ApplySequence -ne $second.ApplySequence) {
        throw "Duplicate transaction applied twice."
    }
    Write-Output "duplicate transaction: apply_sequence=$($second.ApplySequence)"

    $firstBurstSequence = $null
    $lastBurstSequence = $null
    for ($index = 0; $index -lt 50; $index++) {
        $transactionId = [uint32](0x72000000 + $index)
        $value = [single](1.0 + (0.01 * $index))
        $frame = New-ParameterFrame -TransactionId $transactionId -ParameterId 1 -Value $value
        $acks = @()
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            $acks = Send-AndReadAck -SerialPort $portObject -Frame $frame -TransactionId $transactionId -TimeoutMilliseconds $TimeoutMilliseconds
            if (($null -ne $acks) -and ($acks.Count -gt 0)) {
                break
            }
        }
        $ack = Assert-Ack -Acks $acks -ExpectedStatus 0 -ExpectedParameterId 1 -Name "continuous tuning $index"
        if ($null -eq $firstBurstSequence) {
            $firstBurstSequence = $ack.ApplySequence
        }
        $lastBurstSequence = $ack.ApplySequence
    }
    if (($lastBurstSequence - $firstBurstSequence) -ne 49) {
        throw "Continuous tuning retries applied a transaction more than once."
    }
    Write-Output "continuous tuning: 50/50 applied, sequence $firstBurstSequence..$lastBurstSequence"
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
