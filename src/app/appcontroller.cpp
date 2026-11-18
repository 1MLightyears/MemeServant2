// 应用启动、退出、竞态保护和核心业务流转集中在这里处理。
#include "app/appcontroller.h"
#include "core/appstrings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QBuffer>
#include <QImageReader>
#include <QPair>
#include <QTimer>
#include <QScopeGuard>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include "app/appcontroller.h"
#include "platforms/win/winclipboard.h"
#include "platforms/win/winglobalhotkey.h"
#include "platforms/win/winsingleinstance.h"
#include "platforms/win/wincaretlocator.h"
#include "platforms/win/winpaste.h"
#include "platforms/win/wincredentialstore.h"
#include "platforms/win/winautostart.h"
#include "storage/importservice.h"
#include "storage/logservice.h"
#include "thumbnail/thumbnaillistener.h"
#include "ui/capturetoast.h"
#include "ui/quickbar.h"

namespace {
constexpr char kApiKeyTarget[] = "AiApiKey";
// 同一进程可注册多个全局快捷键，每个消息窗口使用各自的 ID。
constexpr int kQuickbarHotkeyId = 0x4D53;
constexpr int kDebugHotkeyId = 0x4D54;
// 调试模式专用：按一次读取一次当前剪贴板并写入调试日志。
constexpr char kDebugClipboardHotkey[] = "Alt+D";
const int THUMBNAIL_THREAD_TIMEOUT = 10000;
}

// 初始化配置存储；其余服务在 initialize 中按依赖顺序创建。
AppController::AppController(QObject *parent)
    : QObject(parent), m_configStore(QCoreApplication::applicationDirPath())
{
}

// 先请求缩略图线程停止并等待结束，再关闭仍存活的浮窗。
AppController::~AppController()
{
    if (m_thumbnailThread) {
        m_thumbnailThread->requestInterruption();
        m_thumbnailThread->quit();
        m_thumbnailThread->wait(THUMBNAIL_THREAD_TIMEOUT);
    }
    if (m_captureToast)
        m_captureToast->cancel();
    if (m_quickBar)
        m_quickBar->close();
}

// 依次让单实例、全局快捷键和剪贴板服务消费原生消息。
bool AppController::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
    MSG *windowsMessage = static_cast<MSG *>(message);
    if (!windowsMessage)
        return false;
    if (m_singleInstance && m_singleInstance->processMessage(windowsMessage))
        return true;
    if (m_hotkey && m_hotkey->processHotkey(windowsMessage)) {
        emit quickbarRequested();
        return true;
    }
    // Alt+D 只在调试模式开启时注册：关闭调试后系统不会再向本进程投递该热键，
    // 组合键会继续由前台程序正常处理，而不是被本程序消费后再丢弃。
    if (m_debugHotkey && m_debugHotkey->processHotkey(windowsMessage)) {
        logClipboardDiagnostics();
        return true;
    }
    return m_clipboard && m_clipboard->processMessage(windowsMessage) && false;
}

// 按日志、单实例、数据库、快捷键、剪贴板、UI 的顺序启动所有服务。
bool AppController::initialize(QString *error)
{
    // 先加载日志和配置，之后所有失败路径都能留下诊断信息。
    LogService::instance().initialize(QCoreApplication::applicationDirPath() + QStringLiteral("/logs"));
    m_config = m_configStore.load();
    // 调试模式在启动阶段就生效，让接下来的启动日志也进入同一个调试文件。
    applyDebugSettings();

    // 单实例失败时通知已有进程并让当前进程退出，不继续打开数据库。
    m_singleInstance = new WinSingleInstance(this);
    connect(m_singleInstance, &WinSingleInstance::activateRequested, this, &AppController::settingsRequested);
    if (!m_singleInstance->becomePrimary()) {
        WinSingleInstance::notifyExisting();
        if (error)
            *error = QStringLiteral("MemeServant2已在运行。");
        return false;
    }

    const QString gallery = m_config.resolvedGalleryPath(QCoreApplication::applicationDirPath());
    m_config.galleryPath = gallery;
    // 数据库是后续搜索、捕获保存和导入操作的前置依赖。
    if (!m_database.open(gallery, error)) {
        LogService::instance().error(QStringLiteral("数据库初始化失败"));
        emit databaseErrorOccurred(error ? *error : QStringLiteral("数据库无法打开。"));
        return false;
    }

    m_hotkey = new WinGlobalHotkey(kQuickbarHotkeyId, this);
    connect(m_hotkey, &WinGlobalHotkey::activated, this, &AppController::quickbarRequested);
    QString hotkeyError;
    if (!m_config.quickHotkey.isEmpty() && !m_hotkey->registerSequence(m_config.quickHotkey, &hotkeyError)) {
        // 快捷键冲突不阻止程序启动，只清空冲突配置并在托盘处提示用户。
        m_config.quickHotkey.clear();
        m_configStore.save(m_config);
        m_startupHotkeyConflict = true;
    }
    // 调试快捷键使用独立消息窗口和 ID，与快捷栏快捷键互不影响；关闭调试模式时保持未注册。
    m_debugHotkey = new WinGlobalHotkey(kDebugHotkeyId, this);
    applyDebugSettings();

    m_clipboard = new WinClipboard(this);
    connect(m_clipboard, &WinClipboard::imageCaptured, this, [this](const CapturedImage &image) {
        handleCapture(image);
    });
    if (!m_clipboard->start(error))
        return false;

    // UI 服务只在平台监听器成功后创建，避免半初始化状态响应用户操作。
    LogService::instance().info(QStringLiteral("开始创建快捷栏"));
    m_quickBar = new QuickBar;
    m_quickBar->applyConfig(m_config);
    connect(m_quickBar, &QuickBar::memeSelected, this, [this](const QString &id) {
        QString selectionError;
        if (!selectMeme(id, &selectionError)) {
            LogService::instance().error(QStringLiteral("选中表情包后写入剪贴板失败：") + selectionError);
            emit notificationShown(AppStrings::copyFailedTitle(), selectionError, true);
        }
    });
    connect(m_quickBar, &QuickBar::nicknamesChanged, this, [this](const QString &id, const QStringList &names) {
        replaceNicknames(id, names, nullptr);
    });
    connect(m_quickBar, &QuickBar::memeDeleted, this, [this](const QString &id) { deleteMeme(id, nullptr); });
    LogService::instance().info(QStringLiteral("快捷栏创建完成，开始刷新记录"));
    reloadRecords(true);
    LogService::instance().info(QStringLiteral("记录刷新完成，开始安装事件过滤器"));
    connect(this, &AppController::quickbarRequested, this, &AppController::openQuickBar);
    connect(this, &AppController::captureAvailable, this, &AppController::openCaptureToast);
    // 事件过滤器必须最后安装，确保所有服务已准备好消费原生消息。
    QCoreApplication::instance()->installNativeEventFilter(this);
    LogService::instance().info(QStringLiteral("事件过滤器安装完成"));
    LogService::instance().info(QStringLiteral("程序启动完成并进入常驻状态"));
    return true;
}

// 调试日志文件固定在程序根目录，文件名来自配置并做纯文件名清洗。
QString AppController::debugLogFilePath() const
{
    return QCoreApplication::applicationDirPath() + QLatin1Char('/')
           + m_config.resolvedDebugLogFileName();
}

// 幂等地同步调试模式：日志文件按配置接入/切换/关闭，Alt+D 只在实际开启调试模式时注册。
// 关闭时先注销快捷键，系统随后会把 Alt+D 继续交给前台程序，而不是被本程序吞掉。
void AppController::applyDebugSettings()
{
    LogService &log = LogService::instance();
    if (!m_config.debugMode) {
        if (m_debugHotkey && m_debugHotkey->isRegistered())
            m_debugHotkey->unregister();
        if (log.isDebugFileEnabled()) {
            // 关闭前把最后一条说明写进调试文件，便于确认之后的日志不再属于调试模式。
            log.info(QStringLiteral("调试模式已关闭，停止追加调试日志"));
            log.disableDebugFile();
        }
        return;
    }

    const QString path = debugLogFilePath();
    if (!log.isDebugFileEnabled()
        || QDir::cleanPath(log.debugFilePath()) != QDir::cleanPath(path)) {
        const bool switching = log.isDebugFileEnabled();
        // 切换文件名前先在旧文件里留一条说明，避免旧日志看起来无故中断。
        if (switching) {
            log.info(QStringLiteral("调试日志文件即将切换至：")
                     + QDir::toNativeSeparators(path));
        }
        QString enableError;
        if (!log.enableDebugFile(path, &enableError)) {
            if (switching)
                log.warning(QStringLiteral("调试日志文件不可写，继续使用原日志文件：") + enableError);
            else
                log.warning(QStringLiteral("调试模式已开启，但调试日志文件不可写：") + enableError);
        } else if (switching) {
            log.info(QStringLiteral("调试日志已切换至：") + QDir::toNativeSeparators(path)
                     + QStringLiteral("；后续运行日志追加写入该文件"));
        } else {
            log.info(QStringLiteral("调试模式已开启：运行日志追加写入 %1；按 Alt+D 输出一次当前剪贴板诊断快照")
                         .arg(QDir::toNativeSeparators(path)));
        }
    }

    // 注册失败不影响调试日志和程序启动，只记录原因；下次保存调试设置时会重试。
    if (m_debugHotkey && !m_debugHotkey->isRegistered()) {
        QString hotkeyError;
        if (!m_debugHotkey->registerSequence(QLatin1String(kDebugClipboardHotkey), &hotkeyError))
            log.warning(QStringLiteral("Alt+D 剪贴板诊断快捷键注册失败：") + hotkeyError);
        else
            log.info(QStringLiteral("Alt+D 剪贴板诊断快捷键已注册"));
    }
}

// Alt+D 的调试入口：记录当前剪贴板格式、捕获读取结果，以及为什么没有激活捕获浮窗。
// 只读取，不发出捕获信号，也不影响正常捕获流程。
void AppController::logClipboardDiagnostics()
{
    // 热键只应在调试模式注册；消息可能在关闭调试的瞬间残留在队列里，这里再兜底一次。
    if (!m_config.debugMode)
        return;
    LogService &log = LogService::instance();
    log.info(QStringLiteral("调试快照：收到 Alt+D 请求，开始读取当前剪贴板"));
    if (!m_clipboard) {
        log.warning(QStringLiteral("调试快照：剪贴板服务尚未创建，无法读取"));
        return;
    }

    const WinClipboard::ClipboardSnapshot snapshot = m_clipboard->debugSnapshot();
    if (!snapshot.opened) {
        log.warning(QStringLiteral("调试快照：无法打开系统剪贴板（可能被其他程序占用）"));
        return;
    }
    log.info(QStringLiteral("调试快照：剪贴板格式 %1 个：%2")
                 .arg(snapshot.formats.size())
                 .arg(snapshot.formats.isEmpty() ? QStringLiteral("（无）")
                                                 : snapshot.formats.join(QStringLiteral("、"))));

    const CapturedImage &captured = snapshot.image;
    if (captured.isValid) {
        log.info(QStringLiteral("调试快照：捕获读取成功，格式=%1，尺寸=%2x%3，编码字节=%4")
                     .arg(captured.format)
                     .arg(captured.width)
                     .arg(captured.height)
                     .arg(captured.encoded.size()));
    } else {
        log.info(QStringLiteral("调试快照：按捕获路径未读到可保存的单个逻辑图片"));
    }

    // 判定顺序与 AppController::handleCapture 的过滤顺序保持一致，便于直接定位漏捕获原因。
    QString verdict;
    bool unexpected = false;
    if (!m_config.captureEnabled) {
        verdict = QStringLiteral("剪贴板监听已暂停（captureEnabled=false），任何图片都不会弹出捕获浮窗");
    } else if (snapshot.selfWritten) {
        verdict = QStringLiteral("剪贴板带本程序自写标记，会被 selfWritten 过滤，不弹浮窗属于预期行为");
    } else if (!captured.isValid) {
        verdict = QStringLiteral("当前剪贴板没有可捕获的图片（文本、文件列表、多图引用等不会激活捕获浮窗）");
    } else if (captured.width > m_config.maxWidth || captured.height > m_config.maxHeight) {
        verdict = QStringLiteral("图片尺寸超过捕获阈值 %1x%2，会被尺寸上限过滤")
                      .arg(m_config.maxWidth)
                      .arg(m_config.maxHeight);
    } else if (m_captureToast) {
        verdict = QStringLiteral("捕获浮窗当前已存在，本次内容满足捕获条件");
    } else {
        verdict = QStringLiteral("内容满足捕获条件但当前没有捕获浮窗；若刚复制图片却没有出现浮窗，"
                                 "请检查剪贴板监听或捕获事件是否丢失");
        unexpected = true;
    }
    if (unexpected)
        log.warning(QStringLiteral("调试快照：判定=") + verdict);
    else
        log.info(QStringLiteral("调试快照：判定=") + verdict);
    log.info(QStringLiteral("调试快照：监听启用=%1，捕获浮窗=%2")
                 .arg(m_config.captureEnabled ? QStringLiteral("是") : QStringLiteral("否"),
                      m_captureToast ? QStringLiteral("已打开") : QStringLiteral("未打开")));
}

// 只查询一次数据库快照，并同时交给快捷栏和缩略图任务。
void AppController::reloadRecords(bool scheduleThumbnails)
{
    const QVector<MemeRecord> memes = m_database.memes();
    const QHash<QString, QVector<NicknameRecord>> nicknames = m_database.nicknames();
    if (m_quickBar)
        m_quickBar->updateRecords(memes, nicknames);
    if (scheduleThumbnails && m_config.thumbnailCacheSize > 0)
        startThumbnailScan(memes);
}

// 每次只允许一个 worker 存活；扫描期间的请求不丢弃，当前扫描结束后自动补扫一次。
void AppController::startThumbnailScan(const QVector<MemeRecord> &records)
{
    if (m_thumbnailThread) {
        m_thumbnailScanPending = true;
        return;
    }
    m_thumbnailThread = new QThread(this);
    m_thumbnailWorker = new ThumbnailWorker;
    m_thumbnailWorker->moveToThread(m_thumbnailThread);
    connect(m_thumbnailThread, &QThread::finished, m_thumbnailWorker, &QObject::deleteLater);
    connect(m_thumbnailWorker, &ThumbnailWorker::finished, m_thumbnailThread, &QThread::quit);
    connect(m_thumbnailThread, &QThread::finished, this, [this]() {
        m_thumbnailThread->deleteLater();
        m_thumbnailThread = nullptr;
        m_thumbnailWorker = nullptr;
        // 补扫时重新读取数据库快照，把扫描进行中新增的记录纳入生成范围。
        if (m_thumbnailScanPending) {
            m_thumbnailScanPending = false;
            reloadRecords(true);
        }
    });
    m_thumbnailThread->start();
    const QString gallery = m_database.galleryPath();
    const int size = m_config.thumbnailCacheSize;
    QMetaObject::invokeMethod(m_thumbnailWorker, [this, records, gallery, size]() {
        m_thumbnailWorker->generate(records, gallery, size);
    }, Qt::QueuedConnection);
}

// 捕获入口只接受启用监听、内容有效且不是本程序回写的图片。
void AppController::handleCapture(const CapturedImage &image)
{
    if (!m_config.captureEnabled || !image.isValid || image.selfWritten)
        return;
    if (image.width > m_config.maxWidth || image.height > m_config.maxHeight) {
        LogService::instance().info(QStringLiteral("剪贴板图片超过捕获尺寸阈值"));
        return;
    }
    emit captureAvailable(image);
}

// 优先使用配置的环境变量密钥，存在已保存密钥时再以凭据管理器值覆盖。
AiSettings AppController::aiSettings()
{
    AiSettings settings;
    settings.endpoint = m_config.aiEndpoint;
    settings.model = m_config.aiModel;
    settings.nicknameExampleCount = m_config.aiNicknameExampleCount;
    settings.gifCompatMode = m_config.gifAiCompatMode;
    settings.timeoutSeconds = 30;
    QSet<QString> seenNicknames;
    const auto groupedNicknames = m_database.nicknames();
    for (auto group = groupedNicknames.cbegin(); group != groupedNicknames.cend(); ++group) {
        for (const NicknameRecord &record : group.value()) {
            const QString key = record.normalized.trimmed();
            if (key.isEmpty() || seenNicknames.contains(key))
                continue;
            seenNicknames.insert(key);
            settings.nicknameCandidates.append(record.nickname.trimmed());
        }
    }
    if (!m_config.apiKeyEnvName.trimmed().isEmpty()) {
        settings.apiKey = qEnvironmentVariable(qPrintable(m_config.apiKeyEnvName.trimmed())).trimmed();
        if (!settings.apiKey.isEmpty())
            settings.apiKeySource = QStringLiteral("environment");
    }
    if (!m_config.hasStoredApiKey)
        return settings;
    QString credential;
    readWindowsCredential(QLatin1String(kApiKeyTarget), credential);
    if (!credential.isEmpty()) {
        settings.apiKey = credential.trimmed();
        settings.apiKeySource = QStringLiteral("windows-credential");
    }
    return settings;
}

// 先验证快捷键和图库切换，再持久化配置并刷新受影响的服务。
bool AppController::saveConfiguration(AppConfig config, QString *error)
{
    // 快捷键必须实际注册成功后才允许持久化，冲突时保留原配置。
    if (config.quickHotkey != m_config.quickHotkey && !config.quickHotkey.isEmpty()) {
        QString hotkeyError;
        if (!m_hotkey->registerSequence(config.quickHotkey, &hotkeyError)) {
            // registerSequence 会先注销旧键再注册新键；注册失败时必须尽力恢复旧键，
            // 否则保存虽被拒绝，旧快捷键却已静默失效，直到下次成功保存或重启进程。
            if (!m_config.quickHotkey.isEmpty() &&
                !m_hotkey->registerSequence(m_config.quickHotkey, nullptr))
                LogService::instance().warning(QStringLiteral("旧全局快捷键恢复注册失败"));
            if (error)
                *error = hotkeyError;
            return false;
        }
    } else if (config.quickHotkey.isEmpty() && !m_config.quickHotkey.isEmpty()) {
        m_hotkey->unregister();
    }

    const AppConfig oldConfig = m_config;
    const QString oldGallery = m_database.galleryPath();
    const QString newGallery = config.resolvedGalleryPath(QCoreApplication::applicationDirPath());
    // 切换图库前先打开新库；失败时保留旧配置和旧数据库连接。
    if (!oldGallery.isEmpty() && oldGallery != newGallery) {
        QString databaseError;
        if (!m_database.open(newGallery, &databaseError)) {
            emit databaseErrorOccurred(databaseError);
            if (error)
                *error = databaseError;
            return false;
        }
    }

    // 只有配置文件写入成功后才替换内存快照。
    if (!m_configStore.save(config, error))
        return false;
    m_config = config;
    // 调试开关或调试日志文件名变化时，立即同步日志文件和 Alt+D 热键的注册状态。
    if (config.debugMode != oldConfig.debugMode
        || config.resolvedDebugLogFileName() != oldConfig.resolvedDebugLogFileName())
        applyDebugSettings();
    // 持久化成功后再刷新快捷栏、开机启动和内存索引。
    if (m_quickBar)
        m_quickBar->applyConfig(m_config);
#ifdef Q_OS_WIN
    if (!setWindowsAutoStart(m_config.startup))
        LogService::instance().warning(QStringLiteral("开机启动配置写入失败"));
#endif
    if (m_config.thumbnailCacheSize != oldConfig.thumbnailCacheSize) {
        // 缓存尺寸改变会使旧 PNG 全部失效，后台删除后重新扫描。
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher]() {
            watcher->deleteLater();
            reloadRecords(true);
        });
        const QString gallery = m_database.galleryPath();
        watcher->setFuture(QtConcurrent::run([gallery]() {
            QDir cache(gallery + QStringLiteral("/.thumbnails"));
            const QStringList files = cache.entryList({QStringLiteral("*.png")}, QDir::Files);
            for (const QString &file : files)
                QFile::remove(cache.filePath(file));
            return true;
        }));
    }
    else {
        reloadRecords(false);
    }
    emit configurationSaved(m_config);
    LogService::instance().info(QStringLiteral("配置已应用"));
    return true;
}

// 生成新 ID 和扩展名，先写原图再写数据库索引。
bool AppController::saveCapture(CapturedImage image, const QStringList &nicknames, QString *error)
{
    // 先落盘原图，再写数据库；数据库失败时删除已写入的文件。
    MemeRecord meme;
    meme.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString extension = image.format.toLower();
    if (extension != QLatin1String("gif") && extension != QLatin1String("jpg") &&
        extension != QLatin1String("jpeg") && extension != QLatin1String("bmp") &&
        extension != QLatin1String("webp"))
        extension = QStringLiteral("png");
    meme.fileName = meme.id + QLatin1Char('.') + extension;
    meme.format = extension == QLatin1String("jpeg") ? QStringLiteral("jpg") : extension;
    meme.width = image.width;
    meme.height = image.height;
    meme.createdAt = QDateTime::currentDateTimeUtc();
    QFile output(m_database.galleryPath() + QLatin1Char('/') + meme.fileName);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error)
            *error = AppStrings::originalImageSaveFailed();
        LogService::instance().error(QStringLiteral("捕获图片落盘失败"));
        return false;
    }
    if (output.write(image.encoded) != image.encoded.size()) {
        if (error)
            *error = AppStrings::originalImageSaveFailed();
        output.remove();
        LogService::instance().error(QStringLiteral("捕获图片写入不完整"));
        return false;
    }
    output.close();
    if (!m_database.saveMeme(meme, nicknames, error)) {
        output.remove();
        return false;
    }
    reloadRecords(true);
    LogService::instance().info(QStringLiteral("新表情包已保存"));
    return true;
}

// 委托数据库完成校验和事务更新，然后刷新内存搜索数据。
bool AppController::replaceNicknames(const QString &memeId, const QStringList &nicknames, QString *error)
{
    if (!m_database.replaceNicknames(memeId, nicknames, error))
        return false;
    reloadRecords(false);
    return true;
}

// 先把文件移动到同图库的临时目录，数据库成功后再真正删除，失败可回滚。
bool AppController::deleteMeme(const QString &memeId, QString *error)
{
    const QStringList paths = m_database.memeFilePaths(memeId);
    QDir trashDirectory(m_database.galleryPath() + QStringLiteral("/.trash"));
    if (!trashDirectory.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("无法创建回收目录，已保留原记录。");
        return false;
    }

    // 同时记录原路径和临时路径，回滚不会因为跳过不存在的文件而错位。
    QVector<QPair<QString, QString>> movedFiles;
    movedFiles.reserve(paths.size());
    const auto rollbackMoves = [&movedFiles]() {
        for (auto move = movedFiles.crbegin(); move != movedFiles.crend(); ++move)
            QFile::rename(move->second, move->first);
    };

    for (const QString &path : paths) {
        if (!QFile::exists(path))
            continue;
        // v1.0.1(20260918): 原图和缩略图可能同名，必须为每次移动生成唯一目标名，避免在 .trash 中互相覆盖。
        const QString target = trashDirectory.filePath(
            QFileInfo(path).fileName() + QLatin1Char('.') +
            QUuid::createUuid().toString(QUuid::WithoutBraces) +
            QStringLiteral(".deleting"));
        if (!QFile::rename(path, target)) {
            rollbackMoves();
            if (error)
                *error = QStringLiteral("删除源文件或缩略图失败，已保留原记录。");
            return false;
        }
        movedFiles.append(QPair<QString, QString>(path, target));
    }
    if (!m_database.deleteMeme(memeId, error)) {
        rollbackMoves();
        return false;
    }
    for (const auto &move : std::as_const(movedFiles))
        QFile::remove(move.second);
    reloadRecords(false);
    LogService::instance().info(QStringLiteral("表情包及其关联标签已删除"));
    return true;
}

// 读取原图、写回剪贴板、更新使用统计，最后按配置恢复原前台窗口。
bool AppController::selectMeme(const QString &memeId, QString *error)
{
    // 先确认数据库记录仍有原图，避免向剪贴板写入空载荷。
    MemeRecord record;
    if (!m_database.findMeme(memeId, &record)) {
        if (error)
            *error = AppStrings::originalImageMissing();
        return false;
    }
    const QString sourcePath = m_database.galleryPath() + QLatin1Char('/') + record.fileName;
    if (!QFile::exists(sourcePath)) {
        if (error)
            *error = AppStrings::originalImageMissing();
        return false;
    }
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = AppStrings::originalImageReadFailed(source.errorString());
        return false;
    }
    CapturedImage image;
    image.encoded = source.readAll();
    image.format = QFileInfo(sourcePath).suffix().toLower();
    // 宽高直接取索引里的既有记录，不必为了拿尺寸先解码一次整图；原图能否解码由
    // writeImage 在写剪贴板前校验，失败时走的是同一套错误提示。
    image.width = record.width;
    image.height = record.height;
    image.isValid = true;
    if (!m_clipboard->writeImage(image, error))
        return false;
    // 写回成功后再更新使用统计，并关闭候选界面。
    if (!m_database.updateUsage(memeId, error))
        LogService::instance().warning(QStringLiteral("使用统计更新失败"));
    if (m_quickBar)
        m_quickBar->close();
    if (m_config.autoPaste && m_foregroundWindow) {
        // 延迟一点恢复原窗口，给系统时间完成剪贴板更新和快捷栏关闭。
        const quintptr pasteTarget = m_foregroundWindow;
        QTimer::singleShot(80, this, [pasteTarget]() { restoreAndPasteWindow(pasteTarget); });
    }
    reloadRecords(false);
    return true;
}

// 打开或关闭快捷栏；首次打开时记住触发快捷键前的前台窗口。
void AppController::openQuickBar()
{
    LogService::instance().info(QStringLiteral("已收到表情包查找快捷栏请求"));
    if (!m_quickBar)
        return;
    if (m_quickBar->isVisible()) {
        m_quickBar->close();
        return;
    }
    m_foregroundWindow = activeForegroundWindow();
    m_quickBar->openAt(m_config, m_foregroundWindow);
}

// 保证同一时刻只有一个捕获浮窗，并转发保存结果到图库服务。
void AppController::openCaptureToast(const CapturedImage &image)
{
    if (m_captureToast)
        m_captureToast->cancel();
    m_captureToast = new CaptureToast;
    connect(m_captureToast, &CaptureToast::saveRequested, this,
            [this](const CapturedImage &captured, const QStringList &names) {
                QString error;
                if (!saveCapture(captured, names, &error))
                    emit notificationShown(AppStrings::saveFailedTitle(), error, true);
            });
    connect(m_captureToast, &QObject::destroyed, this, [this]() { m_captureToast = nullptr; });
    m_captureToast->showImage(image, m_config, aiSettings());
}

// 在后台线程执行导入，完成后回到主线程刷新索引并通知设置窗口。
void AppController::importDatabase(const QString &sourcePath)
{
    auto *watcher = new QFutureWatcher<ImportResult>(this);
    connect(watcher, &QFutureWatcher<ImportResult>::finished, this, [this, watcher]() {
        const ImportResult result = watcher->result();
        watcher->deleteLater();
        reloadRecords(true);
        emit importFinished(result);
    });
    const QString current = m_database.galleryPath();
    watcher->setFuture(QtConcurrent::run([current, sourcePath]() {
        return ImportService::importSource(current, sourcePath);
    }));
}

// 空字符串表示删除凭据，否则写入凭据管理器并更新配置标志。
bool AppController::storeApiKey(const QString &secret, QString *error)
{
    if (secret.trimmed().isEmpty()) {
        if (!deleteWindowsCredential(QLatin1String(kApiKeyTarget))) {
            if (error)
                *error = QStringLiteral("清除Windows凭据失败。");
            return false;
        }
        m_config.hasStoredApiKey = false;
        return true;
    }
    if (!writeWindowsCredential(QLatin1String(kApiKeyTarget), secret.trimmed())) {
        if (error)
            *error = QStringLiteral("写入Windows凭据管理器失败。");
        return false;
    }
    m_config.hasStoredApiKey = true;
    return true;
}
