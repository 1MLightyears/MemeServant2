// 开机启动路径始终使用当前exe位置，便携目录移动后重新保存即可更新。
#include "platforms/win/winautostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

// 通过 HKCU Run 项设置当前用户的自启动命令，禁用时删除该项。
bool setWindowsAutoStart(bool enabled)
{
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                       QSettings::Registry64Format);
    if (!enabled) {
        settings.remove(QStringLiteral("MemeServant2"));
        return true;
    }
    const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    settings.setValue(QStringLiteral("MemeServant2"), QStringLiteral("\"%1\"").arg(executable));
    return settings.status() == QSettings::NoError;
}
