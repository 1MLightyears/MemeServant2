// 应用启动、退出、竞态保护和核心业务流转集中在这里处理。
#include "app/appcontroller.h"
#include "core/appstrings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
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
        m_thumbnailThread->wait(3000);
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
    return m_clipboard && m_clipboard->processMessage(windowsMessage) && false;
}

// 按日志、单实例、数据库、快捷键、剪贴板、UI 的顺序启动所有服务。
bool AppController::initialize(QString *error)
{
    // 先加载日志和配置，之后所有失败路径都能留下诊断信息。
    LogService::instance().initialize(QCoreApplication::applicationDirPath() + QStringLiteral("/logs"));
    m_config = m_configStore.load();

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

    m_hotkey = new WinGlobalHotkey(this);
    connect(m_hotkey, &WinGlobalHotkey::activated, this, &AppController::quickbarRequested);
    QString hotkeyError;
    if (!m_config.quickHotkey.isEmpty() && !m_hotkey->registerSequence(m_config.quickHotkey, &hotkeyError)) {
        // 快捷键冲突不阻止程序启动，只清空冲突配置并在托盘处提示用户。
        m_config.quickHotkey.clear();
        m_configStore.save(m_config);
        m_startupHotkeyConflict = true;
    }

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

// 每次只允许一个 worker 存活；完成后线程退出并复位，后续变更可再次扫描。
void AppController::startThumbnailScan(const QVector<MemeRecord> &records)
{
    if (m_thumbnailThread)
        return;
    m_thumbnailThread = new QThread(this);
    m_thumbnailWorker = new ThumbnailWorker;
    m_thumbnailWorker->moveToThread(m_thumbnailThread);
    connect(m_thumbnailThread, &QThread::finished, m_thumbnailWorker, &QObject::deleteLater);
    connect(m_thumbnailWorker, &ThumbnailWorker::finished, m_thumbnailThread, &QThread::quit);
    connect(m_thumbnailThread, &QThread::finished, this, [this]() {
        m_thumbnailThread->deleteLater();
        m_thumbnailThread = nullptr;
        m_thumbnailWorker = nullptr;
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
    LogService::instance().info(QStringLiteral("表情包及其关联nickname已删除"));
    return true;
}

// 读取原图、写回剪贴板、更新使用统计，最后按配置恢复原前台窗口。
bool AppController::selectMeme(const QString &memeId, QString *error)
{
    // 先确认数据库记录仍有原图，避免向剪贴板写入空载荷。
    const QStringList paths = m_database.memeFilePaths(memeId);
    if (paths.isEmpty() || !QFile::exists(paths.first())) {
        if (error)
            *error = AppStrings::originalImageMissing();
        return false;
    }
    QFile source(paths.first());
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = AppStrings::originalImageReadFailed(source.errorString());
        return false;
    }
    CapturedImage image;
    image.encoded = source.readAll();
    image.format = QFileInfo(paths.first()).suffix().toLower();
    QImage decoded = QImage::fromData(image.encoded, image.format.toLatin1().constData());
    if (decoded.isNull())
        decoded = QImage::fromData(image.encoded);
    image.width = decoded.width();
    image.height = decoded.height();
    image.isValid = !decoded.isNull();
    if (!image.isValid) {
        if (error)
            *error = AppStrings::originalImageDecodeFailed();
        return false;
    }
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
