[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet("Static", "Motion")]
    [string]$Mode = "Static",
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(10, 900)]
    [int]$DurationSeconds = 20,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ImuFlag = 0x02
$ValidFlag = 0x04
$ReadyFlag = 0x10

function Get-Statistics {
    param([double[]]$Values)

    if ($Values.Count -eq 0) {
        return [pscustomobject]@{ Count = 0; Mean = 0.0; StdDev = 0.0; Min = 0.0; Max = 0.0 }
    }
    $mean = ($Values | Measure-Object -Average).Average
    $variance = 0.0
    foreach ($value in $Values) {
        $delta = $value - $mean
        $variance += $delta * $delta
    }
    $variance /= $Values.Count
    return [pscustomobject]@{
        Count = $Values.Count
        Mean = [Math]::Round($mean, 6)
        StdDev = [Math]::Round([Math]::Sqrt($variance), 6)
        Min = [Math]::Round(($Values | Measure-Object -Minimum).Minimum, 6)
        Max = [Math]::Round(($Values | Measure-Object -Maximum).Maximum, 6)
    }
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $env:TEMP "echo-mpu6050-$stamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$rawPath = Join-Path $OutputDirectory "capture.bin"
$csvPath = Join-Path $OutputDirectory "capture.csv"
$captureJsonPath = Join-Path $OutputDirectory "capture.json"
$resultJsonPath = Join-Path $OutputDirectory "mpu6050-result.json"
$captureScript = Join-Path $PSScriptRoot "telemetry_capture.ps1"
$hostStartUtc = [DateTime]::UtcNow
$hostStopwatch = [System.Diagnostics.Stopwatch]::StartNew()

& $captureScript -Port $Port -BaudRate $BaudRate `
    -DurationSeconds $DurationSeconds -FlushSeconds 1 `
    -RawPath $rawPath -CsvPath $csvPath -JsonPath $captureJsonPath
$hostStopwatch.Stop()
$hostEndUtc = [DateTime]::UtcNow
$captureExitCode = $LASTEXITCODE
if (($null -ne $captureExitCode) -and ($captureExitCode -ne 0)) {
    throw "Telemetry capture gate failed with exit code $captureExitCode."
}

$capture = Get-Content -LiteralPath $captureJsonPath -Raw |
    ConvertFrom-Json
$rows = @(Import-Csv -LiteralPath $csvPath)
$imuRows = @($rows | Where-Object {
    (([uint32]$_.flags -band $ImuFlag) -ne 0) -and
    (([uint32]$_.flags -band $ValidFlag) -ne 0)
})
$readyRows = @($imuRows | Where-Object {
    ([uint32]$_.flags -band $ReadyFlag) -ne 0
})

$culture = [System.Globalization.CultureInfo]::InvariantCulture
[double[]]$gyroX = @($readyRows | ForEach-Object {
    [double]::Parse($_.setpoint, $culture)
})
[double[]]$gyroY = @($readyRows | ForEach-Object {
    [double]::Parse($_.measurement, $culture)
})
[double[]]$gyroZ = @($readyRows | ForEach-Object {
    [double]::Parse($_.control_output, $culture)
})
[double[]]$accelNorm = @($readyRows | ForEach-Object {
    [double]::Parse($_.auxiliary, $culture)
})

$x = Get-Statistics -Values $gyroX
$y = Get-Statistics -Values $gyroY
$z = Get-Statistics -Values $gyroZ
$accel = Get-Statistics -Values $accelNorm
$minimumReadyFrames = [Math]::Max(100, ($DurationSeconds - 5) * 90)
$frozenEpsilon = 0.000001
$health = $capture.LatestHealth
$failures = [System.Collections.Generic.List[string]]::new()

if ($readyRows.Count -lt $minimumReadyFrames) {
    $failures.Add("IMU ready frames $($readyRows.Count) < $minimumReadyFrames")
}
if (($readyRows.Count -ge 10) -and
    (($x.Max - $x.Min) -le $frozenEpsilon) -and
    (($y.Max - $y.Min) -le $frozenEpsilon) -and
    (($z.Max - $z.Min) -le $frozenEpsilon) -and
    (($accel.Max - $accel.Min) -le $frozenEpsilon)) {
    $failures.Add("IMU telemetry is frozen on every published channel")
}
if ($null -eq $health) {
    $failures.Add("No Health frame was captured")
}
elseif ($health.I2cErrorCount -ne 0) {
    $failures.Add("I2C error count is $($health.I2cErrorCount)")
}
else {
    if ($health.ActiveIssueMask -ne "0x00000000") {
        $failures.Add("Health active issue mask is $($health.ActiveIssueMask)")
    }
    if ($health.StickyIssueMask -ne "0x00000000") {
        $failures.Add("Health sticky issue mask is $($health.StickyIssueMask)")
    }
    if ($health.QuietAcquiredCount -ne $health.QuietReleasedCount) {
        $failures.Add(
            "Quiet acquire/release mismatch: $($health.QuietAcquiredCount)/$($health.QuietReleasedCount)")
    }
    if ($health.QuietWindowActive -ne 0) {
        $failures.Add("Quiet window is still active")
    }
}

if ($Mode -eq "Static") {
    foreach ($axis in @($x, $y, $z)) {
        if ([Math]::Abs($axis.Mean) -gt 1.0) {
            $failures.Add("Static gyro mean exceeds 1.0 dps")
            break
        }
        if ($axis.StdDev -gt 0.5) {
            $failures.Add("Static gyro standard deviation exceeds 0.5 dps")
            break
        }
    }
    if (($accel.Mean -lt 0.80) -or ($accel.Mean -gt 1.20)) {
        $failures.Add("Static acceleration magnitude is outside 0.80-1.20 g")
    }
}
else {
    $largestRange = [Math]::Max($x.Max - $x.Min,
        [Math]::Max($y.Max - $y.Min, $z.Max - $z.Min))
    if ($largestRange -lt 10.0) {
        $failures.Add("Motion range is below 10 dps on every axis")
    }
}

$result = [pscustomobject]@{
    Mode = $Mode
    Port = $Port
    DurationSeconds = $DurationSeconds
    HostStartUtc = $hostStartUtc.ToString("O")
    HostEndUtc = $hostEndUtc.ToString("O")
    HostElapsedSeconds = [Math]::Round(
        $hostStopwatch.Elapsed.TotalSeconds, 3)
    Passed = ($failures.Count -eq 0)
    Failures = @($failures)
    Capture = $capture
    ImuValidFrames = $imuRows.Count
    ImuReadyFrames = $readyRows.Count
    GyroXDps = $x
    GyroYDps = $y
    GyroZDps = $z
    AccelNormG = $accel
    RawPath = $rawPath
    OutputDirectory = $OutputDirectory
}
$result | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $resultJsonPath -Encoding UTF8
$result | Format-List

if ($failures.Count -ne 0) {
    exit 2
}
