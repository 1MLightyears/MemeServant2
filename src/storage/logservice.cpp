// 日志禁止记录昵称正文、图片数据和任何密钥值。
#include "storage/logservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
// 把一行日志追加到文件末尾；失败时静默返回，避免日志问题影响主流程。
bool appendLine(const QString &path, const QByteArray &line)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    return file.write(line) == line.size();
}
}

// 通过函数内静态对象保证单例在首次使用时初始化。
LogService &LogService::instance()
{
    static LogService service;
    return service;
}

// 创建日志目录、按大小轮转旧文件，并记录初始化完成事件。
void LogService::initialize(const QString &preferredDirectory)
{
    m_debugFilePath.clear();
    QDir directory(preferredDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        const QString fallback = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"))
                                 + QStringLiteral("/MemeServant2/logs");
        directory.setPath(fallback);
        directory.mkpath(QStringLiteral("."));
    }
    m_filePath = directory.filePath(QStringLiteral("memeservant2.log"));
    QFile file(m_filePath);
    if (file.size() > 5 * 1024 * 1024) {
        const QString rotated = directory.filePath(
            QStringLiteral("memeservant2.%1.log").arg(QDateTime::currentDateTimeUtc().toSecsSinceEpoch()));
        QFile::rename(m_filePath, rotated);
    }
    info(QStringLiteral("日志系统初始化完成"));
}

// 先用追加模式实际打开一次验证可写，再切换路径；失败时保留原来的调试日志文件。
bool LogService::enableDebugFile(const QString &filePath, QString *error)
{
    const QString normalized = QDir::fromNativeSeparators(filePath.trimmed());
    if (normalized.isEmpty()) {
        if (error)
            *error = QStringLiteral("调试日志文件路径为空。");
        return false;
    }
    const QFileInfo info(normalized);
    if (!info.dir().exists() && !info.dir().mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("无法创建调试日志目录。");
        return false;
    }
    QFile file(normalized);
    // 追加模式不截断旧内容，也顺便完成“不开调试模式就不产生日志文件”的语义。
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法写入调试日志文件：") + file.errorString();
        return false;
    }
    file.close();
    m_debugFilePath = normalized;
    return true;
}

void LogService::disableDebugFile() { m_debugFilePath.clear(); }

void LogService::info(const QString &message) { write(QStringLiteral("INFO"), message); }
void LogService::warning(const QString &message) { write(QStringLiteral("WARNING"), message); }
void LogService::error(const QString &message) { write(QStringLiteral("ERROR"), message); }

// 追加单行日志；未初始化或文件不可写时静默返回。
void LogService::write(const QString &level, const QString &message)
{
    if (m_filePath.isEmpty())
        return;
    const QByteArray line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8()
                            + '\t' + level.toUtf8() + '\t' + message.toUtf8() + '\n';
    appendLine(m_filePath, line);
    // 调试模式开启时把同一行镜像到根目录调试文件，便于把常规运行日志和剪贴板快照对照。
    if (!m_debugFilePath.isEmpty() && m_debugFilePath != m_filePath)
        appendLine(m_debugFilePath, line);
}
