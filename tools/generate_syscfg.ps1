$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

$product = Join-Path $EchoPaths.SdkRoot ".metadata\product.json"
$inputFile = Join-Path $EchoPaths.ProjectRoot "config\ECHO.syscfg"
$outputDirectory = Join-Path $EchoPaths.ProjectRoot "platform\generated"

foreach ($requiredPath in @($EchoPaths.SysConfigCli, $product, $inputFile)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required SysConfig path was not found: $requiredPath"
    }
}

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
& $EchoPaths.SysConfigCli "-o" $outputDirectory "-s" $product "--compiler" "keil" $inputFile
if ($LASTEXITCODE -ne 0) {
    throw "SysConfig generation failed with exit code $LASTEXITCODE."
}
