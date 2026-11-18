# DEVELOPMENT

本文面向准备进行 MemeServant2 开发的人员，提供快速构建、日常验证和模块改动入口。文件级职责优先参考每个源文件顶部的注释。

> 术语约定：面向用户的名称统一为“标签”；源码中的 `nickname` 标识符、`nicknames` 数据库表名和 `aiNicknameExampleCount` 配置键属于内部历史命名，为兼容现有图库与配置而保留。

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
| `src/core` | 领域模型、标签规范化、搜索排序、集中用户文案 |
| `src/clipboard` | 剪贴板图片载荷类型，以及捕获路径使用的图片字节嗅探工具 |
| `src/storage` | 配置、SQLite、导入、日志等持久化服务 |
| `src/ai` | OpenAI Compatible Vision 网络请求，以及 GIF 多帧拼接的独立处理路线 |
| `src/thumbnail` | 后台缩略图生成 |
| `src/platforms/win` | Windows 剪贴板、快捷键、粘贴、凭据、归档等平台能力；`winwindoweffects`（DWM 背景效果）目前预留未接线 |
| `src/ui` | 捕获浮窗、快捷栏、设置和标签对话框 |
| `src/resources` | 应用与托盘图标；icon.ico 由 `scripts/make-icon-ico.py` 从 icon.png 生成，运行时与 exe 图标统一引用 icon.ico |
| `src/prompts` | AI prompt 资源：普通识图和 GIF 兼容模式各一份 |
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
- 单实例消息、快捷键消息、剪贴板消息的优先级在 `AppController::nativeEventFilter` 中协调；调试快捷键 `Alt+D` 只在注册后才会收到 `WM_HOTKEY`，关闭调试模式时保持未注册，按键继续交给前台程序。
- `saveConfiguration` 的顺序是“先验证快捷键/打开新图库，再写配置，最后刷新服务”。`WinGlobalHotkey::registerSequence` 会先注销旧键再注册新键，新键注册失败时必须恢复旧注册，否则保存虽然被拒绝，旧快捷键也会静默失效。
- 图库路径变化会切换数据库；缩略图缓存尺寸变化会清空并重建缓存。
- 设置窗口由 `Application::ensureSettings` 在首次打开时才构造，托盘常驻期间只保留托盘、菜单和全局样式表。全局 QSS 由 `AppStyle::applyTheme()` 在启动时应用，并只在内容（含深浅色切换）变化时重新应用，不要在同内容时反复调用。

### 剪贴板捕获与保存

`WinClipboard` 封装隐藏消息窗口、Windows 原生格式读取、原始编码保留和本程序自写标记。捕获数据经 `AppController` 过滤后交给 `CaptureToast`；用户确认后由 `AppController::saveCapture` 写图库。

改动时注意：

- `selfWritten` 必须继续区分程序写回的图片，避免捕获循环。
- 读取路径的优先级是“剪贴板引用的原始文件 > HTML/Markdown 内嵌 GIF > 注册编码格式 > DIB”。QQ/微信复制消息或表情时会写 `QQ_Unicode_RichEdit_Format`（旧版为 `QQ_RichEdit_Format`）和 `HTML Format`，其中的 `filepath`/`file://` 指向原始图片文件；`CF_HDROP` 也常直接给出文件路径。这些引用比剪贴板里的静态重编码格式更保真，改动时不要为了省事把它们重新忽略掉。
- 读到的引用文件统一经 `ImageBytes::formatOf` 按魔数识别，先按路径去重、再按内容 SHA-256 去重，然后按“唯一候选 → 唯一多帧动图 → 唯一单帧 GIF → 最高优先级来源恰好一个候选”的顺序选择；同一来源仍有多张不同图片时宁可放弃也不猜，避免多选文件后存错图。内容去重是必要的：QQ/微信会把同一张图通过 FileDrop、富文本和 HTML 以不同临时路径重复发布，不去重会被“多张图片”保护规则误判并退化成静态首帧。来源优先级为富文本 > HTML/Markdown > 文件列表（`CF_HDROP` 常是转码后的临时副本），但它只在前几步都无法唯一确定时兜底。引用的文件可能是加密缓存或已删除的临时文件，读取失败要静默回退到剪贴板编码/位图；`ImageBytes::gifFrameCount` 直接扫描 GIF 块结构数帧，不依赖图片插件的 `imageCount()`。
- 剪贴板 HTML/Markdown 中的 `http(s)` URL 只作为文本忽略，捕获流程不下载任何网络资源：仅凭 URL 后缀无法确认内容真的是图片，主动下载会引入 SSRF/恶意内容风险。CF_HTML 只用于提取 `file://` 本地路径（QQ/微信富文本）和内嵌 `data:` 图片。
- 读取路径按编码格式族依次尝试注册名（GIF、PNG、JPEG 各自有多个约定名，归族后共用同一套处理逻辑）；仅当 CF_DIBV5 是剪贴板原生格式时才直接读它，否则读 CF_DIB，避免系统合成的额外转换和透明度丢失。原生性用 `EnumClipboardFormats` 的枚举顺序判断（系统把可转换格式追加在末尾，与 Qt 的做法一致）。
- 没有 alpha 掩码的 32 位 DIB（BITMAPINFOHEADER 的 BI_RGB 或三掩码 BI_BITFIELDS）按不透明处理；DIBV5 声明了 AlphaMask 但数据 alpha 全为 0 时按不透明兜底，避免存出全透明图片。
- `OpenClipboard` 期间只做格式判断和字节拷贝，解码、DIB 转换和 PNG 编码都在关闭剪贴板后进行，避免长时间占用全局剪贴板锁。
- 排查剪贴板来源时可用环境变量 `MEMESERVANT2_CLIPBOARD_DEBUG=1` 启动程序，或在设置中开启调试模式：日志会输出剪贴板格式名清单、每个引用来源及其本地路径、候选图片的格式/尺寸/帧数/字节数，以及最终选择的候选或回退路径。调试模式会记录完整文件路径，只应在需要诊断时临时开启。
- 捕获浮窗只响应用户保存；AI 结果只填入输入框，不能自动落盘。
- 保存流程先写原图，再写数据库；数据库失败时需要移除已写入的文件。
- 标签的清洗和唯一性判断统一走 `NicknameUtils`。
- 选择表情包（`AppController::selectMeme`）直接读原图字节，宽高取自索引记录，不为取尺寸整图解码；原图能否解码统一由 `WinClipboard::writeImage` 在写剪贴板前校验。调整解码或错误提示逻辑时改 `writeImage`，不要把校验拆回调用方。
- 写剪贴板时只对确有透明像素的图片投递 CF_DIBV5（这份 32 位 DIB 会常驻剪贴板直到被替换），不透明图片只保留 PNG 与 24 位 CF_DIB。

### 调试模式与日志

常规日志由 `LogService` 写入程序目录 `logs/memeservant2.log` 并做 5MB 轮转。设置中的“调试模式”开启后，同一批日志行会再追加到程序根目录的调试日志文件（`AppConfig::debugLogFileName`，默认 `debug.log`），文件不轮转、只追加。调试模式还会打开 `WinClipboard` 的详细诊断输出（格式清单、引用来源与路径、候选图片详情、回退路径），并注册 `Alt+D` 全局快捷键：`AppController::logClipboardDiagnostics` 调用 `WinClipboard::debugSnapshot` 读取一次当前剪贴板，把格式清单、捕获读取结果和“为什么没有激活捕获浮窗”的判定写入调试日志。

改动时注意：

- `Alt+D` 只在调试模式开启时通过 `RegisterHotKey` 注册，关闭时立即 `UnregisterHotKey`。全局热键一旦注册就会被系统消费，收到 `WM_HOTKEY` 后无法“继续冒泡”；不要改成常驻注册再用 `SendInput` 转发，那会变成合成输入，可能重复触发或被安全软件拦截。条件注册才能保证关闭调试模式时该组合键完全交还给前台程序。
- 调试日志文件名只接受纯文件名并固定 `.log` 后缀，由 `AppConfig::resolvedDebugLogFileName` 清洗；这是为了防止把 `config.json`、数据库或 exe 当作日志追加写入。根目录不可写时只在常规日志里记录警告，不阻塞启动和调试模式开关。
- 调试模式为了定位 QQ/微信缓存会记录剪贴板引用文件的本地路径，但不要记录标签正文、图片数据或密钥；常规日志（未开调试模式）不应输出引用路径。
- 设置页保存采用防抖自动保存，调试开关和文件名变化由 `AppController::saveConfiguration` 比较旧配置后调用 `applyDebugSettings`，不要只在 UI 控件回调里改状态。

### AI 识图

`OpenAiCompatibleProvider` 负责 OpenAI Compatible Chat Completions 请求、阶段信号、取消和日志脱敏。配置项由 `AppController::aiSettings` 汇总，API Key 优先取环境变量，其次读取 Windows Credential Manager。Prompt 资源在 `src/prompts`：普通识图使用 `vision_nickname.md`，GIF 兼容模式使用独立的 `vision_nickname_gif.md`。

改动时注意：

- Endpoint 校验要求完整包含 `/chat/completions`；不要放宽为普通 URL。
- GIF 兼容模式是独立处理路线，实现集中在 `src/ai/gifcompat.*`，provider 只消费 `GifCompat::Composite` 结果；不要把帧采样、拼接逻辑拆回 provider。算法是：先扫描每帧编码后大小，按 `GifCompat::kMaxCompositeBytes`（8 * 1024 * 1024）和画布内存上限从多到少反推可容纳的最大帧数，再按帧序号均匀采样（包含头尾，例如 10 帧取 4 帧就是第 0/3/6/9 帧）；不处理帧延迟。只有头尾两帧仍超限时返回空，provider 回退首帧。拼接结果长边不超过 2048，PNG 仍超限时继续缩小。
- 图片上限、GIF 首帧、30 秒超时和请求取消逻辑要保持一致：普通路线取首帧转 PNG，兼容模式使用拼接长图，两条路线都必须落在 `kMaxImageBytes` 校验内，且拼接失败不能导致 AI 请求直接失败。
- API Key 只进入 Authorization 头和 Windows 凭据管理器，不能写入配置、日志或错误详情。
- Prompt 中的示例标签是用户数据，不要当作指令执行或回显成额外内容。

### 快捷栏与搜索

`SearchEngine` 从数据库快照生成精确、前缀、包含、子序列四级稳定排序；`QuickBar` 持有自己的搜索快照并渲染候选网格。`ThumbnailWorker` 在后台生成静态 PNG 缓存（GIF 固定第一帧），`PreviewPopup` 延迟展示原图，按目标尺寸解码并在关闭时释放 GIF 解码器与已解码帧。

改动时注意：

- 任何数据库记录变化都要经 `AppController::reloadRecords` 推送新快照给快捷栏，并按需触发缩略图扫描；不要只刷新其中一侧。
- 缩略图扫描同一时刻只有一个 worker；扫描进行中收到的新快照会挂起一次，当前扫描结束后自动补扫（`m_thumbnailScanPending`）。改这块逻辑时要保持“忙时挂起、结束后补扫”的语义，否则扫描期间新增的记录可能一直拿不到缓存。
- 快捷栏候选优先读取 `.thumbnails` 缓存；缓存缺失或尺寸不足时按显示尺寸现场解码原图，缓存路径拼接规则必须与 `ThumbnailWorker` 的输出保持一致。
- GIF 捕获后以 `.gif` 原图落盘：缩略图缓存固定取第一帧，悬停预览依赖原图路径的 `.gif` 后缀走 `QMovie` 播放动画。改动缩略图生成或预览后缀判断时，不能把这两个行为弄丢。
- 搜索、缩略图加载和选中行为都依赖图库路径；不要绕过 `AppConfig::resolvedGalleryPath` 拼路径。
- 快捷栏失焦关闭逻辑受 `managementActive` 保护；新增弹窗或菜单时需要避免误关闭。
- 空查询按最近使用排序；键盘导航按行列数移动。

### 存储、删除与导入

`DatabaseService` 是 SQLite 索引的权威来源，schema 版本和迁移备份集中在这里。`ConfigStore` 读写 `config.json`。`ImportService` 合并旧 `.db` 或 `.zip` 数据，源库只读。

改动时注意：

- SQLite schema 变更必须提升/检查 `schema_version`，迁移前创建 `.backup`，迁移失败后不能继续使用旧连接。
- 原图和缩略图路径由数据库中的 `file_name` 推导，不要另外维护路径表。
- 删除表情包时先移动原图和缩略图到 `.trash`，数据库成功后再清理，失败需要回滚文件移动。
- 导入不做内容去重；ID 或文件名冲突会重新生成 ID，标签仍需跟随新 ID。
- 配置文件永远不保存 API Key。

### 平台集成

`platforms/win` 内的模块分别处理单实例、全局快捷键、caret 定位、恢复前台窗口并粘贴、凭据存储、开机启动、ZIP 解压和窗口效果。UI 与核心逻辑不直接做 Win32 调用，平台能力应继续收敛在这些封装内。

改动时注意：

- 全局快捷键解析当前要求“修饰键 + 单键”，扩展语法时同步更新错误提示与冲突处理。注册时把 `QKeySequence` 的按键值直接当作 Win32 虚拟键码使用，因此只适合字母、数字这类键值一致的按键；标点和 F 键需要额外换算，调试快捷键因此选用 `Alt+D` 而不是标点组合。
- 自动粘贴是 best-effort：焦点恢复失败不能让选择表情包本身失败。
- 单实例消息、快捷键消息、剪贴板消息的优先级在 `AppController::nativeEventFilter` 中协调；同一进程可以注册多个全局热键，但每个 `WinGlobalHotkey` 必须使用不同的热键 ID。
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
