$ErrorActionPreference = "Stop"

cmd /c (Join-Path $PSScriptRoot "build-msvc.bat")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$deploy = Join-Path (Get-Location) "build\deploy"
$zip = Join-Path (Get-Location) "MemeServant2-0.1.0-windows-x64.zip"
if (Test-Path $deploy) { Remove-Item -LiteralPath $deploy -Recurse -Force }
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
New-Item -ItemType Directory -Path $deploy | Out-Null
Copy-Item -Path "build\MemeServant2.exe" -Destination $deploy

$crtDirectory = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.42.34433\x64\Microsoft.VC143.CRT"
if (Test-Path $crtDirectory) {
    Get-ChildItem -Path $crtDirectory -Filter "*.dll" | ForEach-Object {
        if ($_.Name -like "msvcp140*.dll" -or $_.Name -like "vcruntime140*.dll" -or $_.Name -like "concrt140*.dll") {
            Copy-Item -LiteralPath $_.FullName -Destination $deploy
        }
    }
}

& "D:\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-quick-import (Join-Path $deploy "MemeServant2.exe")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Compress-Archive -Path $deploy\* -DestinationPath $zip -Force
Write-Output "Package created: $zip"
