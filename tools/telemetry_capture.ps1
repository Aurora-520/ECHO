param(
    [string]$Port = "COM4",
    [ValidateSet(115200, 460800, 921600)]
    [int]$BaudRate = 460800,
    [ValidateRange(1, 900)]
    [int]$DurationSeconds = 10,
    [ValidateRange(0, 10)]
    [int]$FlushSeconds = 1,
    [string]$CsvPath = ""
)

$ErrorActionPreference = "Stop"

$Sync0 = 0xA5
$Sync1 = 0x5A
$ProtocolVersion = 1
$ControlFrameType = 1
$ControlPayloadLength = 40
$MinimumFrameLength = 16
$MaximumPayloadLength = 128

function Get-Crc16Ccitt {
    param(
        [byte[]]$Data,
        [int]$Offset,
        [int]$Length
    )

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

function Get-U32Delta {
    param(
        [uint32]$Current,
        [uint32]$Previous
    )

    [int64]$modulus = 4294967296
    return [uint64]((([int64]$Current - [int64]$Previous + $modulus) % $modulus))
}
[uint64]$U32HalfRange = 2147483648

$serialPort = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serialPort.ReadBufferSize = 1MB
$serialPort.ReadTimeout = 100
$capture = [System.IO.MemoryStream]::new()

try {
    $serialPort.Open()

    if ($FlushSeconds -gt 0) {
        $flushDeadline = [DateTime]::UtcNow.AddSeconds($FlushSeconds)
        while ([DateTime]::UtcNow -lt $flushDeadline) {
            $serialPort.DiscardInBuffer()
            Start-Sleep -Milliseconds 20
        }
    }

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $available = $serialPort.BytesToRead
        if ($available -gt 0) {
            $buffer = New-Object byte[] $available
            $read = $serialPort.Read($buffer, 0, $available)
            $capture.Write($buffer, 0, $read)
        }
        Start-Sleep -Milliseconds 2
    }
    $stopwatch.Stop()

    $available = $serialPort.BytesToRead
    if ($available -gt 0) {
        $buffer = New-Object byte[] $available
        $read = $serialPort.Read($buffer, 0, $available)
        $capture.Write($buffer, 0, $read)
    }
}
finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
    $serialPort.Dispose()
}

$data = $capture.ToArray()
$capture.Dispose()

$csvWriter = $null
if (-not [string]::IsNullOrWhiteSpace($CsvPath)) {
    $resolvedCsvPath = [System.IO.Path]::GetFullPath($CsvPath)
    $csvDirectory = Split-Path -Parent $resolvedCsvPath
    if (-not [string]::IsNullOrWhiteSpace($csvDirectory)) {
        New-Item -ItemType Directory -Path $csvDirectory -Force | Out-Null
    }
    $csvWriter = [System.IO.StreamWriter]::new(
        $resolvedCsvPath,
        $false,
        [System.Text.UTF8Encoding]::new($false)
    )
    $csvWriter.WriteLine(
        "sequence,timestamp_us,setpoint,measurement,control_output," +
        "auxiliary,loop_count,period_us,execution_us,jitter_us," +
        "deadline_miss_count,flags"
    )
}

$culture = [System.Globalization.CultureInfo]::InvariantCulture
$offset = 0
$validFrames = 0
$crcErrors = 0
[uint64]$sequenceGaps = 0
$sequenceGapEvents = 0
$sequenceDuplicates = 0
$sequenceOutOfOrder = 0
$syncSkippedBytes = 0
$firstSequence = $null
$lastSequence = $null
$firstTimestamp = $null
$lastTimestamp = $null
$minimumPeriodUs = [uint32]::MaxValue
$maximumPeriodUs = 0
$maximumExecutionUs = 0
$maximumJitterUs = 0
$deadlineMissCount = 0

try {
    while (($offset + $MinimumFrameLength) -le $data.Length) {
        if (($data[$offset] -ne $Sync0) -or
            ($data[$offset + 1] -ne $Sync1)) {
            $offset++
            $syncSkippedBytes++
            continue
        }

        $version = $data[$offset + 2]
        $frameType = $data[$offset + 3]
        $payloadLength = [BitConverter]::ToUInt16($data, $offset + 4)
        if (($version -ne $ProtocolVersion) -or
            ($payloadLength -gt $MaximumPayloadLength)) {
            $offset++
            $syncSkippedBytes++
            continue
        }

        $frameLength = 16 + $payloadLength
        if (($offset + $frameLength) -gt $data.Length) {
            break
        }

        $receivedCrc =
            [BitConverter]::ToUInt16($data, $offset + 14 + $payloadLength)
        $calculatedCrc = Get-Crc16Ccitt -Data $data -Offset ($offset + 2) -Length (12 + $payloadLength)
        if ($receivedCrc -ne $calculatedCrc) {
            $crcErrors++
            $offset++
            continue
        }

        $sequence = [BitConverter]::ToUInt32($data, $offset + 6)
        $timestampUs = [BitConverter]::ToUInt32($data, $offset + 10)
        if ($null -eq $firstSequence) {
            $firstSequence = $sequence
            $firstTimestamp = $timestampUs
            $lastSequence = $sequence
            $lastTimestamp = $timestampUs
        }
        else {
            $sequenceDelta = Get-U32Delta -Current $sequence -Previous $lastSequence
            if ($sequenceDelta -eq 0) {
                $sequenceDuplicates++
            }
            elseif ($sequenceDelta -lt $U32HalfRange) {
                if ($sequenceDelta -gt 1) {
                    $sequenceGapEvents++
                    $sequenceGaps += [uint64]($sequenceDelta - 1)
                }
                $lastSequence = $sequence
                $lastTimestamp = $timestampUs
            }
            else {
                $sequenceOutOfOrder++
            }
        }
        $validFrames++

        if (($frameType -eq $ControlFrameType) -and
            ($payloadLength -eq $ControlPayloadLength)) {
            $payloadOffset = $offset + 14
            $setpoint = [BitConverter]::ToSingle($data, $payloadOffset)
            $measurement = [BitConverter]::ToSingle(
                $data, $payloadOffset + 4)
            $controlOutput = [BitConverter]::ToSingle(
                $data, $payloadOffset + 8)
            $auxiliary = [BitConverter]::ToSingle(
                $data, $payloadOffset + 12)
            $loopCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 16)
            $periodUs = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 20)
            $executionUs = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 24)
            $jitterUs = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 28)
            $deadlineMissCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 32)
            $flags = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 36)

            if (($periodUs -ne 0) -and ($periodUs -lt $minimumPeriodUs)) {
                $minimumPeriodUs = $periodUs
            }
            if ($periodUs -gt $maximumPeriodUs) {
                $maximumPeriodUs = $periodUs
            }
            if ($executionUs -gt $maximumExecutionUs) {
                $maximumExecutionUs = $executionUs
            }
            if ($jitterUs -gt $maximumJitterUs) {
                $maximumJitterUs = $jitterUs
            }

            if ($null -ne $csvWriter) {
                $values = @(
                    $sequence,
                    $timestampUs,
                    $setpoint.ToString("R", $culture),
                    $measurement.ToString("R", $culture),
                    $controlOutput.ToString("R", $culture),
                    $auxiliary.ToString("R", $culture),
                    $loopCount,
                    $periodUs,
                    $executionUs,
                    $jitterUs,
                    $deadlineMissCount,
                    $flags
                )
                $csvWriter.WriteLine($values -join ",")
            }
        }

        $offset += $frameLength
    }
}
finally {
    if ($null -ne $csvWriter) {
        $csvWriter.Dispose()
    }
}

$timestampSpanUs = if ($validFrames -gt 1) {
    Get-U32Delta -Current $lastTimestamp -Previous $firstTimestamp
}
else {
    0
}
$measuredRateHz = if ($timestampSpanUs -gt 0) {
    (($validFrames - 1) * 1000000.0) / $timestampSpanUs
}
else {
    0.0
}
$minimumExpectedFrames = [int]($DurationSeconds * 90)

[pscustomobject]@{
    Port = $Port
    BaudRate = $BaudRate
    CapturedBytes = $data.Length
    ValidFrames = $validFrames
    CrcErrors = $crcErrors
    SequenceGaps = $sequenceGaps
    SequenceGapEvents = $sequenceGapEvents
    SequenceDuplicates = $sequenceDuplicates
    SequenceOutOfOrder = $sequenceOutOfOrder
    SyncSkippedBytes = $syncSkippedBytes
    FirstSequence = $firstSequence
    LastSequence = $lastSequence
    TimestampSpanUs = $timestampSpanUs
    MeasuredRateHz = [Math]::Round($measuredRateHz, 3)
    MinimumPeriodUs = $minimumPeriodUs
    MaximumPeriodUs = $maximumPeriodUs
    MaximumExecutionUs = $maximumExecutionUs
    MaximumJitterUs = $maximumJitterUs
    DeadlineMissCount = $deadlineMissCount
    CsvPath = $CsvPath
} | Format-List

if (($crcErrors -ne 0) -or
    ($sequenceGaps -ne 0) -or
    ($sequenceDuplicates -ne 0) -or ($sequenceOutOfOrder -ne 0) -or
    ($deadlineMissCount -ne 0) -or
    ($validFrames -lt $minimumExpectedFrames)) {
    exit 2
}
