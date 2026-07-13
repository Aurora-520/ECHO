$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$localConfig = Join-Path $projectRoot "config\local_paths.ps1"

if (-not (Test-Path -LiteralPath $localConfig)) {
    throw @"
Local toolchain configuration was not found:
  $localConfig

Copy config\local_paths.example.ps1 to config\local_paths.ps1,
then update the paths for this computer.
"@
}

$EchoLocalPaths = $null
. $localConfig

if (-not ($EchoLocalPaths -is [System.Collections.IDictionary])) {
    throw "local_paths.ps1 must define an EchoLocalPaths hashtable."
}

function Resolve-EchoConfiguredPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Key,

        [Parameter(Mandatory = $true)]
        [string]$EnvironmentVariable
    )

    $value = [Environment]::GetEnvironmentVariable($EnvironmentVariable)
    if ([string]::IsNullOrWhiteSpace($value)) {
        $value = [string]$EchoLocalPaths[$Key]
    }

    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "Missing path '$Key'. Set $EnvironmentVariable or update $localConfig."
    }

    return [System.IO.Path]::GetFullPath($value)
}

$EchoPaths = [pscustomobject]@{
    ProjectRoot  = $projectRoot
    LocalConfig  = $localConfig
    SdkRoot      = Resolve-EchoConfiguredPath "SdkRoot" "MSPM0_SDK_ROOT"
    KeilUv4      = Resolve-EchoConfiguredPath "KeilUv4" "ECHO_KEIL_UV4"
    SysConfigCli = Resolve-EchoConfiguredPath "SysConfigCli" "ECHO_SYSCONFIG_CLI"
    OpenOcdRoot  = Resolve-EchoConfiguredPath "OpenOcdRoot" "ECHO_OPENOCD_ROOT"
    GnuArmBin    = Resolve-EchoConfiguredPath "GnuArmBin" "ECHO_GNU_ARM_BIN"
    ArmClangBin  = Resolve-EchoConfiguredPath "ArmClangBin" "ECHO_ARMCLANG_BIN"
}
