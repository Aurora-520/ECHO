param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(10, 900)]
    [int]$DurationSeconds = 120,
    [string]$OutputDirectory = "",
    [switch]$SkipFlash,
    [switch]$AllowDegraded
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $EchoPaths.ProjectRoot `
        "tests\artifacts\phase1f-field-$timestamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$captureJson = Join-Path $OutputDirectory "capture.json"
$captureCsv = Join-Path $OutputDirectory "control.csv"
$summaryJson = Join-Path $OutputDirectory "field-check.json"
$hexPath = Join-Path $EchoPaths.ProjectRoot "keil\Objects\ECHO.hex"
$stage = "environment"
$result = [ordered]@{
    SchemaVersion = 1
    StartedAt = (Get-Date).ToString("o")
    CompletedAt = $null
    Result = "running"
    FailedStage = $null
    Port = $Port
    BaudRate = $BaudRate
    DurationSeconds = $DurationSeconds
    Flash = if ($SkipFlash) { "skipped" } else { "pending" }
    HexSha256 = $null
    HexBytes = $null
    Capture = $null
    OutputDirectory = $OutputDirectory
}

try {
    & (Join-Path $PSScriptRoot "check_environment.ps1") -Scope All

    $stage = "build"
    & (Join-Path $PSScriptRoot "build_echo.ps1") -Mode All
    if (-not (Test-Path -LiteralPath $hexPath -PathType Leaf)) {
        throw "Build completed without ECHO.hex."
    }
    $result.HexSha256 = (Get-FileHash -LiteralPath $hexPath `
        -Algorithm SHA256).Hash
    $result.HexBytes = (Get-Item -LiteralPath $hexPath).Length

    if (-not $SkipFlash) {
        $stage = "swd-flash"
        & (Join-Path $PSScriptRoot "flash_echo.ps1")
        if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
            throw "Flash tool returned $LASTEXITCODE."
        }
        $result.Flash = "passed"
        Start-Sleep -Seconds 2
    }

    $stage = "uart-protocol"
    & (Join-Path $PSScriptRoot "telemetry_capture.ps1") `
        -Port $Port `
        -BaudRate $BaudRate `
        -DurationSeconds $DurationSeconds `
        -CsvPath $captureCsv `
        -JsonPath $captureJson | Out-Host
    if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
        throw "Telemetry capture returned $LASTEXITCODE."
    }

    $capture = Get-Content -Raw -LiteralPath $captureJson |
        ConvertFrom-Json
    $result.Capture = $capture
    if ($null -eq $capture.LatestHealth) {
        throw "No Health frame was decoded."
    }
    if ($capture.LatestHealth.ActuatorOutputPermitted -ne 0) {
        throw "Actuator safety gate is unexpectedly open."
    }
    if (($capture.LatestHealth.DeadlineMissCount -ne 0) -or
        ($capture.LatestHealth.PublishDropCount -ne 0) -or
        ($capture.LatestHealth.TransportDropCount -ne 0) -or
        ($capture.LatestHealth.SerialTxDropCount -ne 0) -or
        ($capture.LatestHealth.SerialRxOverflowCount -ne 0)) {
        throw "Health snapshot reports deadline, drop, or overflow errors."
    }
    if ((-not $AllowDegraded) -and ($capture.LatestHealth.Level -ne 1)) {
        throw "Health level is not OK: $($capture.LatestHealth.Level)."
    }

    $stage = "complete"
    $result.Result = "passed"
}
catch {
    $result.Result = "failed"
    $result.FailedStage = $stage
    throw
}
finally {
    $result.CompletedAt = (Get-Date).ToString("o")
    $result | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $summaryJson -Encoding UTF8
    Write-Output "Phase 1F field summary: $summaryJson"
}
