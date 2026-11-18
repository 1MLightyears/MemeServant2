// 声明线程安全的应用日志单例服务。
#ifndef STORAGE_LOGSERVICE_H
#define STORAGE_LOGSERVICE_H

// 统一中文日志输出，执行简单的大小轮转，并支持把日志镜像到调试文件。
#include <QString>

class LogService
{
public:
    static LogService &instance();
    /// 初始化日志目录，必要时回退到 LOCALAPPDATA 并轮转大文件。
    void initialize(const QString &preferredDirectory);
    /// 启用调试日志文件；成功后所有日志行同时追加到该文件，失败时保留原有调试文件。
    bool enableDebugFile(const QString &filePath, QString *error = nullptr);
    /// 停止向调试日志文件追加；主日志不受影响。
    void disableDebugFile();
    bool isDebugFileEnabled() const { return !m_debugFilePath.isEmpty(); }
    QString debugFilePath() const { return m_debugFilePath; }
    void info(const QString &message);
    void warning(const QString &message);
    void error(const QString &message);

private:
    LogService() = default;
    /// 追加一行带时间、级别和消息的日志。
    void write(const QString &level, const QString &message);

    QString m_filePath;
    /// 非空表示调试模式开启，write 会把同一行再追加到该文件。
    QString m_debugFilePath;
};

#endif
