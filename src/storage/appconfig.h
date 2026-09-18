// 定义应用配置模型及配置文件读写服务。
#ifndef STORAGE_APPCONFIG_H
#define STORAGE_APPCONFIG_H

// 配置对象是config.json的内存表示，API Key本身永远不进入该文件。
#include <QString>
#include <QStringList>

struct AppConfig
{
    QString galleryPath;
    bool startup = false;
    bool captureEnabled = true;
    int maxWidth = 1920;
    int maxHeight = 1080;
    QString saveShortcut = QStringLiteral("Ctrl+S");
    QString cancelShortcut = QStringLiteral("Esc");
    QString aiShortcut = QStringLiteral("Alt+A");
    QString quickHotkey = QStringLiteral("Ctrl+Alt+X");
    int rows = 1;
    int columns = 3;
    int thumbnailDisplaySize = 128;
    /// 鼠标在候选缩略图上停留多久后显示原图预览；0 表示关闭悬停预览。
    int thumbnailPreviewDelayMs = 1500;
    bool autoPaste = true;
    QString aiProvider = QStringLiteral("OpenAI Compatible");
    QString aiEndpoint;
    QString aiModel;
    QString apiKeyEnvName;
    bool hasStoredApiKey = false;
    bool autoAi = false;
    int aiNicknameExampleCount = 8;
    int thumbnailCacheSize = 256;

    /// 将空图库路径解析为程序目录下的 memes 子目录。
    QString resolvedGalleryPath(const QString &applicationDirectory) const;
};

class ConfigStore
{
public:
    explicit ConfigStore(const QString &applicationDirectory);
    /// 读取当前有效配置文件；文件不可读时返回默认配置。
    AppConfig load();
    /// 将配置序列化为 JSON 写入当前有效路径。
    bool save(AppConfig &config, QString *error = nullptr);

private:
    /// 返回程序目录下的首选配置路径。
    QString preferredPath() const;
    /// 返回 AppData 下的备用配置路径。
    QString fallbackPath() const;
    /// 选择已存在或可写的配置路径。
    QString activePath() const;

    QString m_applicationDirectory;
    QString m_activePath;
};

#endif
