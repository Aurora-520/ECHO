param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")
$ErrorActionPreference = "Continue"

# Only stop debug tools used by ECHO. Do not terminate Keil or VSCode.
$knownExecutables = @(
    @{ Name = "openocd"; Path = (Join-Path $EchoPaths.OpenOcdRoot "bin\openocd.exe") },
    @{ Name = "pyocd"; Path = (Join-Path $EchoPaths.ProjectRoot ".tools\pyocd\Scripts\pyocd.exe") },
    @{ Name = "arm-none-eabi-gdb"; Path = (Join-Path $EchoPaths.GnuArmBin "arm-none-eabi-gdb.exe") }
)

foreach ($item in $knownExecutables) {
    $expectedPath = [System.IO.Path]::GetFullPath($item.Path)
    $processes = Get-Process -Name $item.Name -ErrorAction SilentlyContinue
    foreach ($process in $processes) {
        $processPath = $null
        try {
            $processPath = $process.Path
        }
        catch {
            # Processes owned by another user may not expose Path.
        }
        if ($processPath -and ([System.IO.Path]::GetFullPath($processPath) -ieq $expectedPath)) {
            Write-Host "Stopping stale ECHO debug process: $($process.ProcessName) [$($process.Id)]"
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
Write-Host "ECHO debug tools are ready." -ForegroundColor Green
