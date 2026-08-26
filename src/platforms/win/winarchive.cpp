// Windows 10/11自带tar可解压ZIP；失败原因只写日志，不暴露路径内容。
#include "platforms/win/winarchive.h"

#include <QDir>
#include <cstdlib>

#include "storage/logservice.h"

// 创建解压目录后调用 Windows tar；不把具体路径写入日志或错误文本。
bool extractWindowsZip(const QString &archivePath, const QString &destinationPath, QString *error)
{
    QDir destination(destinationPath);
    if (!destination.exists() && !destination.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("导入临时目录创建失败。");
        return false;
    }
    const QString command = QStringLiteral("tar -xf \"%1\" -C \"%2\"")
                                .arg(QDir::toNativeSeparators(archivePath),
                                     QDir::toNativeSeparators(destinationPath));
    const int result = _wsystem(reinterpret_cast<const wchar_t *>(command.utf16()));
    if (result != 0) {
        LogService::instance().warning(QStringLiteral("ZIP解压失败"));
        if (error)
            *error = QStringLiteral("ZIP文件解压失败。");
        return false;
    }
    return true;
}
