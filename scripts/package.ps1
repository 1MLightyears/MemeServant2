$ErrorActionPreference = "Stop"

# Allow toolchain.bat to be the one local machine config shared with build-msvc.bat.
function Import-ToolchainVariables {
    $toolchainFile = Join-Path $PSScriptRoot "toolchain.bat"
    if (-not (Test-Path $toolchainFile)) {
        return
    }
    foreach ($line in Get-Content -LiteralPath $toolchainFile) {
        if ($line -match '^\s*set\s+"([^=]+)=(.*)"\s*$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }
}
Import-ToolchainVariables

# Machine-specific locations are supplied by environment variables, not committed.
if (-not $env:MEMESERVANT2_QT_ROOT) {
    throw "MEMESERVANT2_QT_ROOT is not set. See scripts\toolchain.bat.example."
}
if (-not $env:MEMESERVANT2_MSVC_REDIST_ROOT) {
    throw "MEMESERVANT2_MSVC_REDIST_ROOT is not set. See scripts\toolchain.bat.example."
}

cmd /c (Join-Path $PSScriptRoot "build-msvc.bat")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# CMake writes this JSON from ProjectConfig.cmake during configure.
# Packaging therefore runs after configure/build and never repeats the version.
$metadataPath = Join-Path (Get-Location) "build\project_metadata.json"
if (-not (Test-Path $metadataPath)) {
    throw "Missing project metadata: $metadataPath"
}
$metadata = Get-Content $metadataPath -Raw | ConvertFrom-Json
$zipName = "MemeServant2-{0}-{1}.zip" -f $metadata.version, $metadata.packagePlatform

$deploy = Join-Path (Get-Location) "build\deploy"
$zip = Join-Path (Get-Location) $zipName
if (Test-Path $deploy) { Remove-Item -LiteralPath $deploy -Recurse -Force }
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
New-Item -ItemType Directory -Path $deploy | Out-Null
Copy-Item -Path "build\MemeServant2.exe" -Destination $deploy

if (Test-Path $env:MEMESERVANT2_MSVC_REDIST_ROOT) {
    Get-ChildItem -Path $env:MEMESERVANT2_MSVC_REDIST_ROOT -Filter "*.dll" | ForEach-Object {
        if ($_.Name -like "msvcp140*.dll" -or $_.Name -like "vcruntime140*.dll" -or $_.Name -like "concrt140*.dll") {
            Copy-Item -LiteralPath $_.FullName -Destination $deploy
        }
    }
}

$windeployqt = Join-Path $env:MEMESERVANT2_QT_ROOT "bin\windeployqt.exe"
& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-quick-import (Join-Path $deploy "MemeServant2.exe")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Compress-Archive -Path $deploy\* -DestinationPath $zip -Force
Write-Output "Package created: $zip"
