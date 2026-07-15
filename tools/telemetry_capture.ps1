[CmdletBinding(DefaultParameterSetName = "Serial")]
param(
    [Parameter(Mandatory = $true, ParameterSetName = "Serial")]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [Parameter(Mandatory = $true, ParameterSetName = "File")]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$InputPath,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(1, 900)]
    [int]$DurationSeconds = 10,
    [ValidateRange(0, 10)]
    [int]$FlushSeconds = 1,
    [string]$CsvPath = "",
    [string]$JsonPath = ""
)

$ErrorActionPreference = "Stop"

$Sync0 = 0xA5
$Sync1 = 0x5A
$ProtocolVersion = 1
$ControlFrameType = 1
$ParameterAckFrameType = 3
$HealthFrameType = 4
$ControlPayloadLength = 40
$ParameterAckPayloadLength = 16
$HealthPayloadLength = 112
$MinimumFrameLength = 16
$MaximumPayloadLength = 128
[uint64]$U32HalfRange = 2147483648

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

function Get-U32Delta {
    param([uint32]$Current, [uint32]$Previous)

    [int64]$modulus = 4294967296
    return [uint64]((([int64]$Current - [int64]$Previous + $modulus) % $modulus))
}

function Get-RateHz {
    param([int]$Count, $FirstTimestamp, $LastTimestamp)

    if (($Count -le 1) -or ($null -eq $FirstTimestamp) -or
        ($null -eq $LastTimestamp)) {
        return 0.0
    }
    $spanUs = Get-U32Delta -Current $LastTimestamp -Previous $FirstTimestamp
    if ($spanUs -eq 0) {
        return 0.0
    }
    return [Math]::Round((($Count - 1) * 1000000.0) / $spanUs, 3)
}

function Read-SerialCapture {
    param([string]$Name, [int]$Rate, [int]$Seconds, [int]$Flush)

    $serialPort = [System.IO.Ports.SerialPort]::new(
        $Name,
        $Rate,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serialPort.ReadBufferSize = 1MB
    $serialPort.ReadTimeout = 100
    $estimatedCapacity = [Math]::Max(65536, $Seconds * 8192)
    $capture = [System.IO.MemoryStream]::new($estimatedCapacity)
    $readBuffer = New-Object byte[] 4096

    try {
        $serialPort.Open()
        if ($Flush -gt 0) {
            $flushDeadline = [DateTime]::UtcNow.AddSeconds($Flush)
            while ([DateTime]::UtcNow -lt $flushDeadline) {
                $serialPort.DiscardInBuffer()
                Start-Sleep -Milliseconds 20
            }
        }

        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        while ($stopwatch.Elapsed.TotalSeconds -lt $Seconds) {
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
        return ,$capture.ToArray()
    }
    finally {
        if ($serialPort.IsOpen) {
            $serialPort.Close()
        }
        $serialPort.Dispose()
        $capture.Dispose()
    }
}

if ($PSCmdlet.ParameterSetName -eq "File") {
    $resolvedInputPath = [System.IO.Path]::GetFullPath($InputPath)
    $data = [System.IO.File]::ReadAllBytes($resolvedInputPath)
    $sourceName = $resolvedInputPath
}
else {
    $data = Read-SerialCapture -Name $Port -Rate $BaudRate `
        -Seconds $DurationSeconds -Flush $FlushSeconds
    $sourceName = $Port
}

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
$controlFrames = 0
$healthFrames = 0
$parameterAckFrames = 0
$unknownFrames = 0
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
$firstControlTimestamp = $null
$lastControlTimestamp = $null
$firstHealthTimestamp = $null
$lastHealthTimestamp = $null
$minimumPeriodUs = [uint32]::MaxValue
$maximumPeriodUs = 0
$maximumExecutionUs = 0
$maximumJitterUs = 0
$deadlineMissCount = 0
$latestHealth = $null

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
        $calculatedCrc = Get-Crc16Ccitt -Data $data `
            -Offset ($offset + 2) -Length (12 + $payloadLength)
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
        $payloadOffset = $offset + 14

        if (($frameType -eq $ControlFrameType) -and
            ($payloadLength -eq $ControlPayloadLength)) {
            $controlFrames++
            if ($null -eq $firstControlTimestamp) {
                $firstControlTimestamp = $timestampUs
            }
            $lastControlTimestamp = $timestampUs
            $setpoint = [BitConverter]::ToSingle($data, $payloadOffset)
            $measurement = [BitConverter]::ToSingle($data, $payloadOffset + 4)
            $controlOutput = [BitConverter]::ToSingle($data, $payloadOffset + 8)
            $auxiliary = [BitConverter]::ToSingle($data, $payloadOffset + 12)
            $loopCount = [BitConverter]::ToUInt32($data, $payloadOffset + 16)
            $periodUs = [BitConverter]::ToUInt32($data, $payloadOffset + 20)
            $executionUs = [BitConverter]::ToUInt32($data, $payloadOffset + 24)
            $jitterUs = [BitConverter]::ToUInt32($data, $payloadOffset + 28)
            $deadlineMissCount = [BitConverter]::ToUInt32($data, $payloadOffset + 32)
            $flags = [BitConverter]::ToUInt32($data, $payloadOffset + 36)

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
        elseif (($frameType -eq $HealthFrameType) -and
            ($payloadLength -eq $HealthPayloadLength)) {
            $healthFrames++
            if ($null -eq $firstHealthTimestamp) {
                $firstHealthTimestamp = $timestampUs
            }
            $lastHealthTimestamp = $timestampUs
            $latestHealth = [pscustomobject]@{
                SchemaVersion = [BitConverter]::ToUInt16($data, $payloadOffset)
                BuildPhase = ('0x{0:X4}' -f [BitConverter]::ToUInt16($data, $payloadOffset + 2))
                SnapshotSequence = [BitConverter]::ToUInt32($data, $payloadOffset + 4)
                UptimeTicks = [BitConverter]::ToUInt32($data, $payloadOffset + 8)
                ActiveIssueMask = ('0x{0:X8}' -f [BitConverter]::ToUInt32($data, $payloadOffset + 12))
                StickyIssueMask = ('0x{0:X8}' -f [BitConverter]::ToUInt32($data, $payloadOffset + 16))
                PeriodUs = [BitConverter]::ToUInt32($data, $payloadOffset + 20)
                ExecutionUs = [BitConverter]::ToUInt32($data, $payloadOffset + 24)
                DeadlineMissCount = [BitConverter]::ToUInt32($data, $payloadOffset + 28)
                PublishDropCount = [BitConverter]::ToUInt32($data, $payloadOffset + 32)
                TransportDropCount = [BitConverter]::ToUInt32($data, $payloadOffset + 36)
                SerialTxDropCount = [BitConverter]::ToUInt32($data, $payloadOffset + 40)
                SerialRxOverflowCount = [BitConverter]::ToUInt32($data, $payloadOffset + 44)
                I2cErrorCount = [BitConverter]::ToUInt32($data, $payloadOffset + 48)
                ParameterErrorCount = [BitConverter]::ToUInt32($data, $payloadOffset + 52)
                HeapMinEverFreeBytes = [BitConverter]::ToUInt32($data, $payloadOffset + 56)
                ParameterApplySequence = [BitConverter]::ToUInt32($data, $payloadOffset + 60)
                MinimumStackFreeWords = [BitConverter]::ToUInt16($data, $payloadOffset + 64)
                Level = $data[$payloadOffset + 66]
                ActiveIssue = $data[$payloadOffset + 67]
                FirstFaultIssue = $data[$payloadOffset + 68]
                FirstFaultValid = $data[$payloadOffset + 69]
                OledOnline = $data[$payloadOffset + 70]
                ActuatorOutputPermitted = $data[$payloadOffset + 71]
                ParameterPending = $data[$payloadOffset + 72]
                ParameterLastStatus = $data[$payloadOffset + 73]
                ResetReason = $data[$payloadOffset + 74]
                ResetReasonValid = $data[$payloadOffset + 75]
                I2cSuccessCount = [BitConverter]::ToUInt32($data, $payloadOffset + 76)
                QuietAcquiredCount = [BitConverter]::ToUInt32($data, $payloadOffset + 80)
                QuietReleasedCount = [BitConverter]::ToUInt32($data, $payloadOffset + 84)
                MaxQuietWindowUs = [BitConverter]::ToUInt32($data, $payloadOffset + 88)
                DisplayRefreshCount = [BitConverter]::ToUInt32($data, $payloadOffset + 92)
                SystemStackFreeWords = [BitConverter]::ToUInt16($data, $payloadOffset + 96)
                ServiceStackFreeWords = [BitConverter]::ToUInt16($data, $payloadOffset + 98)
                TelemetryStackFreeWords = [BitConverter]::ToUInt16($data, $payloadOffset + 100)
                DisplayStackFreeWords = [BitConverter]::ToUInt16($data, $payloadOffset + 102)
                IdleStackFreeWords = [BitConverter]::ToUInt16($data, $payloadOffset + 104)
                TimerStackFreeWords = [BitConverter]::ToUInt16($data, $payloadOffset + 106)
                SerialRingHighWaterBytes = [BitConverter]::ToUInt16($data, $payloadOffset + 108)
                QuietWindowActive = $data[$payloadOffset + 110]
            }
        }
        elseif (($frameType -eq $ParameterAckFrameType) -and
            ($payloadLength -eq $ParameterAckPayloadLength)) {
            $parameterAckFrames++
        }
        else {
            $unknownFrames++
        }

        $offset += $frameLength
    }
}
finally {
    if ($null -ne $csvWriter) {
        $csvWriter.Dispose()
    }
}

if ($minimumPeriodUs -eq [uint32]::MaxValue) {
    $minimumPeriodUs = 0
}
$summary = [pscustomobject]@{
    Source = $sourceName
    BaudRate = if ($PSCmdlet.ParameterSetName -eq "Serial") { $BaudRate } else { $null }
    CapturedBytes = $data.Length
    ValidFrames = $validFrames
    ControlFrames = $controlFrames
    ControlRateHz = Get-RateHz -Count $controlFrames `
        -FirstTimestamp $firstControlTimestamp -LastTimestamp $lastControlTimestamp
    HealthFrames = $healthFrames
    HealthRateHz = Get-RateHz -Count $healthFrames `
        -FirstTimestamp $firstHealthTimestamp -LastTimestamp $lastHealthTimestamp
    ParameterAckFrames = $parameterAckFrames
    UnknownFrames = $unknownFrames
    CrcErrors = $crcErrors
    SequenceGaps = $sequenceGaps
    SequenceGapEvents = $sequenceGapEvents
    SequenceDuplicates = $sequenceDuplicates
    SequenceOutOfOrder = $sequenceOutOfOrder
    SyncSkippedBytes = $syncSkippedBytes
    FirstSequence = $firstSequence
    LastSequence = $lastSequence
    FirstTimestampUs = $firstTimestamp
    LastTimestampUs = $lastTimestamp
    MinimumPeriodUs = $minimumPeriodUs
    MaximumPeriodUs = $maximumPeriodUs
    MaximumExecutionUs = $maximumExecutionUs
    MaximumJitterUs = $maximumJitterUs
    DeadlineMissCount = $deadlineMissCount
    LatestHealth = $latestHealth
    CsvPath = $CsvPath
    JsonPath = $JsonPath
}

if (-not [string]::IsNullOrWhiteSpace($JsonPath)) {
    $resolvedJsonPath = [System.IO.Path]::GetFullPath($JsonPath)
    $jsonDirectory = Split-Path -Parent $resolvedJsonPath
    if (-not [string]::IsNullOrWhiteSpace($jsonDirectory)) {
        New-Item -ItemType Directory -Path $jsonDirectory -Force | Out-Null
    }
    $summary | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath $resolvedJsonPath -Encoding UTF8
}

$summary | Format-List

$minimumControlFrames = [int]($DurationSeconds * 90)
$minimumHealthFrames = if ($DurationSeconds -ge 3) {
    [int][Math]::Floor($DurationSeconds * 0.7)
}
else {
    0
}
$rateGateFailed = ($PSCmdlet.ParameterSetName -eq "Serial") -and
    (($controlFrames -lt $minimumControlFrames) -or
     ($healthFrames -lt $minimumHealthFrames))
if (($crcErrors -ne 0) -or
    ($sequenceGaps -ne 0) -or
    ($sequenceDuplicates -ne 0) -or
    ($sequenceOutOfOrder -ne 0) -or
    ($deadlineMissCount -ne 0) -or $rateGateFailed) {
    exit 2
}
