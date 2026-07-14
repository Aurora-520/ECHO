param(
    [ValidateRange(1024, 65535)]
    [int]$Port = 8765
)

$ErrorActionPreference = "Stop"
$webRoot = Join-Path $PSScriptRoot "telemetry-web"
if (-not (Test-Path -LiteralPath (Join-Path $webRoot "index.html"))) {
    throw "Telemetry web files were not found: $webRoot"
}

$python = Get-Command python.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.Source -notlike "*WindowsApps*" } |
    Select-Object -First 1
if ($null -eq $python) {
    $python = Get-Command py.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
}
if ($null -eq $python) {
    throw "Python was not found. Use the VSCode Live Server extension instead."
}

Write-Host "ECHO telemetry web tool" -ForegroundColor Cyan
Write-Host "URL: http://localhost:$Port"
Write-Host "Stop: Ctrl+C"

Push-Location $webRoot
try {
    & $python.Source -m http.server $Port --bind 127.0.0.1
    if ($LASTEXITCODE -ne 0) {
        throw "Telemetry web server exited with code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
