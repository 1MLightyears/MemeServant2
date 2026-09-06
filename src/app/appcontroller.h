// 声明应用控制器，协调平台事件、存储服务、后台任务与界面。
#ifndef APP_APPCONTROLLER_H
#define APP_APPCONTROLLER_H

// 连接配置、数据库、平台服务、后台任务与 UI，但不直接操作 Win32。
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QVector>
#include "ai/openaicompatibleprovider.h"
#include "clipboard/clipboardtypes.h"
#include "core/models.h"
#include "core/searchengine.h"
#include "storage/appconfig.h"
#include "storage/databaseservice.h"
#include "storage/importservice.h"

class CaptureToast;
class QQuickBarForward;
class QThread;
class WinClipboard;
class WinGlobalHotkey;
class WinSingleInstance;
class ThumbnailWorker;
class QuickBar;

class AppController : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;
    /// 将 Windows 原生消息分发给单实例、全局快捷键和剪贴板服务。
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
    /// 按配置初始化日志、数据库、平台监听器、快捷栏和原生事件过滤器。
    bool initialize(QString *error);
    AppConfig config() const { return m_config; }
    QString galleryPath() const { return m_database.galleryPath(); }
    /// 保存配置，并在快捷键或图库路径变化时先完成相应迁移。
    bool saveConfiguration(AppConfig config, QString *error);
    /// 将捕获图片写入图库并创建其 nickname 索引。
    bool saveCapture(CapturedImage image, const QStringList &nicknames, QString *error);
    /// 校验并替换指定表情包的全部 nickname。
    bool replaceNicknames(const QString &memeId, const QStringList &nicknames, QString *error);
    /// 删除表情包记录、原图、缩略图和关联 nickname。
    bool deleteMeme(const QString &memeId, QString *error);
    /// 将指定表情包原图写入剪贴板，并按配置恢复之前的前台窗口。
    bool selectMeme(const QString &memeId, QString *error);
    /// 导入数据库或 ZIP 归档，并通过信号报告结果。
    void importDatabase(const QString &sourceDatabasePath);
    bool storeApiKey(const QString &secret, QString *error);
    bool startupHotkeyConflict() const { return m_startupHotkeyConflict; }
    /// 打开快捷栏并记住触发快捷键前的前台窗口。
    void openQuickBar();
    /// 创建捕获浮窗并把图片交给它处理。
    void openCaptureToast(const CapturedImage &image);

signals:
    void captureAvailable(const CapturedImage &image);
    void quickbarRequested();
    void settingsRequested();
    void notificationShown(const QString &title, const QString &message, bool withSound);
    void recordsChanged();
    void configurationSaved(const AppConfig &config);
    void databaseErrorOccurred(const QString &message);
    void importFinished(const ImportResult &result);

private:
    /// 从数据库刷新搜索索引，并按需启动缩略图扫描。
    void reloadRecords(bool scheduleThumbnails = true);
    /// 过滤无效或超尺寸图片后发出捕获信号。
    void handleCapture(const CapturedImage &image);
    /// 在后台线程扫描并生成缺失的缩略图缓存。
    void startThumbnailScan();
    /// 合并环境变量和 Windows 凭据中的 AI 密钥配置。
    AiSettings aiSettings();

    ConfigStore m_configStore;
    AppConfig m_config;
    DatabaseService m_database;
    SearchEngine m_searchEngine;
    WinClipboard *m_clipboard = nullptr;
    WinGlobalHotkey *m_hotkey = nullptr;
    WinSingleInstance *m_singleInstance = nullptr;
    CaptureToast *m_captureToast = nullptr;
    QuickBar *m_quickBar = nullptr;
    QThread *m_thumbnailThread = nullptr;
    ThumbnailWorker *m_thumbnailWorker = nullptr;
    quintptr m_foregroundWindow = 0;
    bool m_startupHotkeyConflict = false;
};

#endif
