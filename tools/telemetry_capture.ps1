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
    [string]$ImuCsvPath = "",
    [string]$AttitudeCsvPath = "",
    [string]$ReflectanceCsvPath = "",
    [string]$BallBalanceCsvPath = "",
    [string]$JsonPath = ""
)

$ErrorActionPreference = "Stop"

$Sync0 = 0xA5
$Sync1 = 0x5A
$ProtocolVersion = 1
$ControlFrameType = 1
$ParameterAckFrameType = 3
$HealthFrameType = 4
$ActuatorAckFrameType = 6
$MotorProfileFrameType = 7
$ReflectanceFrameType = 8
$SupplyVoltageFrameType = 9
$TfminiFrameType = 10
$ImuFrameType = 11
$EspLinkFrameType = 12
$AttitudeFrameType = 13
$BallBalanceFrameType = 16
$LegacyControlPayloadLength = 40
$DualOutputControlPayloadLength = 44
$ControlPayloadLength = 96
$ParameterAckPayloadLength = 16
$LegacyHealthPayloadLength = 112
$HealthPayloadLength = 116
$ActuatorAckPayloadLength = 16
$MotorProfilePayloadLength = 36
$ReflectancePayloadLength = 36
$SupplyVoltagePayloadLength = 24
$TfminiPayloadLength = 64
$LegacyImuPayloadLength = 64
$ImuPayloadLength = 88
$LegacyEspLinkPayloadLength = 64
$EspLinkPayloadLength = 96
$AttitudePayloadLength = 64
$LegacyBallBalancePayloadLength = 64
$BallBalancePayloadLength = 72
$MinimumFrameLength = 16
$MaximumPayloadLength = 160
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

function Get-I8 {
    param([byte]$Value)

    if ($Value -ge 128) {
        return [int]$Value - 256
    }
    return [int]$Value
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
        "auxiliary,right_auxiliary,right_setpoint," +
        "left_pid_proportional,left_pid_integrator,left_pid_derivative," +
        "left_pid_feedforward,right_pid_proportional,right_pid_integrator," +
        "right_pid_derivative,right_pid_feedforward,active_kp,active_ki," +
        "active_kd,parameter_apply_sequence," +
        "loop_count,period_us,execution_us,jitter_us," +
        "deadline_miss_count,flags"
    )
}

function Format-OptionalFloat {
    param($Value, [System.Globalization.CultureInfo]$Culture)

    if ($null -eq $Value) {
        return ""
    }
    return $Value.ToString("R", $Culture)
}

$imuCsvWriter = $null
if (-not [string]::IsNullOrWhiteSpace($ImuCsvPath)) {
    $resolvedImuCsvPath = [System.IO.Path]::GetFullPath($ImuCsvPath)
    $imuCsvDirectory = Split-Path -Parent $resolvedImuCsvPath
    if (-not [string]::IsNullOrWhiteSpace($imuCsvDirectory)) {
        New-Item -ItemType Directory -Path $imuCsvDirectory -Force | Out-Null
    }
    $imuCsvWriter = [System.IO.StreamWriter]::new(
        $resolvedImuCsvPath,
        $false,
        [System.Text.UTF8Encoding]::new($false)
    )
    $imuCsvWriter.WriteLine(
        "sequence,timestamp_us,sample_sequence,measurement_timestamp_us," +
        "age_us,update_sequence,accel_x_g,accel_y_g,accel_z_g,accel_norm_g," +
        "gyro_filtered_x_dps,gyro_filtered_y_dps,gyro_filtered_z_dps," +
        "gyro_raw_x_dps,gyro_raw_y_dps,gyro_raw_z_dps," +
        "gyro_unfiltered_x_dps,gyro_unfiltered_y_dps,gyro_unfiltered_z_dps," +
        "gyro_bias_x_dps,gyro_bias_y_dps,gyro_bias_z_dps,temperature_c," +
        "calibration_samples,calibration_target_samples,state,address," +
        "who_am_i,flags,sample_success_count,sample_failure_count"
    )
}

$attitudeCsvWriter = $null
if (-not [string]::IsNullOrWhiteSpace($AttitudeCsvPath)) {
    $resolvedAttitudeCsvPath = [System.IO.Path]::GetFullPath($AttitudeCsvPath)
    $attitudeCsvDirectory = Split-Path -Parent $resolvedAttitudeCsvPath
    if (-not [string]::IsNullOrWhiteSpace($attitudeCsvDirectory)) {
        New-Item -ItemType Directory -Path $attitudeCsvDirectory -Force |
            Out-Null
    }
    $attitudeCsvWriter = [System.IO.StreamWriter]::new(
        $resolvedAttitudeCsvPath,
        $false,
        [System.Text.UTF8Encoding]::new($false)
    )
    $attitudeCsvWriter.WriteLine(
        "sequence,timestamp_us,imu_sample_count,measurement_timestamp_us," +
        "age_us,update_sequence,roll_deg,pitch_deg,yaw_deg," +
        "roll_rate_dps,pitch_rate_dps,yaw_rate_dps,accel_norm_g," +
        "accel_weight,dt_s,processed_count,rejected_count," +
        "timing_reset_count,flags"
    )
}

$reflectanceCsvWriter = $null
if (-not [string]::IsNullOrWhiteSpace($ReflectanceCsvPath)) {
    $resolvedReflectanceCsvPath =
        [System.IO.Path]::GetFullPath($ReflectanceCsvPath)
    $reflectanceCsvDirectory = Split-Path -Parent $resolvedReflectanceCsvPath
    if (-not [string]::IsNullOrWhiteSpace($reflectanceCsvDirectory)) {
        New-Item -ItemType Directory -Path $reflectanceCsvDirectory -Force |
            Out-Null
    }
    $reflectanceCsvWriter = [System.IO.StreamWriter]::new(
        $resolvedReflectanceCsvPath,
        $false,
        [System.Text.UTF8Encoding]::new($false)
    )
    $reflectanceCsvWriter.WriteLine(
        "sequence,timestamp_us,scan_sequence," +
        "raw0,raw1,raw2,raw3,raw4,raw5,raw6,raw7," +
        "minimum_raw,maximum_raw,conversion_timeout_count," +
        "incomplete_scan_count,sample_count"
    )
}

$ballBalanceCsvWriter = $null
if (-not [string]::IsNullOrWhiteSpace($BallBalanceCsvPath)) {
    $resolvedBallBalanceCsvPath =
        [System.IO.Path]::GetFullPath($BallBalanceCsvPath)
    $ballBalanceCsvDirectory = Split-Path -Parent $resolvedBallBalanceCsvPath
    if (-not [string]::IsNullOrWhiteSpace($ballBalanceCsvDirectory)) {
        New-Item -ItemType Directory -Path $ballBalanceCsvDirectory -Force |
            Out-Null
    }
    $ballBalanceCsvWriter = [System.IO.StreamWriter]::new(
        $resolvedBallBalanceCsvPath,
        $false,
        [System.Text.UTF8Encoding]::new($false)
    )
    $ballBalanceCsvWriter.WriteLine(
        "sequence,timestamp_us,update_sequence,elapsed_ms," +
        "accepted_command_count,rejected_command_count," +
        "vision_invalid_count,fault_count,control_output_deg," +
        "motor_center_deg,motor_target_deg,motor_actual_deg," +
        "motor_error_deg,target_position_mm,measured_position_mm," +
        "velocity_mm_s,position_error_mm,vision_sequence," +
        "vision_valid_age_ms,settle_ms,vision_flags,state," +
        "mission_status,fault,vision_valid,saturated,motor_online," +
        "motor_enabled,feedforward_deg,planned_accel_mm_s2," +
        "imu_accel_mm_s2,encoder_accel_mm_s2"
    )
}

$culture = [System.Globalization.CultureInfo]::InvariantCulture
$offset = 0
$validFrames = 0
$controlFrames = 0
$healthFrames = 0
$parameterAckFrames = 0
$actuatorAckFrames = 0
$motorProfileFrames = 0
$reflectanceFrames = 0
$supplyVoltageFrames = 0
$tfminiFrames = 0
$imuFrames = 0
$espLinkFrames = 0
$attitudeFrames = 0
$ballBalanceFrames = 0
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
$firstMotorProfileTimestamp = $null
$lastMotorProfileTimestamp = $null
$firstReflectanceTimestamp = $null
$lastReflectanceTimestamp = $null
$firstReflectanceScanSequence = $null
$lastReflectanceScanSequence = $null
$firstSupplyVoltageTimestamp = $null
$lastSupplyVoltageTimestamp = $null
$firstSupplySampleSequence = $null
$lastSupplySampleSequence = $null
$firstTfminiTimestamp = $null
$lastTfminiTimestamp = $null
$firstTfminiSampleSequence = $null
$lastTfminiSampleSequence = $null
$firstImuTimestamp = $null
$lastImuTimestamp = $null
$firstImuSampleSequence = $null
$lastImuSampleSequence = $null
$firstEspLinkTimestamp = $null
$lastEspLinkTimestamp = $null
$firstAttitudeTimestamp = $null
$lastAttitudeTimestamp = $null
$firstAttitudeSampleSequence = $null
$lastAttitudeSampleSequence = $null
$firstBallBalanceTimestamp = $null
$lastBallBalanceTimestamp = $null
$supplyRawMinimum = [uint16]::MaxValue
$supplyRawMaximum = 0
[uint64]$supplyRawSum = 0
$supplyBatteryMinimumMv = [uint32]::MaxValue
$supplyBatteryMaximumMv = 0
[uint64]$supplyBatterySumMv = 0
$tfminiValidDistanceCount = 0
$tfminiDistanceMinimumCm = [uint16]::MaxValue
$tfminiDistanceMaximumCm = 0
[uint64]$tfminiDistanceSumCm = 0
$minimumPeriodUs = [uint32]::MaxValue
$maximumPeriodUs = 0
$maximumExecutionUs = 0
$maximumJitterUs = 0
$deadlineMissCount = 0
$latestHealth = $null
$latestActuatorAck = $null
$latestMotorProfile = $null
$latestReflectance = $null
$latestSupplyVoltage = $null
$latestTfmini = $null
$latestImu = $null
$latestEspLink = $null
$latestAttitude = $null
$latestBallBalance = $null

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
            (($payloadLength -eq $ControlPayloadLength) -or
             ($payloadLength -eq $DualOutputControlPayloadLength) -or
             ($payloadLength -eq $LegacyControlPayloadLength))) {
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
            $rightAuxiliary = if ($payloadLength -ge
                $DualOutputControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 40)
            } else { 0.0 }
            $rightSetpoint = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 44)
            } else { $setpoint }
            $leftPidProportional = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 48)
            } else { 0.0 }
            $leftPidIntegrator = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 52)
            } else { 0.0 }
            $leftPidDerivative = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 56)
            } else { 0.0 }
            $leftPidFeedforward = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 60)
            } else { 0.0 }
            $rightPidProportional = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 64)
            } else { 0.0 }
            $rightPidIntegrator = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 68)
            } else { 0.0 }
            $rightPidDerivative = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 72)
            } else { 0.0 }
            $rightPidFeedforward = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 76)
            } else { 0.0 }
            $activeKp = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 80)
            } else { 0.0 }
            $activeKi = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 84)
            } else { 0.0 }
            $activeKd = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToSingle($data, $payloadOffset + 88)
            } else { 0.0 }
            $parameterApplySequence = if ($payloadLength -eq $ControlPayloadLength) {
                [BitConverter]::ToUInt32($data, $payloadOffset + 92)
            } else { 0 }

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
                    $rightAuxiliary.ToString("R", $culture),
                    $rightSetpoint.ToString("R", $culture),
                    $leftPidProportional.ToString("R", $culture),
                    $leftPidIntegrator.ToString("R", $culture),
                    $leftPidDerivative.ToString("R", $culture),
                    $leftPidFeedforward.ToString("R", $culture),
                    $rightPidProportional.ToString("R", $culture),
                    $rightPidIntegrator.ToString("R", $culture),
                    $rightPidDerivative.ToString("R", $culture),
                    $rightPidFeedforward.ToString("R", $culture),
                    $activeKp.ToString("R", $culture),
                    $activeKi.ToString("R", $culture),
                    $activeKd.ToString("R", $culture),
                    $parameterApplySequence,
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
            (($payloadLength -eq $HealthPayloadLength) -or
             ($payloadLength -eq $LegacyHealthPayloadLength))) {
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
                EncoderIsrLateCount = if ($payloadLength -ge $HealthPayloadLength) {
                    [BitConverter]::ToUInt32($data, $payloadOffset + 112)
                } else { 0 }
            }
        }
        elseif (($frameType -eq $ParameterAckFrameType) -and
            ($payloadLength -eq $ParameterAckPayloadLength)) {
            $parameterAckFrames++
        }
        elseif (($frameType -eq $ActuatorAckFrameType) -and
            ($payloadLength -eq $ActuatorAckPayloadLength)) {
            $actuatorAckFrames++
            $latestActuatorAck = [pscustomobject]@{
                Sequence = [BitConverter]::ToUInt32(
                    $data, $payloadOffset)
                LeftElectricalPermille = [BitConverter]::ToInt16(
                    $data, $payloadOffset + 4)
                RightElectricalPermille = [BitConverter]::ToInt16(
                    $data, $payloadOffset + 6)
                DurationMs = [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 8)
                Status = $data[$payloadOffset + 10]
                Reserved = $data[$payloadOffset + 11]
                AcceptedRequestCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 12)
            }
        }
        elseif (($frameType -eq $MotorProfileFrameType) -and
            ($payloadLength -eq $MotorProfilePayloadLength)) {
            $motorProfileFrames++
            if ($null -eq $firstMotorProfileTimestamp) {
                $firstMotorProfileTimestamp = $timestampUs
            }
            $lastMotorProfileTimestamp = $timestampUs
            $profileId = [BitConverter]::ToUInt16(
                $data, $payloadOffset + 2)
            $profileModel = switch ($profileId) {
                1 { "MG370" }
                2 { "513X" }
                3 { "513A" }
                4 { "513B" }
                5 { "513X-4S" }
                default { "UNKNOWN" }
            }
            $latestMotorProfile = [pscustomobject]@{
                SchemaVersion = [BitConverter]::ToUInt16(
                    $data, $payloadOffset)
                ProfileId = $profileId
                Model = $profileModel
                ProfileVersion = [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 4)
                StatusFlags = ('0x{0:X4}' -f [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 6))
                ValidFields = ('0x{0:X8}' -f [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 8))
                RatedVoltageMv = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 12)
                EncoderPpr = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 16)
                LeftCountsPerRevolution = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 20)
                RightCountsPerRevolution = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 24)
                LeftMotorOutputSign = Get-I8 $data[$payloadOffset + 28]
                RightMotorOutputSign = Get-I8 $data[$payloadOffset + 29]
                LeftEncoderCountSign = Get-I8 $data[$payloadOffset + 30]
                RightEncoderCountSign = Get-I8 $data[$payloadOffset + 31]
                LeftDecodeMultiplier = $data[$payloadOffset + 32]
                RightDecodeMultiplier = $data[$payloadOffset + 33]
                ActuatorTestReady = $data[$payloadOffset + 34]
                OutputLocked = $data[$payloadOffset + 35]
            }
        }
        elseif (($frameType -eq $ReflectanceFrameType) -and
            ($payloadLength -eq $ReflectancePayloadLength)) {
            $reflectanceFrames++
            $reflectanceScanSequence = [BitConverter]::ToUInt32(
                $data, $payloadOffset)
            if ($null -eq $firstReflectanceTimestamp) {
                $firstReflectanceTimestamp = $timestampUs
                $firstReflectanceScanSequence = $reflectanceScanSequence
            }
            $lastReflectanceTimestamp = $timestampUs
            $lastReflectanceScanSequence = $reflectanceScanSequence
            $rawValues = for ($channel = 0; $channel -lt 8; $channel++) {
                [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 4 + ($channel * 2))
            }
            $latestReflectance = [pscustomobject]@{
                ScanSequence = $reflectanceScanSequence
                Raw = @($rawValues)
                MinimumRaw = [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 20)
                MaximumRaw = [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 22)
                ConversionTimeoutCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 24)
                IncompleteScanCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 28)
                SampleCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 32)
            }
            if ($null -ne $reflectanceCsvWriter) {
                $values = @(
                    $sequence, $timestampUs, $reflectanceScanSequence
                ) + @($rawValues) + @(
                    $latestReflectance.MinimumRaw,
                    $latestReflectance.MaximumRaw,
                    $latestReflectance.ConversionTimeoutCount,
                    $latestReflectance.IncompleteScanCount,
                    $latestReflectance.SampleCount
                )
                $reflectanceCsvWriter.WriteLine($values -join ",")
            }
        }
        elseif (($frameType -eq $SupplyVoltageFrameType) -and
            ($payloadLength -eq $SupplyVoltagePayloadLength)) {
            $supplyVoltageFrames++
            $supplySampleSequence = [BitConverter]::ToUInt32(
                $data, $payloadOffset)
            $supplyRaw = [BitConverter]::ToUInt16(
                $data, $payloadOffset + 4)
            $supplyBatteryMv = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 12)
            if ($null -eq $firstSupplyVoltageTimestamp) {
                $firstSupplyVoltageTimestamp = $timestampUs
                $firstSupplySampleSequence = $supplySampleSequence
            }
            $lastSupplyVoltageTimestamp = $timestampUs
            $lastSupplySampleSequence = $supplySampleSequence
            if ($supplyRaw -lt $supplyRawMinimum) {
                $supplyRawMinimum = $supplyRaw
            }
            if ($supplyRaw -gt $supplyRawMaximum) {
                $supplyRawMaximum = $supplyRaw
            }
            if ($supplyBatteryMv -lt $supplyBatteryMinimumMv) {
                $supplyBatteryMinimumMv = $supplyBatteryMv
            }
            if ($supplyBatteryMv -gt $supplyBatteryMaximumMv) {
                $supplyBatteryMaximumMv = $supplyBatteryMv
            }
            $supplyRawSum += $supplyRaw
            $supplyBatterySumMv += $supplyBatteryMv
            $latestSupplyVoltage = [pscustomobject]@{
                SampleSequence = $supplySampleSequence
                Raw = $supplyRaw
                FilteredRaw = [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 6)
                AdcInputMv = [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 8)
                BatteryMv = $supplyBatteryMv
                SampleCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 16)
                ConversionTimeoutCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 20)
            }
        }
        elseif (($frameType -eq $TfminiFrameType) -and
            ($payloadLength -eq $TfminiPayloadLength)) {
            $tfminiFrames++
            $tfminiSampleSequence = [BitConverter]::ToUInt32(
                $data, $payloadOffset)
            $tfminiDistanceCm = [BitConverter]::ToUInt16(
                $data, $payloadOffset + 12)
            $tfminiFlags = $data[$payloadOffset + 19]
            if ($null -eq $firstTfminiTimestamp) {
                $firstTfminiTimestamp = $timestampUs
                $firstTfminiSampleSequence = $tfminiSampleSequence
            }
            $lastTfminiTimestamp = $timestampUs
            $lastTfminiSampleSequence = $tfminiSampleSequence
            if (($tfminiFlags -band 0x02) -ne 0) {
                $tfminiValidDistanceCount++
                if ($tfminiDistanceCm -lt $tfminiDistanceMinimumCm) {
                    $tfminiDistanceMinimumCm = $tfminiDistanceCm
                }
                if ($tfminiDistanceCm -gt $tfminiDistanceMaximumCm) {
                    $tfminiDistanceMaximumCm = $tfminiDistanceCm
                }
                $tfminiDistanceSumCm += $tfminiDistanceCm
            }
            $firmwareRaw = @(
                $data[$payloadOffset + 60],
                $data[$payloadOffset + 61],
                $data[$payloadOffset + 62]
            )
            $latestTfmini = [pscustomobject]@{
                SampleSequence = $tfminiSampleSequence
                MeasurementTimestampUs = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 4)
                AgeUs = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 8)
                DistanceCm = $tfminiDistanceCm
                Strength = [BitConverter]::ToUInt16(
                    $data, $payloadOffset + 14)
                TemperatureC = [Math]::Round(
                    [BitConverter]::ToInt16($data, $payloadOffset + 16) /
                        100.0, 2)
                Status = $data[$payloadOffset + 18]
                Online = (($tfminiFlags -band 0x01) -ne 0)
                MeasurementValid = (($tfminiFlags -band 0x02) -ne 0)
                FirmwareVersionValid = (($tfminiFlags -band 0x04) -ne 0)
                FramePeriodUs = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 20)
                DeviceFrameRateHz = [Math]::Round(
                    [BitConverter]::ToUInt32(
                        $data, $payloadOffset + 24) / 1000.0, 3)
                DataFrameCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 28)
                ValidMeasurementCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 32)
                InvalidMeasurementCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 36)
                ChecksumErrorCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 40)
                CommandFrameCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 44)
                CommandChecksumErrorCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 48)
                TimeoutCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 52)
                UartRxOverflowCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 56)
                FirmwareVersionRaw = $firmwareRaw
                FirmwareVersion = if (($tfminiFlags -band 0x04) -ne 0) {
                    "{0}.{1}.{2}" -f $firmwareRaw[2],
                        $firmwareRaw[1], $firmwareRaw[0]
                } else { "unknown" }
                QueryAttemptCount = $data[$payloadOffset + 63]
            }
        }
        elseif (($frameType -eq $ImuFrameType) -and
            (($payloadLength -eq $ImuPayloadLength) -or
             ($payloadLength -eq $LegacyImuPayloadLength))) {
            $imuFrames++
            $imuSampleSequence = [BitConverter]::ToUInt32(
                $data, $payloadOffset)
            $imuMeasurementTimestampUs = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 4)
            $imuAgeUs = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 8)
            $imuUpdateSequence = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 12)
            $accelX = [BitConverter]::ToSingle($data, $payloadOffset + 16)
            $accelY = [BitConverter]::ToSingle($data, $payloadOffset + 20)
            $accelZ = [BitConverter]::ToSingle($data, $payloadOffset + 24)
            $accelNorm = [BitConverter]::ToSingle($data, $payloadOffset + 28)
            $gyroFilteredX = [BitConverter]::ToSingle(
                $data, $payloadOffset + 32)
            $gyroFilteredY = [BitConverter]::ToSingle(
                $data, $payloadOffset + 36)
            $gyroFilteredZ = [BitConverter]::ToSingle(
                $data, $payloadOffset + 40)
            $temperatureC = [BitConverter]::ToSingle(
                $data, $payloadOffset + 44)
            $calibrationSamples = [BitConverter]::ToUInt16(
                $data, $payloadOffset + 48)
            $calibrationTargetSamples = [BitConverter]::ToUInt16(
                $data, $payloadOffset + 50)
            $imuFlags = $data[$payloadOffset + 55]
            $sampleSuccessCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 56)
            $sampleFailureCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 60)
            $gyroRawX = $null
            $gyroRawY = $null
            $gyroRawZ = $null
            $gyroBiasX = $null
            $gyroBiasY = $null
            $gyroBiasZ = $null
            $gyroUnfilteredX = $null
            $gyroUnfilteredY = $null
            $gyroUnfilteredZ = $null
            if ($payloadLength -eq $ImuPayloadLength) {
                $gyroRawX = [BitConverter]::ToSingle(
                    $data, $payloadOffset + 64)
                $gyroRawY = [BitConverter]::ToSingle(
                    $data, $payloadOffset + 68)
                $gyroRawZ = [BitConverter]::ToSingle(
                    $data, $payloadOffset + 72)
                $gyroBiasX = [BitConverter]::ToSingle(
                    $data, $payloadOffset + 76)
                $gyroBiasY = [BitConverter]::ToSingle(
                    $data, $payloadOffset + 80)
                $gyroBiasZ = [BitConverter]::ToSingle(
                    $data, $payloadOffset + 84)
                $gyroUnfilteredX = $gyroRawX - $gyroBiasX
                $gyroUnfilteredY = $gyroRawY - $gyroBiasY
                $gyroUnfilteredZ = $gyroRawZ - $gyroBiasZ
            }
            if ($null -eq $firstImuTimestamp) {
                $firstImuTimestamp = $timestampUs
                $firstImuSampleSequence = $imuSampleSequence
            }
            $lastImuTimestamp = $timestampUs
            $lastImuSampleSequence = $imuSampleSequence
            $imuState = $data[$payloadOffset + 52]
            $latestImu = [pscustomobject]@{
                SampleSequence = $imuSampleSequence
                MeasurementTimestampUs = $imuMeasurementTimestampUs
                AgeUs = $imuAgeUs
                UpdateSequence = $imuUpdateSequence
                AccelG = @(
                    [Math]::Round($accelX, 5),
                    [Math]::Round($accelY, 5),
                    [Math]::Round($accelZ, 5)
                )
                AccelNormG = [Math]::Round($accelNorm, 5)
                GyroDps = @(
                    [Math]::Round($gyroFilteredX, 5),
                    [Math]::Round($gyroFilteredY, 5),
                    [Math]::Round($gyroFilteredZ, 5)
                )
                GyroRawDps = if ($null -ne $gyroRawX) { @(
                    [Math]::Round($gyroRawX, 5),
                    [Math]::Round($gyroRawY, 5),
                    [Math]::Round($gyroRawZ, 5)
                ) } else { $null }
                GyroUnfilteredDps = if ($null -ne $gyroUnfilteredX) { @(
                    [Math]::Round($gyroUnfilteredX, 5),
                    [Math]::Round($gyroUnfilteredY, 5),
                    [Math]::Round($gyroUnfilteredZ, 5)
                ) } else { $null }
                GyroBiasDps = if ($null -ne $gyroBiasX) { @(
                    [Math]::Round($gyroBiasX, 5),
                    [Math]::Round($gyroBiasY, 5),
                    [Math]::Round($gyroBiasZ, 5)
                ) } else { $null }
                TemperatureC = [Math]::Round($temperatureC, 2)
                CalibrationSamples = $calibrationSamples
                CalibrationTargetSamples = $calibrationTargetSamples
                State = $imuState
                StateName = switch ($imuState) {
                    0 { "PROBE" }
                    1 { "RESET" }
                    2 { "SETTLE" }
                    3 { "CAL" }
                    4 { "READY" }
                    default { "UNKNOWN" }
                }
                Address = ('0x{0:X2}' -f $data[$payloadOffset + 53])
                WhoAmI = ('0x{0:X2}' -f $data[$payloadOffset + 54])
                Online = (($imuFlags -band 0x01) -ne 0)
                Valid = (($imuFlags -band 0x02) -ne 0)
                Calibrated = (($imuFlags -band 0x04) -ne 0)
                Ready = (($imuFlags -band 0x08) -ne 0)
                SampleSuccessCount = $sampleSuccessCount
                SampleFailureCount = $sampleFailureCount
            }
            if ($null -ne $imuCsvWriter) {
                $optionalValues = @(
                    (Format-OptionalFloat $gyroRawX $culture),
                    (Format-OptionalFloat $gyroRawY $culture),
                    (Format-OptionalFloat $gyroRawZ $culture),
                    (Format-OptionalFloat $gyroUnfilteredX $culture),
                    (Format-OptionalFloat $gyroUnfilteredY $culture),
                    (Format-OptionalFloat $gyroUnfilteredZ $culture),
                    (Format-OptionalFloat $gyroBiasX $culture),
                    (Format-OptionalFloat $gyroBiasY $culture),
                    (Format-OptionalFloat $gyroBiasZ $culture)
                )
                $values = @(
                    $sequence, $timestampUs, $imuSampleSequence,
                    $imuMeasurementTimestampUs, $imuAgeUs, $imuUpdateSequence,
                    $accelX.ToString("R", $culture),
                    $accelY.ToString("R", $culture),
                    $accelZ.ToString("R", $culture),
                    $accelNorm.ToString("R", $culture),
                    $gyroFilteredX.ToString("R", $culture),
                    $gyroFilteredY.ToString("R", $culture),
                    $gyroFilteredZ.ToString("R", $culture)
                ) + $optionalValues + @(
                    $temperatureC.ToString("R", $culture),
                    $calibrationSamples, $calibrationTargetSamples, $imuState,
                    $data[$payloadOffset + 53], $data[$payloadOffset + 54],
                    $imuFlags, $sampleSuccessCount, $sampleFailureCount
                )
                $imuCsvWriter.WriteLine($values -join ",")
            }
        }
        elseif (($frameType -eq $EspLinkFrameType) -and
            (($payloadLength -eq $EspLinkPayloadLength) -or
             ($payloadLength -eq $LegacyEspLinkPayloadLength))) {
            $espLinkFrames++
            if ($null -eq $firstEspLinkTimestamp) {
                $firstEspLinkTimestamp = $timestampUs
            }
            $lastEspLinkTimestamp = $timestampUs
            $latestEspLink = [pscustomobject]@{
                SchemaVersion = [BitConverter]::ToUInt16(
                    $data, $payloadOffset)
                LinkOnline = ($data[$payloadOffset + 2] -ne 0)
                Outstanding = ($data[$payloadOffset + 3] -ne 0)
                TxFrames = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 4)
                AckFrames = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 8)
                TimeoutCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 12)
                CrcErrorCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 16)
                FormatErrorCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 20)
                UnexpectedSequenceCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 24)
                RxByteCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 28)
                TxByteCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 32)
                RxOverflowCount = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 36)
                MinimumRttUs = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 40)
                AverageRttUs = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 44)
                MaximumRttUs = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 48)
                LastRttUs = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 52)
                LastSequence = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 56)
                BaudRate = [BitConverter]::ToUInt32(
                    $data, $payloadOffset + 60)
                RxDmaDoneCount = if ($payloadLength -ge $EspLinkPayloadLength) {
                    [BitConverter]::ToUInt32($data, $payloadOffset + 64)
                } else { 0 }
                RxDmaRestartCount = if ($payloadLength -ge $EspLinkPayloadLength) {
                    [BitConverter]::ToUInt32($data, $payloadOffset + 68)
                } else { 0 }
                TxDmaDoneCount = if ($payloadLength -ge $EspLinkPayloadLength) {
                    [BitConverter]::ToUInt32($data, $payloadOffset + 72)
                } else { 0 }
                TxEotCount = if ($payloadLength -ge $EspLinkPayloadLength) {
                    [BitConverter]::ToUInt32($data, $payloadOffset + 76)
                } else { 0 }
                IrqEntryCount = if ($payloadLength -ge $EspLinkPayloadLength) {
                    [BitConverter]::ToUInt32($data, $payloadOffset + 80)
                } else { 0 }
                UnexpectedIrqCount = if ($payloadLength -ge $EspLinkPayloadLength) {
                    [BitConverter]::ToUInt32($data, $payloadOffset + 84)
                } else { 0 }
                RxHighWaterBytes = if ($payloadLength -ge $EspLinkPayloadLength) {
                    [BitConverter]::ToUInt16($data, $payloadOffset + 88)
                } else { 0 }
                TxActiveLength = if ($payloadLength -ge $EspLinkPayloadLength) {
                    [BitConverter]::ToUInt16($data, $payloadOffset + 90)
                } else { 0 }
                TxBusy = (($payloadLength -ge $EspLinkPayloadLength) -and
                    ($data[$payloadOffset + 92] -ne 0))
                Initialized = (($payloadLength -ge $EspLinkPayloadLength) -and
                    ($data[$payloadOffset + 93] -ne 0))
                RxDmaActive = (($payloadLength -ge $EspLinkPayloadLength) -and
                    ($data[$payloadOffset + 94] -ne 0))
                TxLineIdle = (($payloadLength -ge $EspLinkPayloadLength) -and
                    ($data[$payloadOffset + 95] -ne 0))
            }
        }
        elseif (($frameType -eq $AttitudeFrameType) -and
            ($payloadLength -eq $AttitudePayloadLength)) {
            $attitudeFrames++
            $attitudeImuSampleCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 4)
            $attitudeMeasurementTimestampUs = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 8)
            $attitudeUpdateSequence = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 12)
            $rollDeg = [BitConverter]::ToSingle($data, $payloadOffset + 16)
            $pitchDeg = [BitConverter]::ToSingle($data, $payloadOffset + 20)
            $yawDeg = [BitConverter]::ToSingle($data, $payloadOffset + 24)
            $rollRateDps = [BitConverter]::ToSingle(
                $data, $payloadOffset + 28)
            $pitchRateDps = [BitConverter]::ToSingle(
                $data, $payloadOffset + 32)
            $yawRateDps = [BitConverter]::ToSingle(
                $data, $payloadOffset + 36)
            $attitudeAccelNorm = [BitConverter]::ToSingle(
                $data, $payloadOffset + 40)
            $attitudeAccelWeight = [BitConverter]::ToSingle(
                $data, $payloadOffset + 44)
            $attitudeDt = [BitConverter]::ToSingle(
                $data, $payloadOffset + 48)
            $attitudeProcessedCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 52)
            $attitudeRejectedCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 56)
            $attitudeTimingResetCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 60)
            $attitudeFlags = $data[$payloadOffset + 2]
            $attitudeAgeUs = Get-U32Delta -Current $timestampUs `
                -Previous $attitudeMeasurementTimestampUs
            if ($null -eq $firstAttitudeTimestamp) {
                $firstAttitudeTimestamp = $timestampUs
                $firstAttitudeSampleSequence = $attitudeImuSampleCount
            }
            $lastAttitudeTimestamp = $timestampUs
            $lastAttitudeSampleSequence = $attitudeImuSampleCount
            $latestAttitude = [pscustomobject]@{
                SchemaVersion = [BitConverter]::ToUInt16(
                    $data, $payloadOffset)
                ImuSampleCount = $attitudeImuSampleCount
                MeasurementTimestampUs = $attitudeMeasurementTimestampUs
                AgeUs = $attitudeAgeUs
                UpdateSequence = $attitudeUpdateSequence
                RollDeg = [Math]::Round($rollDeg, 5)
                PitchDeg = [Math]::Round($pitchDeg, 5)
                YawDeg = [Math]::Round($yawDeg, 5)
                AxisRateDps = @(
                    [Math]::Round($rollRateDps, 5),
                    [Math]::Round($pitchRateDps, 5),
                    [Math]::Round($yawRateDps, 5)
                )
                AccelNormG = [Math]::Round($attitudeAccelNorm, 5)
                AccelWeight = [Math]::Round($attitudeAccelWeight, 5)
                DtS = [Math]::Round($attitudeDt, 6)
                ProcessedCount = $attitudeProcessedCount
                RejectedCount = $attitudeRejectedCount
                TimingResetCount = $attitudeTimingResetCount
                Flags = $attitudeFlags
                Initialized = (($attitudeFlags -band 0x01) -ne 0)
                SourceValid = (($attitudeFlags -band 0x02) -ne 0)
                AccelUsed = (($attitudeFlags -band 0x04) -ne 0)
                TimingReset = (($attitudeFlags -band 0x08) -ne 0)
            }
            if ($null -ne $attitudeCsvWriter) {
                $values = @(
                    $sequence, $timestampUs, $attitudeImuSampleCount,
                    $attitudeMeasurementTimestampUs, $attitudeAgeUs,
                    $attitudeUpdateSequence,
                    $rollDeg.ToString("R", $culture),
                    $pitchDeg.ToString("R", $culture),
                    $yawDeg.ToString("R", $culture),
                    $rollRateDps.ToString("R", $culture),
                    $pitchRateDps.ToString("R", $culture),
                    $yawRateDps.ToString("R", $culture),
                    $attitudeAccelNorm.ToString("R", $culture),
                    $attitudeAccelWeight.ToString("R", $culture),
                    $attitudeDt.ToString("R", $culture),
                    $attitudeProcessedCount, $attitudeRejectedCount,
                    $attitudeTimingResetCount, $attitudeFlags
                )
                $attitudeCsvWriter.WriteLine($values -join ",")
            }
        }
        elseif (($frameType -eq $BallBalanceFrameType) -and
            (($payloadLength -eq $LegacyBallBalancePayloadLength) -or
             ($payloadLength -eq $BallBalancePayloadLength))) {
            $ballBalanceFrames++
            if ($null -eq $firstBallBalanceTimestamp) {
                $firstBallBalanceTimestamp = $timestampUs
            }
            $lastBallBalanceTimestamp = $timestampUs

            $ballUpdateSequence = [BitConverter]::ToUInt32(
                $data, $payloadOffset)
            $ballElapsedMs = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 4)
            $ballAcceptedCommands = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 8)
            $ballRejectedCommands = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 12)
            $ballVisionInvalidCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 16)
            $ballFaultCount = [BitConverter]::ToUInt32(
                $data, $payloadOffset + 20)
            $ballControlOutputMdeg = [BitConverter]::ToInt32(
                $data, $payloadOffset + 24)
            $ballMotorCenterMdeg = [BitConverter]::ToInt32(
                $data, $payloadOffset + 28)
            $ballMotorTargetMdeg = [BitConverter]::ToInt32(
                $data, $payloadOffset + 32)
            $ballMotorActualMdeg = [BitConverter]::ToInt32(
                $data, $payloadOffset + 36)
            $ballMotorErrorMdeg = [BitConverter]::ToInt32(
                $data, $payloadOffset + 40)
            $ballTargetPositionDecimm = [BitConverter]::ToInt16(
                $data, $payloadOffset + 44)
            $ballMeasuredPositionDecimm = [BitConverter]::ToInt16(
                $data, $payloadOffset + 46)
            $ballVelocityMmS = [BitConverter]::ToInt16(
                $data, $payloadOffset + 48)
            $ballPositionErrorDecimm = [BitConverter]::ToInt16(
                $data, $payloadOffset + 50)
            $ballVisionSequence = [BitConverter]::ToUInt16(
                $data, $payloadOffset + 52)
            $ballVisionValidAgeMs = [BitConverter]::ToUInt16(
                $data, $payloadOffset + 54)
            $ballSettleMs = [BitConverter]::ToUInt16(
                $data, $payloadOffset + 56)
            $ballVisionFlags = $data[$payloadOffset + 58]
            $ballState = $data[$payloadOffset + 59]
            $ballMissionStatus = $data[$payloadOffset + 60]
            $ballFault = $data[$payloadOffset + 61]
            $ballStatus = $data[$payloadOffset + 62]
            $ballFeedforwardMdeg = 0
            $ballPlannedAccelMmS2 = 0
            $ballImuAccelMmS2 = 0
            $ballEncoderAccelMmS2 = 0
            if ($payloadLength -eq $BallBalancePayloadLength) {
                $ballFeedforwardMdeg = [BitConverter]::ToInt16(
                    $data, $payloadOffset + 64)
                $ballPlannedAccelMmS2 = [BitConverter]::ToInt16(
                    $data, $payloadOffset + 66)
                $ballImuAccelMmS2 = [BitConverter]::ToInt16(
                    $data, $payloadOffset + 68)
                $ballEncoderAccelMmS2 = [BitConverter]::ToInt16(
                    $data, $payloadOffset + 70)
            }
            $ballControlOutputDeg = $ballControlOutputMdeg / 1000.0
            $ballFeedforwardDeg = $ballFeedforwardMdeg / 1000.0
            $ballMotorCenterDeg = $ballMotorCenterMdeg / 1000.0
            $ballMotorTargetDeg = $ballMotorTargetMdeg / 1000.0
            $ballMotorActualDeg = $ballMotorActualMdeg / 1000.0
            $ballMotorErrorDeg = $ballMotorErrorMdeg / 1000.0
            $ballTargetPositionMm = $ballTargetPositionDecimm / 10.0
            $ballMeasuredPositionMm = $ballMeasuredPositionDecimm / 10.0
            $ballPositionErrorMm = $ballPositionErrorDecimm / 10.0

            $latestBallBalance = [pscustomobject]@{
                UpdateSequence = $ballUpdateSequence
                ElapsedMs = $ballElapsedMs
                AcceptedCommandCount = $ballAcceptedCommands
                RejectedCommandCount = $ballRejectedCommands
                VisionInvalidCount = $ballVisionInvalidCount
                FaultCount = $ballFaultCount
                ControlOutputDeg = $ballControlOutputDeg
                MotorCenterDeg = $ballMotorCenterDeg
                MotorTargetDeg = $ballMotorTargetDeg
                MotorActualDeg = $ballMotorActualDeg
                MotorErrorDeg = $ballMotorErrorDeg
                TargetPositionMm = $ballTargetPositionMm
                MeasuredPositionMm = $ballMeasuredPositionMm
                VelocityMmS = $ballVelocityMmS
                PositionErrorMm = $ballPositionErrorMm
                VisionSequence = $ballVisionSequence
                VisionValidAgeMs = $ballVisionValidAgeMs
                SettleMs = $ballSettleMs
                VisionFlags = $ballVisionFlags
                State = $ballState
                MissionStatus = $ballMissionStatus
                Fault = $ballFault
                VisionValid = (($ballStatus -band 0x01) -ne 0)
                Saturated = (($ballStatus -band 0x02) -ne 0)
                MotorOnline = (($ballStatus -band 0x04) -ne 0)
                MotorEnabled = (($ballStatus -band 0x08) -ne 0)
                FeedforwardDeg = $ballFeedforwardDeg
                PlannedAccelMmS2 = $ballPlannedAccelMmS2
                ImuAccelMmS2 = $ballImuAccelMmS2
                EncoderAccelMmS2 = $ballEncoderAccelMmS2
            }
            if ($null -ne $ballBalanceCsvWriter) {
                $values = @(
                    $sequence, $timestampUs, $ballUpdateSequence,
                    $ballElapsedMs, $ballAcceptedCommands,
                    $ballRejectedCommands, $ballVisionInvalidCount,
                    $ballFaultCount,
                    $ballControlOutputDeg.ToString("R", $culture),
                    $ballMotorCenterDeg.ToString("R", $culture),
                    $ballMotorTargetDeg.ToString("R", $culture),
                    $ballMotorActualDeg.ToString("R", $culture),
                    $ballMotorErrorDeg.ToString("R", $culture),
                    $ballTargetPositionMm.ToString("R", $culture),
                    $ballMeasuredPositionMm.ToString("R", $culture),
                    $ballVelocityMmS,
                    $ballPositionErrorMm.ToString("R", $culture),
                    $ballVisionSequence, $ballVisionValidAgeMs,
                    $ballSettleMs, $ballVisionFlags, $ballState,
                    $ballMissionStatus, $ballFault,
                    [int](($ballStatus -band 0x01) -ne 0),
                    [int](($ballStatus -band 0x02) -ne 0),
                    [int](($ballStatus -band 0x04) -ne 0),
                    [int](($ballStatus -band 0x08) -ne 0),
                    $ballFeedforwardDeg.ToString("R", $culture),
                    $ballPlannedAccelMmS2, $ballImuAccelMmS2,
                    $ballEncoderAccelMmS2
                )
                $ballBalanceCsvWriter.WriteLine($values -join ",")
            }
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
    if ($null -ne $imuCsvWriter) {
        $imuCsvWriter.Dispose()
    }
    if ($null -ne $attitudeCsvWriter) {
        $attitudeCsvWriter.Dispose()
    }
    if ($null -ne $reflectanceCsvWriter) {
        $reflectanceCsvWriter.Dispose()
    }
    if ($null -ne $ballBalanceCsvWriter) {
        $ballBalanceCsvWriter.Dispose()
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
    ActuatorAckFrames = $actuatorAckFrames
    MotorProfileFrames = $motorProfileFrames
    MotorProfileRateHz = Get-RateHz -Count $motorProfileFrames `
        -FirstTimestamp $firstMotorProfileTimestamp `
        -LastTimestamp $lastMotorProfileTimestamp
    ReflectanceFrames = $reflectanceFrames
    ReflectanceRateHz = Get-RateHz -Count $reflectanceFrames `
        -FirstTimestamp $firstReflectanceTimestamp `
        -LastTimestamp $lastReflectanceTimestamp
    ReflectanceScanRateHz = if (($reflectanceFrames -gt 1) -and
        ($lastReflectanceTimestamp -ne $firstReflectanceTimestamp)) {
        [Math]::Round(
            (Get-U32Delta -Current $lastReflectanceScanSequence `
                -Previous $firstReflectanceScanSequence) * 1000000.0 /
            (Get-U32Delta -Current $lastReflectanceTimestamp `
                -Previous $firstReflectanceTimestamp), 3)
    } else { 0.0 }
    SupplyVoltageFrames = $supplyVoltageFrames
    SupplyVoltageRateHz = Get-RateHz -Count $supplyVoltageFrames `
        -FirstTimestamp $firstSupplyVoltageTimestamp `
        -LastTimestamp $lastSupplyVoltageTimestamp
    SupplyVoltageSampleRateHz = if (($supplyVoltageFrames -gt 1) -and
        ($lastSupplyVoltageTimestamp -ne $firstSupplyVoltageTimestamp)) {
        [Math]::Round(
            (Get-U32Delta -Current $lastSupplySampleSequence `
                -Previous $firstSupplySampleSequence) * 1000000.0 /
            (Get-U32Delta -Current $lastSupplyVoltageTimestamp `
                -Previous $firstSupplyVoltageTimestamp), 3)
    } else { 0.0 }
    SupplyRawMinimum = if ($supplyVoltageFrames -gt 0) {
        $supplyRawMinimum
    } else { 0 }
    SupplyRawMaximum = $supplyRawMaximum
    SupplyRawAverage = if ($supplyVoltageFrames -gt 0) {
        [Math]::Round($supplyRawSum / [double]$supplyVoltageFrames, 3)
    } else { 0.0 }
    SupplyBatteryMinimumMv = if ($supplyVoltageFrames -gt 0) {
        $supplyBatteryMinimumMv
    } else { 0 }
    SupplyBatteryMaximumMv = $supplyBatteryMaximumMv
    SupplyBatteryAverageMv = if ($supplyVoltageFrames -gt 0) {
        [Math]::Round(
            $supplyBatterySumMv / [double]$supplyVoltageFrames, 1)
    } else { 0.0 }
    TfminiFrames = $tfminiFrames
    TfminiTelemetryRateHz = Get-RateHz -Count $tfminiFrames `
        -FirstTimestamp $firstTfminiTimestamp `
        -LastTimestamp $lastTfminiTimestamp
    TfminiDeviceFrameRateHz = if (($tfminiFrames -gt 1) -and
        ($lastTfminiTimestamp -ne $firstTfminiTimestamp)) {
        [Math]::Round(
            (Get-U32Delta -Current $lastTfminiSampleSequence `
                -Previous $firstTfminiSampleSequence) * 1000000.0 /
            (Get-U32Delta -Current $lastTfminiTimestamp `
                -Previous $firstTfminiTimestamp), 3)
    } else { 0.0 }
    TfminiValidDistanceCount = $tfminiValidDistanceCount
    TfminiDistanceMinimumCm = if ($tfminiValidDistanceCount -gt 0) {
        $tfminiDistanceMinimumCm
    } else { 0 }
    TfminiDistanceMaximumCm = $tfminiDistanceMaximumCm
    TfminiDistanceAverageCm = if ($tfminiValidDistanceCount -gt 0) {
        [Math]::Round(
            $tfminiDistanceSumCm / [double]$tfminiValidDistanceCount, 3)
    } else { 0.0 }
    ImuFrames = $imuFrames
    ImuTelemetryRateHz = Get-RateHz -Count $imuFrames `
        -FirstTimestamp $firstImuTimestamp -LastTimestamp $lastImuTimestamp
    ImuSampleRateHz = if (($imuFrames -gt 1) -and
        ($lastImuTimestamp -ne $firstImuTimestamp)) {
        [Math]::Round(
            (Get-U32Delta -Current $lastImuSampleSequence `
                -Previous $firstImuSampleSequence) * 1000000.0 /
            (Get-U32Delta -Current $lastImuTimestamp `
                -Previous $firstImuTimestamp), 3)
    } else { 0.0 }
    EspLinkFrames = $espLinkFrames
    EspLinkRateHz = Get-RateHz -Count $espLinkFrames `
        -FirstTimestamp $firstEspLinkTimestamp `
        -LastTimestamp $lastEspLinkTimestamp
    AttitudeFrames = $attitudeFrames
    AttitudeTelemetryRateHz = Get-RateHz -Count $attitudeFrames `
        -FirstTimestamp $firstAttitudeTimestamp `
        -LastTimestamp $lastAttitudeTimestamp
    AttitudeSampleRateHz = if (($attitudeFrames -gt 1) -and
        ($lastAttitudeTimestamp -ne $firstAttitudeTimestamp)) {
        [Math]::Round(
            (Get-U32Delta -Current $lastAttitudeSampleSequence `
                -Previous $firstAttitudeSampleSequence) * 1000000.0 /
            (Get-U32Delta -Current $lastAttitudeTimestamp `
                -Previous $firstAttitudeTimestamp), 3)
    } else { 0.0 }
    BallBalanceFrames = $ballBalanceFrames
    BallBalanceRateHz = Get-RateHz -Count $ballBalanceFrames `
        -FirstTimestamp $firstBallBalanceTimestamp `
        -LastTimestamp $lastBallBalanceTimestamp
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
    LatestActuatorAck = $latestActuatorAck
    LatestMotorProfile = $latestMotorProfile
    LatestReflectance = $latestReflectance
    LatestSupplyVoltage = $latestSupplyVoltage
    LatestTfmini = $latestTfmini
    LatestImu = $latestImu
    LatestEspLink = $latestEspLink
    LatestAttitude = $latestAttitude
    LatestBallBalance = $latestBallBalance
    CsvPath = $CsvPath
    ImuCsvPath = $ImuCsvPath
    AttitudeCsvPath = $AttitudeCsvPath
    ReflectanceCsvPath = $ReflectanceCsvPath
    BallBalanceCsvPath = $BallBalanceCsvPath
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

$diagnosticCapture = (-not [string]::IsNullOrWhiteSpace($ImuCsvPath)) -or
    (-not [string]::IsNullOrWhiteSpace($AttitudeCsvPath)) -or
    (-not [string]::IsNullOrWhiteSpace($BallBalanceCsvPath))
$minimumControlFrames = if (-not $diagnosticCapture) {
    [int]($DurationSeconds * 90)
} else { 0 }
$minimumHealthFrames = if ($DurationSeconds -ge 3) {
    [int][Math]::Floor($DurationSeconds * 0.7)
}
else {
    0
}
$minimumImuFrames = if ([string]::IsNullOrWhiteSpace($ImuCsvPath)) {
    [int]($DurationSeconds * 20)
} else {
    [int]($DurationSeconds * 80)
}
$minimumAttitudeFrames = if ([string]::IsNullOrWhiteSpace($AttitudeCsvPath)) {
    0
} else {
    [int]($DurationSeconds * 20)
}
$rateGateFailed = ($PSCmdlet.ParameterSetName -eq "Serial") -and
    (($controlFrames -lt $minimumControlFrames) -or
     ($healthFrames -lt $minimumHealthFrames) -or
     ($imuFrames -lt $minimumImuFrames) -or
     ($attitudeFrames -lt $minimumAttitudeFrames))
if (($crcErrors -ne 0) -or
    ($sequenceGaps -ne 0) -or
    ($sequenceDuplicates -ne 0) -or
    ($sequenceOutOfOrder -ne 0) -or
    ($deadlineMissCount -ne 0) -or $rateGateFailed) {
    exit 2
}
