$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

function Convert-ToForwardSlash {
    param([string]$Path)
    return $Path.Replace("\", "/")
}

function Join-SdkPath {
    param([string]$RelativePath)
    return [System.IO.Path]::GetFullPath((Join-Path $EchoPaths.SdkRoot $RelativePath))
}

function Save-XmlDocument {
    param(
        [xml]$Document,
        [string]$Path
    )

    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $writer = [System.Xml.XmlWriter]::Create($Path, $settings)
    try {
        $Document.Save($writer)
    }
    finally {
        $writer.Close()
    }
}

function Save-JsonDocument {
    param(
        [object]$Document,
        [string]$Path
    )

    $json = $Document | ConvertTo-Json -Depth 20
    [System.IO.File]::WriteAllText(
        $Path,
        $json + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false))
    )
}

$appProject = Join-Path $EchoPaths.ProjectRoot "keil\ECHO.uvprojx"
$kernelProject = Join-Path $EchoPaths.ProjectRoot "freertos\keil\freertos_ECHO.uvprojx"

[xml]$app = Get-Content -LiteralPath $appProject -Raw
$appTarget = $app.Project.Targets.Target
$appTarget.TargetOption.TargetArmAds.Cads.VariousControls.IncludePath = @(
    "..\app"
    "..\app\tasks"
    "..\app\ui"
    "..\bsp\include"
    "..\module\device"
    "..\module\service"
    "..\config"
    "..\platform\freertos"
    "..\platform\generated"
    "..\freertos"
    (Join-SdkPath "source\third_party\CMSIS\Core\Include")
    (Join-SdkPath "kernel\freertos\Source\include")
    (Join-SdkPath "source")
    (Join-SdkPath "kernel\freertos\Source\portable\GCC\ARM_CM0")
) -join ";"
$appTarget.TargetOption.TargetArmAds.LDads.Misc = @(
    (Join-SdkPath "source\ti\driverlib\lib\keil\m0p\mspm0g1x0x_g3x0x\driverlib.a")
    "..\freertos\keil\Objects\freertos_ECHO.lib"
) -join " "
Save-XmlDocument $app $appProject

[xml]$kernel = Get-Content -LiteralPath $kernelProject -Raw
$kernelTarget = $kernel.Project.Targets.Target
$kernelTarget.TargetOption.TargetArmAds.Cads.VariousControls.IncludePath = @(
    (Join-SdkPath "source")
    (Join-SdkPath "source\third_party\CMSIS\Core\Include")
    (Join-SdkPath "kernel\freertos\Source\include")
    (Join-SdkPath "kernel\freertos\Source\portable\GCC\ARM_CM0")
    ".."
) -join ";"

$kernelCoreFiles = @(
    "croutine.c"
    "event_groups.c"
    "list.c"
    "queue.c"
    "stream_buffer.c"
    "tasks.c"
    "timers.c"
)
$dplFiles = @(
    "ClockP_freertos.c"
    "DebugP_freertos.c"
    "MutexP_freertos.c"
    "SemaphoreP_freertos.c"
    "SystemP_freertos.c"
    "TaskP_freertos.c"
    "HwiPMSPM0_freertos.c"
)

foreach ($file in $kernelTarget.Groups.Group.Files.File) {
    if ($file.FileName -eq "FreeRTOSConfig.h") {
        $file.FilePath = "..\FreeRTOSConfig.h"
    }
    elseif ($kernelCoreFiles -contains $file.FileName) {
        $file.FilePath = Join-SdkPath ("kernel\freertos\Source\" + $file.FileName)
    }
    elseif ($file.FileName -eq "heap_4.c") {
        $file.FilePath = Join-SdkPath "kernel\freertos\Source\portable\MemMang\heap_4.c"
    }
    elseif ($file.FileName -in @("port.c", "portasm.c")) {
        $file.FilePath = Join-SdkPath ("kernel\freertos\Source\portable\GCC\ARM_CM0\" + $file.FileName)
    }
    elseif ($dplFiles -contains $file.FileName) {
        $file.FilePath = Join-SdkPath ("kernel\freertos\dpl\" + $file.FileName)
    }
    elseif ($file.FileName -eq "startup_mspm0g350x_uvision.s") {
        $file.FilePath = Join-SdkPath "source\ti\devices\msp\m0p\startup_system_files\keil\startup_mspm0g350x_uvision.s"
    }
}
Save-XmlDocument $kernel $kernelProject

$launchPath = Join-Path $EchoPaths.ProjectRoot ".vscode\launch.json"
$launch = [System.IO.File]::ReadAllText($launchPath) | ConvertFrom-Json
foreach ($configuration in $launch.configurations) {
    $configuration.armToolchainPath = Convert-ToForwardSlash $EchoPaths.GnuArmBin
    if ($configuration.servertype -eq "openocd") {
        $configuration.openOCDPath = Convert-ToForwardSlash (Join-Path $EchoPaths.OpenOcdRoot "bin\openocd.exe")
        $configuration.searchDir = @(
            (Convert-ToForwardSlash (Join-Path $EchoPaths.OpenOcdRoot "openocd\scripts"))
        )
    }
}
Save-JsonDocument $launch $launchPath

$cppPath = Join-Path $EchoPaths.ProjectRoot ".vscode\c_cpp_properties.json"
$cpp = [System.IO.File]::ReadAllText($cppPath) | ConvertFrom-Json
$cppConfiguration = $cpp.configurations[0]
$cppConfiguration.compilerPath = Convert-ToForwardSlash (Join-Path $EchoPaths.ArmClangBin "armclang.exe")
$cppConfiguration.cStandard = "c99"
$cppConfiguration.includePath = @(
    '${workspaceFolder}/app'
    '${workspaceFolder}/app/tasks'
    '${workspaceFolder}/app/ui'
    '${workspaceFolder}/bsp/include'
    '${workspaceFolder}/module/device'
    '${workspaceFolder}/module/service'
    '${workspaceFolder}/platform/freertos'
    '${workspaceFolder}/config'
    '${workspaceFolder}/platform/generated'
    '${workspaceFolder}/freertos'
    (Convert-ToForwardSlash (Join-SdkPath "source"))
    (Convert-ToForwardSlash (Join-SdkPath "source\third_party\CMSIS\Core\Include"))
    (Convert-ToForwardSlash (Join-SdkPath "kernel\freertos\Source\include"))
    (Convert-ToForwardSlash (Join-SdkPath "kernel\freertos\Source\portable\GCC\ARM_CM0"))
)
Save-JsonDocument $cpp $cppPath

$workspacePath = Join-Path $EchoPaths.ProjectRoot "ECHO.uvmpw"
[xml]$workspace = Get-Content -LiteralPath $workspacePath -Raw
$workspace.ProjectWorkspace.WorkspaceName = [string]$workspacePath
Save-XmlDocument $workspace $workspacePath

Write-Host "Local paths synchronized for: $($EchoPaths.ProjectRoot)" -ForegroundColor Green
Write-Host "SDK: $($EchoPaths.SdkRoot)"
