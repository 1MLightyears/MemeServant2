// 日志禁止记录昵称正文、图片数据和任何密钥值。
#include "storage/logservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

// 通过函数内静态对象保证单例在首次使用时初始化。
LogService &LogService::instance()
{
    static LogService service;
    return service;
}

// 创建日志目录、按大小轮转旧文件，并记录初始化完成事件。
void LogService::initialize(const QString &preferredDirectory)
{
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

void LogService::info(const QString &message) { write(QStringLiteral("INFO"), message); }
void LogService::warning(const QString &message) { write(QStringLiteral("WARNING"), message); }
void LogService::error(const QString &message) { write(QStringLiteral("ERROR"), message); }

// 追加单行日志；未初始化或文件不可写时静默返回。
void LogService::write(const QString &level, const QString &message)
{
    if (m_filePath.isEmpty())
        return;
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << QLatin1Char('\t')
           << level << QLatin1Char('\t') << message << QLatin1Char('\n');
}
