[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(20, 120)]
    [int]$DurationSeconds = 30,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ImuFlag = 0x02
$ValidFlag = 0x04
$CalibratingFlag = 0x08
$ReadyFlag = 0x10
$ImuIssueMask = 0x00018000
$captureScript = Join-Path $PSScriptRoot "telemetry_capture.ps1"

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $env:TEMP `
        "echo-mpu6050-debug-recovery-$stamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$rawPath = Join-Path $OutputDirectory "capture.bin"
$csvPath = Join-Path $OutputDirectory "capture.csv"
$captureJsonPath = Join-Path $OutputDirectory "capture.json"
$resultPath = Join-Path $OutputDirectory "recovery-result.json"

& $captureScript -Port $Port -BaudRate $BaudRate `
    -DurationSeconds $DurationSeconds -FlushSeconds 1 `
    -RawPath $rawPath -CsvPath $csvPath -JsonPath $captureJsonPath
$captureExitCode = $LASTEXITCODE
if (($null -ne $captureExitCode) -and ($captureExitCode -ne 0)) {
    throw "Telemetry capture failed with exit code $captureExitCode."
}

$capture = Get-Content -LiteralPath $captureJsonPath -Raw |
    ConvertFrom-Json
$rows = @(Import-Csv -LiteralPath $csvPath)
if ($rows.Count -eq 0) {
    throw "No Control rows were captured."
}

function Get-ImuState {
    param([uint32]$Flags)

    if (($Flags -band $ReadyFlag) -ne 0) {
        return "READY"
    }
    if (($Flags -band $CalibratingFlag) -ne 0) {
        return "CALIBRATING"
    }
    if (($Flags -band $ValidFlag) -ne 0) {
        return "VALID_NOT_READY"
    }
    return "INVALID"
}

function Find-StateIndex {
    param(
        [string[]]$States,
        [string]$Target,
        [int]$StartIndex
    )

    for ($index = $StartIndex; $index -lt $States.Count; $index++) {
        if ($States[$index] -eq $Target) {
            return $index
        }
    }
    return -1
}

$firstTimestamp = [double]$rows[0].timestamp_us
$segments = [System.Collections.Generic.List[object]]::new()
$invalidRows = [System.Collections.Generic.List[object]]::new()
$currentState = $null
foreach ($row in $rows) {
    $flags = [uint32]$row.flags
    if (($flags -band $ImuFlag) -eq 0) {
        continue
    }
    $state = Get-ImuState -Flags $flags
    if ($state -eq "INVALID") {
        $invalidRows.Add($row)
    }
    $seconds = ([double]$row.timestamp_us - $firstTimestamp) / 1000000.0
    if ($state -ne $currentState) {
        $segments.Add([pscustomobject]@{
            State = $state
            StartSeconds = [Math]::Round($seconds, 3)
            EndSeconds = [Math]::Round($seconds, 3)
            Count = 1
        })
        $currentState = $state
    }
    else {
        $segment = $segments[$segments.Count - 1]
        $segment.EndSeconds = [Math]::Round($seconds, 3)
        $segment.Count++
    }
}

[string[]]$stateNames = @($segments | ForEach-Object { $_.State })
$initialReadyIndex = Find-StateIndex $stateNames "READY" 0
$invalidIndex = Find-StateIndex $stateNames "INVALID" `
    ([Math]::Max(0, $initialReadyIndex + 1))
$calibratingIndex = Find-StateIndex $stateNames "CALIBRATING" `
    ([Math]::Max(0, $invalidIndex + 1))
$recoveredReadyIndex = Find-StateIndex $stateNames "READY" `
    ([Math]::Max(0, $calibratingIndex + 1))

$failures = [System.Collections.Generic.List[string]]::new()
if ($initialReadyIndex -lt 0) {
    $failures.Add("No initial READY segment was captured")
}
if ($invalidIndex -lt 0) {
    $failures.Add("No INVALID segment followed the injection")
}
if ($calibratingIndex -lt 0) {
    $failures.Add("No CALIBRATING segment followed INVALID")
}
if ($recoveredReadyIndex -lt 0) {
    $failures.Add("No recovered READY segment followed CALIBRATING")
}
if ($invalidRows.Count -eq 0) {
    $failures.Add("No invalid Control frames were captured")
}
else {
    $nonzeroInvalidRows = @($invalidRows | Where-Object {
        ([Math]::Abs([double]$_.setpoint) -gt 0.000001) -or
        ([Math]::Abs([double]$_.measurement) -gt 0.000001) -or
        ([Math]::Abs([double]$_.control_output) -gt 0.000001)
    })
    if ($nonzeroInvalidRows.Count -ne 0) {
        $failures.Add("Invalid IMU frames still contain nonzero gyro data")
    }
}
if ($capture.CrcErrors -ne 0) {
    $failures.Add("CRC errors: $($capture.CrcErrors)")
}
if ($capture.SequenceGaps -ne 0) {
    $failures.Add("Sequence gaps: $($capture.SequenceGaps)")
}
if ($capture.SequenceDuplicates -ne 0) {
    $failures.Add("Sequence duplicates: $($capture.SequenceDuplicates)")
}
if ($capture.SequenceOutOfOrder -ne 0) {
    $failures.Add("Out-of-order frames: $($capture.SequenceOutOfOrder)")
}
if ($capture.DeadlineMissCount -ne 0) {
    $failures.Add("Deadline misses: $($capture.DeadlineMissCount)")
}
if ($null -eq $capture.LatestHealth) {
    $failures.Add("No Health frame was captured")
}
else {
    if ($capture.LatestHealth.ActiveIssueMask -ne "0x00000000") {
        $failures.Add(
            "Final Health active issue mask is $($capture.LatestHealth.ActiveIssueMask)")
    }
    $stickyMask = [Convert]::ToUInt32(
        $capture.LatestHealth.StickyIssueMask.Substring(2), 16)
    if (($stickyMask -band $ImuIssueMask) -eq 0) {
        $failures.Add("Final Health did not retain an IMU offline/stale sticky issue")
    }
}

$result = [pscustomobject]@{
    Port = $Port
    DurationSeconds = $DurationSeconds
    Passed = ($failures.Count -eq 0)
    Failures = @($failures)
    Segments = @($segments)
    InvalidFrameCount = $invalidRows.Count
    Capture = $capture
    OutputDirectory = $OutputDirectory
}
$result | ConvertTo-Json -Depth 7 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8
$result | Format-List
$segments | Format-Table -AutoSize

if ($failures.Count -ne 0) {
    exit 2
}
