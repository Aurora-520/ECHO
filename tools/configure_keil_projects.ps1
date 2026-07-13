param(
    [string]$SdkRoot
)

$ErrorActionPreference = "Stop"
if (-not [string]::IsNullOrWhiteSpace($SdkRoot)) {
    $env:MSPM0_SDK_ROOT = [System.IO.Path]::GetFullPath($SdkRoot)
}
& (Join-Path $PSScriptRoot "sync_local_paths.ps1")
