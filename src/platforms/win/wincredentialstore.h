// 声明 Windows Credential Manager 中密钥的读写辅助函数。
#ifndef PLATFORMS_WIN_WINCREDENTIALSTORE_H
#define PLATFORMS_WIN_WINCREDENTIALSTORE_H

// API Key 只保存在 Windows 凭据管理器中。
#include <QString>

/// 读取指定逻辑名称的 UTF-8 凭据；不存在时返回 false。
bool readWindowsCredential(const QString &name, QString &secret);
/// 将密钥以本机持久化方式写入指定逻辑名称。
bool writeWindowsCredential(const QString &name, const QString &secret);
/// 删除指定逻辑名称的凭据。
bool deleteWindowsCredential(const QString &name);

#endif
