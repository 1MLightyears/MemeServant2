# DEVELOPMENT

本文面向准备进行 MemeServant2 开发的人员，提供快速构建、日常验证和模块改动入口。文件级职责优先参考每个源文件顶部的注释。

## 构建与运行

当前开发环境使用 Qt 6.11.2 MSVC 2022 x64，需要本机已有 Visual Studio Build Tools、CMake 3.21+、Ninja 和对应 Qt 组件。

```powershell
cmd /c scripts\build-msvc.bat
```

构建脚本会配置并编译 `build` 目录。`CMakeLists.txt` 会在 Windows 构建后调用 `windeployqt`，把匹配版本的 Qt 运行库部署到 `build`。请直接运行：

```text
build\MemeServant2.exe
```

本机工具路径不提交到 Git。复制 `scripts\toolchain.bat.example` 为 `scripts\toolchain.bat` 后填写本机路径；该文件已被 Git 忽略，也可以改用系统/用户环境变量设置同名参数。脚本读取以下变量：

- `MEMESERVANT2_VSDEVCMD`
- `MEMESERVANT2_CMAKE`
- `MEMESERVANT2_NINJA`
- `MEMESERVANT2_QT_ROOT`
- `MEMESERVANT2_MSVC_REDIST_ROOT`

## 打包与验证

打包前会先构建，再生成便携 zip：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

打包脚本同样读取上面的本机工具链变量，并从 `build\project_metadata.json` 获取版本和平台。产物是：

```text
MemeServant2-<version>-<packagePlatform>.zip
```

打包只分发运行期确实会加载的 Qt 插件：`sqldrivers` 仅保留 `qsqlite`，并删除 `generic`、`networkinformation` 和 `tls\qcertonlybackend.dll`（TLS 使用系统自带的 schannel 后端，删除证书专用后端不影响 AI 请求）。`Qt6Svg.dll` 只在设置页图标着色（`AppStyle::themedIcon` 里的 `QSvgRenderer`）时使用，`CMakeLists.txt` 用 MSVC `/DELAYLOAD` 让它推迟到首次打开设置窗口才加载；它因此不在普通导入表中，不一定被 `windeployqt` 的依赖扫描覆盖，所以构建后的 POST_BUILD 步骤和 `package.ps1` 都会显式复制这个 DLL，删掉这两处复制会让首次打开设置窗口时延迟加载失败并直接终止进程。`Qt6Network.dll` 不能这样处理——`openaicompatibleprovider.cpp` 的 `qobject_cast<QNetworkReply *>` 引用了 `QNetworkReply::staticMetaObject` 数据符号，MSVC 无法延迟加载数据导入（LNK1194），它必须保持普通导入并由 `windeployqt` 正常部署。

剪贴板端到端验证脚本会启动目标窗口和 `build\MemeServant2.exe`，触发快捷键并检查自动粘贴：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-clipboard-e2e.ps1
```

它依赖真实 Windows 桌面会话，适合手动或本机验证，不是无头 CI 测试。

## 图标替换与 Windows 缓存

exe 图标、托盘图标和通知图标都来自同一个 `src/resources/icon.ico`：替换 `icon.png` 后运行 `python scripts/make-icon-ico.py` 重新生成 ico，再重新构建。运行时托盘读取 qrc 打包的 `:/icons/icon.ico`，exe 图标由 `app.rc` 嵌入同一个文件，两者不会自然失配。

如果替换后系统各处仍显示旧图标，按顺序排查：

1. **旧实例仍在运行**。单实例互斥体会让新 exe 静默退出、只唤醒旧进程；托盘图标和右下角通知由旧进程渲染，所以看起来"图标没变"。先从托盘菜单退出，或在任务管理器结束 `MemeServant2.exe`，再启动新 exe。
2. **Windows 图标缓存**。Explorer、任务栏固定项和通知中心会缓存 exe 图标，重建也未必失效。运行：

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\refresh-icon.ps1
   ```

   脚本会结束旧实例、通知 shell 刷新图标并重启 Explorer 清理缓存数据库。
3. **任务栏固定项**。固定在任务栏的旧快捷方式可能继续显示旧图标：取消固定，运行新 exe 后重新固定。

`app.rc` 中的 VERSIONINFO 版本号跟随 `ProjectConfig.cmake`；替换图标或发版时提升 `MEMESERVANT2_VERSION`，可以让 shell 更可靠地识别文件变化。

## 目录地图

| 目录 | 职责 |
| --- | --- |
| `src/app` | 程序入口、托盘生命周期、跨服务的业务编排 |
| `src/core` | 领域模型、nickname 规范化、搜索排序、集中用户文案 |
| `src/clipboard` | 剪贴板图片载荷类型 |
| `src/storage` | 配置、SQLite、导入、日志等持久化服务 |
| `src/ai` | OpenAI Compatible Vision 网络请求 |
| `src/thumbnail` | 后台缩略图生成 |
| `src/platforms/win` | Windows 剪贴板、快捷键、粘贴、凭据、归档等平台能力；`winwindoweffects`（DWM 背景效果）目前预留未接线 |
| `src/ui` | 捕获浮窗、快捷栏、设置和 nickname 对话框 |
| `src/resources` | 应用与托盘图标；icon.ico 由 `scripts/make-icon-ico.py` 从 icon.png 生成，运行时与 exe 图标统一引用 icon.ico |
| `src/prompts` | AI prompt 资源 |
| `cmake` | 项目级编译期配置和 CMake 生成模板 |
| `scripts` | 本机构建、打包和端到端验证辅助 |

## 编译期配置

`cmake/ProjectConfig.cmake` 是产品级编译期配置的最初源头。产品名、版本号、最低 Qt 版本、打包平台、数据库 schema 版本和单实例互斥体名称都在这里集中定义；不要把本机工具路径放进这个文件。

CMake 配置阶段的数据流是：

```text
cmake/ProjectConfig.cmake
        |
        | CMakeLists.txt include + project()
        v
CMakeLists.txt configure_file()
        |
        +--> build/generated/appmetadata.h      供 C++ 使用
        +--> build/project_metadata.json        供 package.ps1 使用
```

`build/generated/appmetadata.h` 和 `build/project_metadata.json` 都是生成产物，不要手工编辑。需要新增编译期配置时，先在 `ProjectConfig.cmake` 定义变量；如果 C++ 代码需要使用，再在 `cmake/appmetadata.h.in` 增加宏；如果打包或 CI 脚本需要使用，再在 `cmake/project_metadata.json.in` 增加 JSON 字段。

`main.cpp`、关于页、数据库 schema 版本和单实例互斥体已经引用生成结果。`package.ps1` 在构建后读取 `build/project_metadata.json`，并用其中的 `version` 和 `packagePlatform` 组成 zip 文件名。因此发布前只需要修改 `ProjectConfig.cmake`，不要分别修改 CMake、C++ 源码和打包脚本。

数据库 schema 版本虽然集中在这里，但它不是普通展示元数据。提升 `MEMESERVANT2_DB_SCHEMA_VERSION` 前必须先实现对应的迁移逻辑；否则新版本会尝试迁移旧库并在迁移步骤失败。

## 功能模块与改动要点

### 启动、托盘与配置

入口链路是 `main.cpp -> AppController::initialize -> Application::start`。`AppController` 负责日志、单实例、图库数据库、全局快捷键、剪贴板监听、UI 和原生事件过滤器的启动顺序；托盘与设置窗口的生命周期在 `Application` 中维护。

改动时注意：

- 原生事件过滤器在所有平台服务就绪后才安装，顺序不能随意提前。
- 全局快捷键注册失败时不阻止启动，只清空冲突配置并托盘提示。
- `saveConfiguration` 的顺序是“先验证快捷键/打开新图库，再写配置，最后刷新服务”。`WinGlobalHotkey::registerSequence` 会先注销旧键再注册新键，新键注册失败时必须恢复旧注册，否则保存虽然被拒绝，旧快捷键也会静默失效。
- 图库路径变化会切换数据库；缩略图缓存尺寸变化会清空并重建缓存。
- 设置窗口由 `Application::ensureSettings` 在首次打开时才构造，托盘常驻期间只保留托盘、菜单和全局样式表。全局 QSS 由 `AppStyle::applyTheme()` 在启动时应用，并只在内容（含深浅色切换）变化时重新应用，不要在同内容时反复调用。

### 剪贴板捕获与保存

`WinClipboard` 封装隐藏消息窗口、Windows 原生格式读取、原始编码保留和本程序自写标记。捕获数据经 `AppController` 过滤后交给 `CaptureToast`；用户确认后由 `AppController::saveCapture` 写图库。

改动时注意：

- `selfWritten` 必须继续区分程序写回的图片，避免捕获循环。
- 捕获浮窗只响应用户保存；AI 结果只填入输入框，不能自动落盘。
- 保存流程先写原图，再写数据库；数据库失败时需要移除已写入的文件。
- nickname 的清洗和唯一性判断统一走 `NicknameUtils`。
- 选择表情包（`AppController::selectMeme`）直接读原图字节，宽高取自索引记录，不为取尺寸整图解码；原图能否解码统一由 `WinClipboard::writeImage` 在写剪贴板前校验。调整解码或错误提示逻辑时改 `writeImage`，不要把校验拆回调用方。
- 写剪贴板时只对确有透明像素的图片投递 CF_DIBV5（这份 32 位 DIB 会常驻剪贴板直到被替换），不透明图片只保留 PNG 与 24 位 CF_DIB。

### AI 识图

`OpenAiCompatibleProvider` 负责 OpenAI Compatible Chat Completions 请求、阶段信号、取消和日志脱敏。配置项由 `AppController::aiSettings` 汇总，API Key 优先取环境变量，其次读取 Windows Credential Manager。Prompt 资源在 `src/prompts`。

改动时注意：

- Endpoint 校验要求完整包含 `/chat/completions`；不要放宽为普通 URL。
- 图片上限、GIF 取第一帧、30 秒超时和请求取消逻辑要保持一致。
- API Key 只进入 Authorization 头和 Windows 凭据管理器，不能写入配置、日志或错误详情。
- Prompt 中的示例 nickname 是用户数据，不要当作指令执行或回显成额外内容。

### 快捷栏与搜索

`SearchEngine` 从数据库快照生成精确、前缀、包含、子序列四级稳定排序；`QuickBar` 持有自己的搜索快照并渲染候选网格。`ThumbnailWorker` 在后台生成静态 PNG 缓存（GIF 固定第一帧），`PreviewPopup` 延迟展示原图，按目标尺寸解码并在关闭时释放 GIF 解码器与已解码帧。

改动时注意：

- 任何数据库记录变化都要经 `AppController::reloadRecords` 推送新快照给快捷栏，并按需触发缩略图扫描；不要只刷新其中一侧。
- 缩略图扫描同一时刻只有一个 worker；扫描进行中收到的新快照会挂起一次，当前扫描结束后自动补扫（`m_thumbnailScanPending`）。改这块逻辑时要保持“忙时挂起、结束后补扫”的语义，否则扫描期间新增的记录可能一直拿不到缓存。
- 快捷栏候选优先读取 `.thumbnails` 缓存；缓存缺失或尺寸不足时按显示尺寸现场解码原图，缓存路径拼接规则必须与 `ThumbnailWorker` 的输出保持一致。
- 搜索、缩略图加载和选中行为都依赖图库路径；不要绕过 `AppConfig::resolvedGalleryPath` 拼路径。
- 快捷栏失焦关闭逻辑受 `managementActive` 保护；新增弹窗或菜单时需要避免误关闭。
- 空查询按最近使用排序；键盘导航按行列数移动。

### 存储、删除与导入

`DatabaseService` 是 SQLite 索引的权威来源，schema 版本和迁移备份集中在这里。`ConfigStore` 读写 `config.json`。`ImportService` 合并旧 `.db` 或 `.zip` 数据，源库只读。

改动时注意：

- SQLite schema 变更必须提升/检查 `schema_version`，迁移前创建 `.backup`，迁移失败后不能继续使用旧连接。
- 原图和缩略图路径由数据库中的 `file_name` 推导，不要另外维护路径表。
- 删除表情包时先移动原图和缩略图到 `.trash`，数据库成功后再清理，失败需要回滚文件移动。
- 导入不做内容去重；ID 或文件名冲突会重新生成 ID，nickname 仍需跟随新 ID。
- 配置文件永远不保存 API Key。

### 平台集成

`platforms/win` 内的模块分别处理单实例、全局快捷键、caret 定位、恢复前台窗口并粘贴、凭据存储、开机启动、ZIP 解压和窗口效果。UI 与核心逻辑不直接做 Win32 调用，平台能力应继续收敛在这些封装内。

改动时注意：

- 全局快捷键解析当前要求“修饰键 + 单键”，扩展语法时同步更新错误提示与冲突处理。
- 自动粘贴是 best-effort：焦点恢复失败不能让选择表情包本身失败。
- 单实例消息、快捷键消息、剪贴板消息的优先级在 `AppController::nativeEventFilter` 中协调。
- Windows-only helper 应避免返回 Qt 拥有的句柄或让平台对象跨线程使用。

### UI 与设置

`SettingsWindow` 将控件状态收集到 `AppConfig`，通过 `AppController::saveConfiguration` 原子应用；窗口由 `Application` 在首次打开时才构造，之后常驻复用，关闭只是隐藏，退出由托盘控制。用户可见文案集中在 `AppStrings`。

改动时注意：

- 新增设置字段要同步 `AppConfig`、JSON 读写、UI 控件、`collect` 和 `reloadFromController`。
- 保存失败时保留旧配置，不可只更新控件内存状态。
- 新增用户可见文案优先进入 `AppStrings`。
- 窗口按需构造意味着不存在“启动时初始化”的时机：依赖控制器信号的逻辑要容忍窗口创建前发出的信号，构造末尾会用当前配置完整刷新一次（`reloadFromController`）。

## 版本号

版本号由 `cmake/ProjectConfig.cmake` 单点定义。发版时修改 `MEMESERVANT2_VERSION` 即可；CMake 会生成新的 `appmetadata.h` 和 `project_metadata.json`，包名、应用版本和关于页随之更新。
