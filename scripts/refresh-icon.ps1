# 图标替换后的统一清理：结束旧实例并刷新 Windows 图标缓存。
#
# 背景：
# 1. 单实例互斥体（Local\MemeServant2-SingleInstance）会让新 exe 静默退出，
#    只唤醒旧进程；托盘图标和右下角通知由旧进程渲染，因此始终是旧图标。
# 2. Explorer、任务栏固定项和通知中心会缓存 exe 图标，即使 exe 已更新也常不刷新。
#
# 用法：powershell -ExecutionPolicy Bypass -File scripts\refresh-icon.ps1
$ErrorActionPreference = "Continue"

Write-Host "1/3 结束正在运行的 MemeServant2 实例..."
Get-Process -Name "MemeServant2" -ErrorAction SilentlyContinue | Stop-Process -Force

Write-Host "2/3 通知 shell 刷新图标缓存..."
ie4uinit.exe -show

Write-Host "3/3 重启 Explorer 以清理图标缓存数据库（任务栏会短暂消失再自动恢复）..."
Stop-Process -Name "explorer" -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800
$explorerCache = Join-Path $env:LOCALAPPDATA "Microsoft\Windows\Explorer"
Remove-Item -Path (Join-Path $explorerCache "iconcache_*.db") -Force -ErrorAction SilentlyContinue
Remove-Item -Path (Join-Path $env:LOCALAPPDATA "IconCache.db") -Force -ErrorAction SilentlyContinue
Start-Process "explorer.exe"

Write-Host "完成。请重新运行 MemeServant2.exe 检查 exe 图标、托盘图标与通知图标。"
Write-Host "若任务栏固定过旧图标，请先取消固定，运行新 exe 后再重新固定。"