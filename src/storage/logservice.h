// 声明线程安全的应用日志单例服务。
#ifndef STORAGE_LOGSERVICE_H
#define STORAGE_LOGSERVICE_H

// 统一中文日志输出，并执行简单的大小轮转。
#include <QString>

class LogService
{
public:
    static LogService &instance();
    /// 初始化日志目录，必要时回退到 LOCALAPPDATA 并轮转大文件。
    void initialize(const QString &preferredDirectory);
    void info(const QString &message);
    void warning(const QString &message);
    void error(const QString &message);

private:
    LogService() = default;
    /// 追加一行带时间、级别和消息的日志。
    void write(const QString &level, const QString &message);

    QString m_filePath;
};

#endif
