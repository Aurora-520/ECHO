$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

$projectRoot = $EchoPaths.ProjectRoot
$openocd = Join-Path $EchoPaths.OpenOcdRoot "bin\openocd.exe"
$scripts = Join-Path $EchoPaths.OpenOcdRoot "openocd\scripts"
$hex = Join-Path $projectRoot "keil\Objects\ECHO.hex"

foreach ($requiredPath in @($openocd, $scripts, $hex)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required file or directory not found: $requiredPath"
    }
}

$hexForTcl = $hex.Replace("\", "/")
$programCommand = 'program "{0}" verify reset exit' -f $hexForTcl

Write-Host "Flashing ECHO through DAPLink..." -ForegroundColor Cyan
Write-Host "Image: $hex"
Write-Host "SWD frequency: 1 MHz"

$arguments = @(
    "-s", $scripts,
    "-f", "interface/cmsis-dap.cfg",
    "-f", "target/ti_mspm0.cfg",
    "-c", "adapter speed 1000",
    "-c", $programCommand
)
& $openocd @arguments
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD flashing failed with exit code $LASTEXITCODE."
}
Write-Host "Flash and verification succeeded." -ForegroundColor Green
