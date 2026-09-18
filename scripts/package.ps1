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

# Qt6Svg 在 CMake 里声明为延迟加载，不在普通导入表中，windeployqt 不会复制它；这里
# 显式补一份，保证首次打开设置窗口重绘图标时能加载成功。Qt6Network 保持普通导入，
# windeployqt 会正常复制，不需要单独处理。
foreach ($delayedModule in @("Qt6Svg.dll")) {
    $modulePath = Join-Path $env:MEMESERVANT2_QT_ROOT "bin\$delayedModule"
    if (Test-Path $modulePath) {
        Copy-Item -LiteralPath $modulePath -Destination $deploy -Force
    }
}

# 只保留运行期真正会加载的插件。qsqlite 是唯一用到的数据库驱动；TUIO 触控、网络状态
# 后端和证书专用 TLS 后端都不会被这个程序加载，分发它们只会白占体积。
$sqlDrivers = Join-Path $deploy "sqldrivers"
if (Test-Path $sqlDrivers) {
    Get-ChildItem -LiteralPath $sqlDrivers -Filter "*.dll" |
        Where-Object { $_.Name -ne "qsqlite.dll" } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
}
foreach ($unusedPluginDir in @("generic", "networkinformation")) {
    $unusedPluginPath = Join-Path $deploy $unusedPluginDir
    if (Test-Path $unusedPluginPath) {
        Remove-Item -LiteralPath $unusedPluginPath -Recurse -Force
    }
}
$certOnlyBackend = Join-Path $deploy "tls\qcertonlybackend.dll"
if (Test-Path $certOnlyBackend) {
    Remove-Item -LiteralPath $certOnlyBackend -Force
}

Compress-Archive -Path $deploy\* -DestinationPath $zip -Force
Write-Output "Package created: $zip"
