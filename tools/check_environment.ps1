param(
    [ValidateSet("Build", "Debug", "All")]
    [string]$Scope = "All",

    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

$checks = @()
if ($Scope -in @("Build", "All")) {
    $checks += @(
        @{ Name = "MSPM0 SDK"; Path = $EchoPaths.SdkRoot },
        @{ Name = "SDK product.json"; Path = (Join-Path $EchoPaths.SdkRoot ".metadata\product.json") },
        @{ Name = "DriverLib"; Path = (Join-Path $EchoPaths.SdkRoot "source\ti\driverlib\lib\keil\m0p\mspm0g1x0x_g3x0x\driverlib.a") },
        @{ Name = "FreeRTOS tasks.c"; Path = (Join-Path $EchoPaths.SdkRoot "kernel\freertos\Source\tasks.c") },
        @{ Name = "Keil uVision"; Path = $EchoPaths.KeilUv4 },
        @{ Name = "ArmClang"; Path = (Join-Path $EchoPaths.ArmClangBin "armclang.exe") },
        @{ Name = "SysConfig CLI"; Path = $EchoPaths.SysConfigCli },
        @{ Name = "Application project"; Path = (Join-Path $EchoPaths.ProjectRoot "keil\ECHO.uvprojx") },
        @{ Name = "FreeRTOS project"; Path = (Join-Path $EchoPaths.ProjectRoot "freertos\keil\freertos_ECHO.uvprojx") }
    )
}
if ($Scope -in @("Debug", "All")) {
    $checks += @(
        @{ Name = "OpenOCD"; Path = (Join-Path $EchoPaths.OpenOcdRoot "bin\openocd.exe") },
        @{ Name = "CMSIS-DAP config"; Path = (Join-Path $EchoPaths.OpenOcdRoot "openocd\scripts\interface\cmsis-dap.cfg") },
        @{ Name = "MSPM0 target config"; Path = (Join-Path $EchoPaths.OpenOcdRoot "openocd\scripts\target\ti_mspm0.cfg") },
        @{ Name = "GNU Arm GDB"; Path = (Join-Path $EchoPaths.GnuArmBin "arm-none-eabi-gdb.exe") },
        @{ Name = "GNU Arm objdump"; Path = (Join-Path $EchoPaths.GnuArmBin "arm-none-eabi-objdump.exe") }
    )
}

$missing = @()
foreach ($check in $checks) {
    $exists = Test-Path -LiteralPath $check.Path
    if (-not $Quiet) {
        $state = if ($exists) { "OK" } else { "MISSING" }
        $color = if ($exists) { "Green" } else { "Red" }
        Write-Host ("[{0}] {1}: {2}" -f $state, $check.Name, $check.Path) -ForegroundColor $color
    }
    if (-not $exists) {
        $missing += $check
    }
}

if ($missing.Count -gt 0) {
    throw "$($missing.Count) required path(s) are missing. Update $($EchoPaths.LocalConfig)."
}
if (-not $Quiet) {
    Write-Host "ECHO $Scope environment is ready." -ForegroundColor Green
}
