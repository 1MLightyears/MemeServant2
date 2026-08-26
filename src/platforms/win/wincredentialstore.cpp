// 凭据值不进入日志，只向调用方返回布尔结果。
#include "platforms/win/wincredentialstore.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <windows.h>
#include <wincred.h>

namespace {
// 将逻辑名称限制为 Credential Manager 允许且稳定的目标名。
QString targetName(const QString &name)
{
    QString safe = name;
    safe.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")));
    return QStringLiteral("MemeServant2:%1").arg(safe);
}
}

// 读取 UTF-8 凭据内容；找不到凭据时不修改输出值。
bool readWindowsCredential(const QString &name, QString &secret)
{
    PCREDENTIALW credential = nullptr;
    const QString target = targetName(name);
    if (!CredReadW(reinterpret_cast<const wchar_t *>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential))
        return false;
    const QByteArray bytes(reinterpret_cast<const char *>(credential->CredentialBlob), credential->CredentialBlobSize);
    secret = QString::fromUtf8(bytes);
    CredFree(credential);
    return true;
}

// 将密钥写入本机持久化凭据，调用方负责避免把内容记录到日志。
bool writeWindowsCredential(const QString &name, const QString &secret)
{
    QByteArray bytes = secret.toUtf8();
    const QString target = targetName(name);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(target.utf16()));
    credential.CredentialBlobSize = DWORD(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&credential, 0);
}

// 删除指定逻辑名称对应的凭据。
bool deleteWindowsCredential(const QString &name)
{
    const QString target = targetName(name);
    return CredDeleteW(reinterpret_cast<const wchar_t *>(target.utf16()), CRED_TYPE_GENERIC, 0);
}
