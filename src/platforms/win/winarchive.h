// 声明使用 Windows 自带工具解压导入归档的辅助函数。
#ifndef PLATFORMS_WIN_WINARCHIVE_H
#define PLATFORMS_WIN_WINARCHIVE_H

// 封装 Windows 平台 ZIP 解压能力，供导入服务使用。
#include <QString>

/// 使用系统 tar 将 ZIP 归档解压到目标目录，并返回可读错误信息。
bool extractWindowsZip(const QString &archivePath, const QString &destinationPath, QString *error);

#endif
