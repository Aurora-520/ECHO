param(
    [ValidateSet("App", "All")]
    [string]$Mode = "App"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

$projectRoot = $EchoPaths.ProjectRoot
$uv4 = $EchoPaths.KeilUv4
$kernelProject = Join-Path $projectRoot "freertos\keil\freertos_ECHO.uvprojx"
$appProject = Join-Path $projectRoot "keil\ECHO.uvprojx"
$configHeader = Join-Path $projectRoot "freertos\FreeRTOSConfig.h"
$kernelLibrary = Join-Path $projectRoot "freertos\keil\Objects\freertos_ECHO.lib"
$appImage = Join-Path $projectRoot "keil\Objects\ECHO.axf"

function Invoke-KeilBuild {
    param(
        [string]$Project,
        [string]$Target,
        [string]$Log,
        [ValidateSet("Build", "Rebuild")]
        [string]$Action = "Build"
    )

    if (-not (Test-Path -LiteralPath $Project)) {
        throw "Keil project not found: $Project"
    }
    if (Test-Path -LiteralPath $Log) {
        Remove-Item -LiteralPath $Log -Force
    }

    Write-Host ""
    Write-Host "$Action $Target" -ForegroundColor Cyan
    Write-Host "Project: $Project"

    $uvAction = if ($Action -eq "Rebuild") { "-r" } else { "-b" }
    $argumentLine = '{0} "{1}" -t "{2}" -j0 -o "{3}"' -f `
        $uvAction, $Project, $Target, $Log
    $process = Start-Process `
        -FilePath $uv4 `
        -ArgumentList $argumentLine `
        -Wait `
        -PassThru `
        -WindowStyle Hidden

    if (-not (Test-Path -LiteralPath $Log)) {
        throw "Keil did not create a build log for $Target. Exit code: $($process.ExitCode)"
    }

    $logLines = Get-Content -LiteralPath $Log -Encoding Default
    $logLines | ForEach-Object { Write-Host $_ }
    $logText = $logLines -join [Environment]::NewLine
    $matches = [regex]::Matches(
        $logText,
        ' - (?<errors>[0-9]+) Error\(s\), (?<warnings>[0-9]+) Warning\(s\)\.'
    )
    if ($matches.Count -eq 0) {
        throw "No Keil build summary was found for $Target."
    }

    $summary = $matches[$matches.Count - 1]
    $errors = [int]$summary.Groups["errors"].Value
    $warnings = [int]$summary.Groups["warnings"].Value
    if (($errors -ne 0) -or ($warnings -ne 0)) {
        throw "$Target failed with $errors error(s) and $warnings warning(s)."
    }
    Write-Host "$Target succeeded with $warnings warning(s)." -ForegroundColor Green
}

& (Join-Path $PSScriptRoot "check_environment.ps1") -Scope Build -Quiet

$buildKernel = ($Mode -eq "All") -or (-not (Test-Path -LiteralPath $kernelLibrary))
if (-not $buildKernel -and (Test-Path -LiteralPath $configHeader)) {
    $buildKernel = (Get-Item -LiteralPath $configHeader).LastWriteTimeUtc -gt `
        (Get-Item -LiteralPath $kernelLibrary).LastWriteTimeUtc
    if ($buildKernel) {
        Write-Host "FreeRTOSConfig.h changed; rebuilding the kernel library." -ForegroundColor Yellow
    }
}

if (-not $buildKernel -and (Test-Path -LiteralPath $kernelProject)) {
    $buildKernel = (Get-Item -LiteralPath $kernelProject).LastWriteTimeUtc -gt `
        (Get-Item -LiteralPath $kernelLibrary).LastWriteTimeUtc
    if ($buildKernel) {
        Write-Host "FreeRTOS project changed; rebuilding the kernel library." -ForegroundColor Yellow
    }
}

if ($buildKernel) {
    $kernelBuild = @{
        Project = $kernelProject
        Target = "freertos_ECHO"
        Log = Join-Path $projectRoot "freertos\keil\Objects\vscode_build.log"
        Action = "Rebuild"
    }
    Invoke-KeilBuild @kernelBuild
}

$rebuildApp = ($Mode -eq "All") -or $buildKernel -or (-not (Test-Path -LiteralPath $appImage))
if (-not $rebuildApp -and (Test-Path -LiteralPath $kernelLibrary)) {
    $rebuildApp = (Get-Item -LiteralPath $kernelLibrary).LastWriteTimeUtc -gt `
        (Get-Item -LiteralPath $appImage).LastWriteTimeUtc
    if ($rebuildApp) {
        Write-Host "FreeRTOS library changed; relinking the application." -ForegroundColor Yellow
    }
}

if (-not $rebuildApp -and (Test-Path -LiteralPath $appProject)) {
    $rebuildApp = (Get-Item -LiteralPath $appProject).LastWriteTimeUtc -gt `
        (Get-Item -LiteralPath $appImage).LastWriteTimeUtc
    if ($rebuildApp) {
        Write-Host "ECHO project changed; rebuilding the application." -ForegroundColor Yellow
    }
}

$appBuild = @{
    Project = $appProject
    Target = "ECHO"
    Log = Join-Path $projectRoot "keil\Objects\vscode_build.log"
    Action = if ($rebuildApp) { "Rebuild" } else { "Build" }
}
Invoke-KeilBuild @appBuild
