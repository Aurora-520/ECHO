[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(60, 43200)]
    [int]$TotalDurationSeconds = 28800,
    [ValidateRange(10, 900)]
    [int]$SegmentDurationSeconds = 900,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$fieldCheck = Join-Path $PSScriptRoot "mpu6050_field_check.ps1"

function Write-JsonAtomic {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Value,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $temporaryPath = "$Path.tmp"
    $Value | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $temporaryPath -Encoding UTF8
    Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
}

function Get-SegmentDurations {
    param(
        [int]$TotalSeconds,
        [int]$SegmentSeconds
    )

    $durations = [System.Collections.Generic.List[int]]::new()
    $fullSegments = [Math]::Floor($TotalSeconds / $SegmentSeconds)
    $remainder = $TotalSeconds % $SegmentSeconds

    for ($index = 0; $index -lt $fullSegments; $index++) {
        $durations.Add($SegmentSeconds)
    }
    if ($remainder -ne 0) {
        if ($durations.Count -eq 0) {
            $durations.Add($remainder)
        } else {
            $lastIndex = $durations.Count - 1
            if (($durations[$lastIndex] + $remainder) -le 900) {
                $durations[$lastIndex] += $remainder
            } else {
                $borrowedSeconds = 10 - $remainder
                $durations[$lastIndex] -= $borrowedSeconds
                $durations.Add(10)
            }
        }
    }
    return $durations.ToArray()
}

if (-not (Test-Path -LiteralPath $fieldCheck)) {
    throw "Field check script not found: $fieldCheck"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $env:TEMP "echo-unattended-soak-$stamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$progressPath = Join-Path $OutputDirectory "soak-progress.json"
$resultPath = Join-Path $OutputDirectory "soak-result.json"
$hostStartUtc = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$segments = [System.Collections.Generic.List[object]]::new()
$durations = @(Get-SegmentDurations -TotalSeconds $TotalDurationSeconds `
    -SegmentSeconds $SegmentDurationSeconds)
$overallPassed = $true
$failureReason = ""
$totalControlFrames = [int64]0
$totalHealthFrames = [int64]0
$minimumStackFreeWords = [uint32]::MaxValue
$minimumHeapFreeBytes = [uint32]::MaxValue
$maximumSerialRingHighWaterBytes = [uint32]0
$maximumQuietWindowUs = [uint32]0

for ($index = 0; $index -lt $durations.Count; $index++) {
    $segmentNumber = $index + 1
    $duration = [int]$durations[$index]
    $segmentDirectory = Join-Path $OutputDirectory (
        "segment-{0:D3}" -f $segmentNumber)
    $segmentLogPath = Join-Path $segmentDirectory "field-check.log"
    New-Item -ItemType Directory -Path $segmentDirectory -Force |
        Out-Null

    $segmentStartUtc = [DateTime]::UtcNow
    $fieldOutput = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $fieldCheck -Port $Port -BaudRate $BaudRate `
        -Mode Static -DurationSeconds $duration `
        -OutputDirectory $segmentDirectory 2>&1
    $fieldExitCode = $LASTEXITCODE
    $fieldOutput | Set-Content -LiteralPath $segmentLogPath -Encoding UTF8
    $segmentEndUtc = [DateTime]::UtcNow
    $fieldResultPath = Join-Path $segmentDirectory "mpu6050-result.json"

    $segment = [ordered]@{
        Index = $segmentNumber
        RequestedDurationSeconds = $duration
        StartUtc = $segmentStartUtc.ToString("O")
        EndUtc = $segmentEndUtc.ToString("O")
        ExitCode = $fieldExitCode
        Passed = $false
        Failures = @()
        OutputDirectory = $segmentDirectory
    }

    if (Test-Path -LiteralPath $fieldResultPath) {
        $fieldResult = Get-Content -LiteralPath $fieldResultPath `
            -Encoding UTF8 -Raw | ConvertFrom-Json
        $health = $fieldResult.Capture.LatestHealth
        $segment.Passed = [bool]$fieldResult.Passed
        $segment.Failures = @($fieldResult.Failures)
        $segment.HostElapsedSeconds = $fieldResult.HostElapsedSeconds
        $segment.ControlFrames = $fieldResult.Capture.ControlFrames
        $segment.HealthFrames = $fieldResult.Capture.HealthFrames
        $segment.ImuReadyFrames = $fieldResult.ImuReadyFrames
        $segment.CrcErrors = $fieldResult.Capture.CrcErrors
        $segment.SequenceGaps = $fieldResult.Capture.SequenceGaps
        $segment.SequenceDuplicates = `
            $fieldResult.Capture.SequenceDuplicates
        $segment.SequenceOutOfOrder = `
            $fieldResult.Capture.SequenceOutOfOrder
        $segment.DeadlineMisses = `
            $fieldResult.Capture.DeadlineMissCount
        if ($null -ne $health) {
            $segment.I2cErrors = $health.I2cErrorCount
            $segment.ActiveIssueMask = $health.ActiveIssueMask
            $segment.StickyIssueMask = $health.StickyIssueMask
            $segment.OledOnline = $health.OledOnline
            $segment.MinimumStackFreeWords = $health.MinimumStackFreeWords
            $segment.HeapMinEverFreeBytes = $health.HeapMinEverFreeBytes
            $segment.SerialRingHighWaterBytes = `
                $health.SerialRingHighWaterBytes
            $segment.MaxQuietWindowUs = $health.MaxQuietWindowUs
            $segment.QuietWindowActive = $health.QuietWindowActive

            $minimumStackFreeWords = [Math]::Min(
                $minimumStackFreeWords,
                [uint32]$health.MinimumStackFreeWords)
            $minimumHeapFreeBytes = [Math]::Min(
                $minimumHeapFreeBytes,
                [uint32]$health.HeapMinEverFreeBytes)
            $maximumSerialRingHighWaterBytes = [Math]::Max(
                $maximumSerialRingHighWaterBytes,
                [uint32]$health.SerialRingHighWaterBytes)
            $maximumQuietWindowUs = [Math]::Max(
                $maximumQuietWindowUs,
                [uint32]$health.MaxQuietWindowUs)
        }
        $totalControlFrames += [int64]$fieldResult.Capture.ControlFrames
        $totalHealthFrames += [int64]$fieldResult.Capture.HealthFrames
    } else {
        $segment.Failures = @(
            "Field check did not create mpu6050-result.json")
    }

    $segmentObject = [pscustomobject]$segment
    $segments.Add($segmentObject)
    $overallPassed = $overallPassed -and $segmentObject.Passed -and
        ($fieldExitCode -eq 0)
    if (-not $overallPassed) {
        $failureReason = "Segment $segmentNumber failed"
    }

    $progress = [pscustomobject]@{
        Port = $Port
        BaudRate = $BaudRate
        TotalDurationSeconds = $TotalDurationSeconds
        SegmentDurationSeconds = $SegmentDurationSeconds
        HostStartUtc = $hostStartUtc.ToString("O")
        UpdatedUtc = [DateTime]::UtcNow.ToString("O")
        ElapsedSeconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
        PassedSoFar = $overallPassed
        FailureReason = $failureReason
        CompletedSegments = $segments.Count
        PlannedSegments = $durations.Count
        Segments = @($segments)
    }
    Write-JsonAtomic -Value $progress -Path $progressPath
    $segmentObject | Format-List

    if (-not $overallPassed) {
        break
    }
}

$stopwatch.Stop()
if ($minimumStackFreeWords -eq [uint32]::MaxValue) {
    $minimumStackFreeWords = [uint32]0
}
if ($minimumHeapFreeBytes -eq [uint32]::MaxValue) {
    $minimumHeapFreeBytes = [uint32]0
}

$result = [pscustomobject]@{
    Port = $Port
    BaudRate = $BaudRate
    TotalDurationSeconds = $TotalDurationSeconds
    SegmentDurationSeconds = $SegmentDurationSeconds
    HostStartUtc = $hostStartUtc.ToString("O")
    HostEndUtc = [DateTime]::UtcNow.ToString("O")
    HostElapsedSeconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
    Passed = $overallPassed -and ($segments.Count -eq $durations.Count)
    FailureReason = $failureReason
    CompletedSegments = $segments.Count
    PlannedSegments = $durations.Count
    TotalControlFrames = $totalControlFrames
    TotalHealthFrames = $totalHealthFrames
    MinimumStackFreeWords = $minimumStackFreeWords
    MinimumHeapFreeBytes = $minimumHeapFreeBytes
    MaximumSerialRingHighWaterBytes = $maximumSerialRingHighWaterBytes
    MaximumQuietWindowUs = $maximumQuietWindowUs
    Segments = @($segments)
    OutputDirectory = $OutputDirectory
}
Write-JsonAtomic -Value $result -Path $resultPath
$result | Format-List

if (-not $result.Passed) {
    exit 2
}
