# MemeServant2

MemeServant2 是一个常驻 Windows 桌面的表情包管理工具。首版支持剪贴板图片捕获、nickname 保存与模糊搜索、原图写回系统剪贴板、自动粘贴、缩略图缓存、AI 识图和数据导入。

## 开发构建

当前开发环境使用 Qt 6.11.2 MSVC 2022 x64：

```powershell
cmd /c scripts\build-msvc.bat
```

构建完成后会自动把匹配的 Qt 6.11.2 运行库部署到 `build` 目录；请运行 `build\MemeServant2.exe`，不要手动把其他 Qt 或 Conda 目录加入优先搜索路径。

## 便携包

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

产物为 `MemeServant2-0.1.0-windows-x64.zip`。
