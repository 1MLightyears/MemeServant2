// 负责程序目录与AppData之间的配置回退，并保持默认值一致。
#include "storage/appconfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "core/appstrings.h"
#include "storage/logservice.h"

namespace {
// 返回 Qt 约定的当前用户本地应用数据目录，并统一路径分隔符。
QString localAppData()
{
    return QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
}
}

// 使用配置路径；未配置时回退到程序目录下的 memes 子目录。
QString AppConfig::resolvedGalleryPath(const QString &applicationDirectory) const
{
    if (galleryPath.trimmed().isEmpty())
        return applicationDirectory + QStringLiteral("/memes");
    return QDir::fromNativeSeparators(galleryPath);
}

// 保存程序目录，后续按可写性选择配置文件位置。
ConfigStore::ConfigStore(const QString &applicationDirectory)
    : m_applicationDirectory(applicationDirectory)
{
}

// 程序目录是便携版优先使用的配置位置。
QString ConfigStore::preferredPath() const
{
    return m_applicationDirectory + QStringLiteral("/config.json");
}

// 程序目录不可写时使用当前用户 AppData 目录。
QString ConfigStore::fallbackPath() const
{
    return localAppData() + QStringLiteral("/config.json");
}

// 缓存已选路径；首次选择时优先已有文件或可写的程序目录。
QString ConfigStore::activePath() const
{
    if (!m_activePath.isEmpty())
        return m_activePath;
    QFile file(preferredPath());
    if (file.exists())
        return preferredPath();
    QFileInfo info(preferredPath());
    QDir parent = info.dir();
    if ((parent.exists() || parent.mkpath(QStringLiteral("."))) && QFileInfo(parent.absolutePath()).isWritable())
        return preferredPath();
    return fallbackPath();
}

// 读取 JSON 字段并对旧版本快捷键、行列数执行兼容迁移。
AppConfig ConfigStore::load()
{
    AppConfig config;
    QFile file(activePath());
    if (!file.open(QIODevice::ReadOnly)) {
        LogService::instance().warning(QStringLiteral("配置文件不存在或无法读取，使用默认配置"));
        return config;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    config.galleryPath = object.value(QStringLiteral("galleryPath")).toString();
    config.startup = object.value(QStringLiteral("startup")).toBool(false);
    config.captureEnabled = object.value(QStringLiteral("captureEnabled")).toBool(true);
    config.maxWidth = object.value(QStringLiteral("maxWidth")).toInt(1920);
    config.maxHeight = object.value(QStringLiteral("maxHeight")).toInt(1080);
    config.saveShortcut = object.value(QStringLiteral("saveShortcut")).toString(QStringLiteral("Ctrl+S"));
    // Enter曾是默认保存键；迁移后将其交还给多行编辑器用于正常换行。
    if (config.saveShortcut == QLatin1String("Enter") || config.saveShortcut == QLatin1String("Return"))
        config.saveShortcut = QStringLiteral("Ctrl+S");
    config.cancelShortcut = object.value(QStringLiteral("cancelShortcut")).toString(QStringLiteral("Esc"));
    config.aiShortcut = object.value(QStringLiteral("aiShortcut")).toString(QStringLiteral("Alt+A"));
    config.quickHotkey = object.value(QStringLiteral("quickHotkey")).toString(QStringLiteral("Ctrl+Alt+X"));
    int rows = object.value(QStringLiteral("rows")).toInt(1);
    config.columns = qBound(1, object.value(QStringLiteral("columns")).toInt(3), 8);
    // 旧版默认值曾写入3x3；这里迁移为横向3列，用户后续仍可自行设置多行。
    if (rows == 3 && config.columns == 3)
        rows = 1;
    config.rows = qBound(1, rows, 8);
    config.thumbnailDisplaySize = object.value(QStringLiteral("thumbnailDisplaySize")).toInt(128);
    config.autoPaste = object.value(QStringLiteral("autoPaste")).toBool(true);
    config.aiProvider = object.value(QStringLiteral("aiProvider")).toString(QStringLiteral("OpenAI Compatible"));
    config.aiEndpoint = object.value(QStringLiteral("aiEndpoint")).toString();
    config.aiModel = object.value(QStringLiteral("aiModel")).toString();
    config.apiKeyEnvName = object.value(QStringLiteral("apiKeyEnvName")).toString();
    config.hasStoredApiKey = object.value(QStringLiteral("hasStoredApiKey")).toBool(false);
    config.autoAi = object.value(QStringLiteral("autoAi")).toBool(false);
    config.aiNicknameExampleCount = qBound(
        0, object.value(QStringLiteral("aiNicknameExampleCount")).toInt(8), 50);
    config.thumbnailCacheSize = object.value(QStringLiteral("thumbnailCacheSize")).toInt(256);
    m_activePath = file.fileName();
    return config;
}

// 将内存配置完整序列化；无法打开目标文件时返回带原因的错误。
bool ConfigStore::save(AppConfig &config, QString *error)
{
    const QString path = activePath();
    QJsonObject object;
    object.insert(QStringLiteral("galleryPath"), config.galleryPath);
    object.insert(QStringLiteral("startup"), config.startup);
    object.insert(QStringLiteral("captureEnabled"), config.captureEnabled);
    object.insert(QStringLiteral("maxWidth"), config.maxWidth);
    object.insert(QStringLiteral("maxHeight"), config.maxHeight);
    object.insert(QStringLiteral("saveShortcut"), config.saveShortcut);
    object.insert(QStringLiteral("cancelShortcut"), config.cancelShortcut);
    object.insert(QStringLiteral("aiShortcut"), config.aiShortcut);
    object.insert(QStringLiteral("quickHotkey"), config.quickHotkey);
    object.insert(QStringLiteral("rows"), config.rows);
    object.insert(QStringLiteral("columns"), config.columns);
    object.insert(QStringLiteral("thumbnailDisplaySize"), config.thumbnailDisplaySize);
    object.insert(QStringLiteral("autoPaste"), config.autoPaste);
    object.insert(QStringLiteral("aiProvider"), config.aiProvider);
    object.insert(QStringLiteral("aiEndpoint"), config.aiEndpoint);
    object.insert(QStringLiteral("aiModel"), config.aiModel);
    object.insert(QStringLiteral("apiKeyEnvName"), config.apiKeyEnvName);
    object.insert(QStringLiteral("hasStoredApiKey"), config.hasStoredApiKey);
    object.insert(QStringLiteral("autoAi"), config.autoAi);
    object.insert(QStringLiteral("aiNicknameExampleCount"), config.aiNicknameExampleCount);
    object.insert(QStringLiteral("thumbnailCacheSize"), config.thumbnailCacheSize);

    QFileInfo info(path);
    if (!info.dir().exists())
        info.dir().mkpath(QStringLiteral("."));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
        *error = AppStrings::configWriteFailed(file.errorString());
        return false;
    }
    const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(encoded) != encoded.size()) {
        if (error)
            *error = AppStrings::configWriteFailed(file.errorString());
        return false;
    }
    m_activePath = path;
    LogService::instance().info(QStringLiteral("配置已保存"));
    return true;
}
