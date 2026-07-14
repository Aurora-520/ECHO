$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

$projectRoot = $EchoPaths.ProjectRoot
$openocd = Join-Path $EchoPaths.OpenOcdRoot "bin\openocd.exe"
$scripts = Join-Path $EchoPaths.OpenOcdRoot "openocd\scripts"
$objcopy = Join-Path $EchoPaths.GnuArmBin "arm-none-eabi-objcopy.exe"
$hex = Join-Path $projectRoot "keil\Objects\ECHO.hex"

foreach ($requiredPath in @($openocd, $scripts, $objcopy, $hex)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required file or directory not found: $requiredPath"
    }
}

function Invoke-EchoOpenOcd {
    param([string[]]$Commands)

    $arguments = @(
        "-s", $scripts,
        "-f", "interface/cmsis-dap.cfg",
        "-f", "target/ti_mspm0.cfg",
        "-c", "adapter speed 1000"
    )
    foreach ($command in $Commands) {
        $arguments += @("-c", $command)
    }
    & $openocd @arguments
}

$hexForTcl = $hex.Replace("\", "/")
$programCommand = 'program "{0}" verify reset exit' -f $hexForTcl

Write-Host "Flashing ECHO through DAPLink..." -ForegroundColor Cyan
Write-Host "Image: $hex"
Write-Host "SWD frequency: 1 MHz"

Invoke-EchoOpenOcd -Commands @($programCommand)
$fastVerifyExitCode = $LASTEXITCODE
if ($fastVerifyExitCode -eq 0) {
    Write-Host "Flash and fast verification succeeded." -ForegroundColor Green
    return
}

Write-Host "OpenOCD target CRC verification was unavailable." -ForegroundColor Yellow
Write-Host "Falling back to byte-for-byte Flash readback verification." -ForegroundColor Yellow

$tempRoot = [System.IO.Path]::GetTempPath()
$expectedBinary = Join-Path $tempRoot ("ECHO_expected_{0}.bin" -f $PID)
$readbackBinary = Join-Path $tempRoot ("ECHO_readback_{0}.bin" -f $PID)

try {
    & $objcopy -I ihex -O binary $hex $expectedBinary
    if ($LASTEXITCODE -ne 0) {
        throw "objcopy could not convert ECHO.hex for readback verification."
    }

    $imageLength = (Get-Item -LiteralPath $expectedBinary).Length
    if ($imageLength -le 0) {
        throw "ECHO.hex produced an empty binary image."
    }

    $readbackForTcl = $readbackBinary.Replace("\", "/")
    $dumpCommand = 'dump_image "{0}" 0x00000000 {1}' -f `
        $readbackForTcl, $imageLength

    Invoke-EchoOpenOcd -Commands @(
        "init",
        "reset halt",
        $dumpCommand,
        "shutdown"
    )
    if ($LASTEXITCODE -ne 0) {
        throw "OpenOCD Flash readback failed with exit code $LASTEXITCODE."
    }

    $expectedHash = (Get-FileHash -LiteralPath $expectedBinary `
        -Algorithm SHA256).Hash
    $readbackHash = (Get-FileHash -LiteralPath $readbackBinary `
        -Algorithm SHA256).Hash
    if ($expectedHash -ne $readbackHash) {
        throw @"
Flash readback mismatch.
Expected: $expectedHash
Readback: $readbackHash
"@
    }

    Invoke-EchoOpenOcd -Commands @("init", "reset run", "shutdown")
    if ($LASTEXITCODE -ne 0) {
        throw "Flash verified, but target reset failed with exit code $LASTEXITCODE."
    }

    Write-Host "Flash readback SHA-256: $readbackHash"
    Write-Host "Flash and byte-for-byte verification succeeded." -ForegroundColor Green
}
finally {
    Remove-Item -LiteralPath $expectedBinary, $readbackBinary `
        -Force -ErrorAction SilentlyContinue
}
