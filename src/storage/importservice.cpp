// 导入不做内容去重；源库保持只读，ID冲突时重新生成ID并保留nickname。
#include "storage/importservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "platforms/win/winarchive.h"
#include "storage/logservice.h"

namespace {
// 检查目录是否直接包含任一大小写形式的图库数据库。
bool hasLibraryDatabase(const QDir &directory)
{
    const QStringList files = directory.entryList({QStringLiteral("MemeServant2.db"),
                                                   QStringLiteral("memeservant2.db")},
                                                  QDir::Files);
    return !files.isEmpty();
}

// 统一分隔符并移除开头斜杠，使后续候选路径保持在图库目录下。
QString normalizedRelativeName(const QString &fileName)
{
    QString name = QDir::fromNativeSeparators(fileName);
    while (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);
    return name;
}
}

// 根据扩展名处理数据库文件或临时解压的 ZIP 归档，并清理临时目录。
ImportResult ImportService::importSource(const QString &currentGallery, const QString &sourcePath)
{
    QFileInfo sourceInfo(sourcePath);
    if (sourceInfo.suffix().compare(QLatin1String("db"), Qt::CaseInsensitive) == 0)
        return importGallery(currentGallery, sourceInfo.absolutePath());
    if (sourceInfo.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) != 0)
        return {0, 0, 0, QStringLiteral("导入文件类型必须是.db或.zip。")};

    QString error;
    const QString tempRoot = QDir::temp().filePath(QStringLiteral("MemeServant2-import-%1")
                                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!extractWindowsZip(sourcePath, tempRoot, &error))
        return {0, 0, 0, error};

    const QString libraryPath = locateDatabase(tempRoot);
    if (libraryPath.isEmpty()) {
        QDir(tempRoot).removeRecursively();
        return {0, 0, 0, QStringLiteral("压缩包中没有找到MemeServant2.db。")};
    }
    const ImportResult result = importGallery(currentGallery, libraryPath);
    QDir(tempRoot).removeRecursively();
    return result;
}

// 检查根目录及一级子目录，兼容不同打包工具产生的 ZIP 目录结构。
QString ImportService::locateDatabase(const QString &sourceLibraryPath)
{
    QDir root(sourceLibraryPath);
    if (hasLibraryDatabase(root))
        return root.absolutePath();

    const QStringList folders = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                               QDir::Name | QDir::IgnoreCase);
    for (const QString &folder : folders) {
        QDir candidate = root.filePath(folder);
        if (hasLibraryDatabase(candidate))
            return candidate.absolutePath();
    }
    return {};
}

// 拒绝疑似路径穿越名称，并在旧版图库常见位置中查找原图。
QString ImportService::resolveSourceImage(const QString &sourceLibraryPath, const QString &fileName)
{
    const QString relative = normalizedRelativeName(fileName);
    if (relative.contains(QLatin1String("..")))
        return {};

    QDir library(sourceLibraryPath);
    const QStringList candidates {
        library.filePath(relative),
        library.filePath(QStringLiteral("images/") + relative),
        library.filePath(QStringLiteral("images/") + QFileInfo(relative).fileName())
    };
    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

// 只读遍历源库，逐条复制图片、解决 ID/文件名冲突并导入 nickname。
ImportResult ImportService::importGallery(const QString &currentGallery, const QString &sourceLibraryPath)
{
    ImportResult result;
    QDir library(sourceLibraryPath);
    QString databasePath;
    for (const QString &file : library.entryList(QDir::Files)) {
        if (file.compare(QStringLiteral("MemeServant2.db"), Qt::CaseInsensitive) == 0 ||
            file.compare(QStringLiteral("memeservant2.db"), Qt::CaseInsensitive) == 0) {
            databasePath = library.filePath(file);
            break;
        }
    }
    if (databasePath.isEmpty())
        return {0, 0, 0, QStringLiteral("没有找到MemeServant2.db。")};

    const QString connection = QStringLiteral("import-%1").arg(QDateTime::currentMSecsSinceEpoch());
    {
        QSqlDatabase source = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        source.setDatabaseName(databasePath);
        source.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!source.open()) {
            result.error = QStringLiteral("源数据库无法打开。");
            QSqlDatabase::removeDatabase(connection);
            return result;
        }
        QSqlDatabase target = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                        connection + QStringLiteral("-target"));
        target.setDatabaseName(currentGallery + QStringLiteral("/memeservant2.db"));
        if (!target.open()) {
            result.error = QStringLiteral("当前数据库无法打开。");
            source.close();
            QSqlDatabase::removeDatabase(connection);
            QSqlDatabase::removeDatabase(connection + QStringLiteral("-target"));
            return result;
        }

        // 源库只读，目标库可写；所有图片和索引在目标事务中批量处理。
        QSqlQuery sourceQuery(source);
        QSqlQuery targetQuery(target);
        if (!sourceQuery.exec(QStringLiteral(
                "SELECT id,file_name,format,width,height,created_at,last_used_at,use_count FROM memes"))) {
            result.error = QStringLiteral("源数据库不是有效的MemeServant2数据库。");
            source.close();
            target.close();
            QSqlDatabase::removeDatabase(connection);
            QSqlDatabase::removeDatabase(connection + QStringLiteral("-target"));
            return result;
        }
        // 目标库批量导入使用单事务，单条失败时保留失败计数并继续处理后续记录。
        targetQuery.exec(QStringLiteral("BEGIN IMMEDIATE TRANSACTION"));
        while (sourceQuery.next()) {
            ++result.memes;
            const QString sourceId = sourceQuery.value(0).toString();
            const QString storedFileName = sourceQuery.value(1).toString();
            const QString sourceFile = resolveSourceImage(library.absolutePath(), storedFileName);
            QString newId = sourceId;
            QString newFileName = QFileInfo(storedFileName).fileName();
            QSqlQuery conflict(target);
            conflict.prepare(QStringLiteral("SELECT 1 FROM memes WHERE id=:id OR file_name=:file"));
            conflict.bindValue(QStringLiteral(":id"), newId);
            conflict.bindValue(QStringLiteral(":file"), newFileName);
            conflict.exec();
            if (conflict.next()) {
                // ID 或文件名任一冲突都生成新 ID，保留源文件扩展名。
                newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                newFileName = newId + QLatin1Char('.') + QFileInfo(newFileName).suffix().toLower();
            }
            const QString targetFile = currentGallery + QLatin1Char('/') + newFileName;
            if (sourceFile.isEmpty() || !QFile::copy(sourceFile, targetFile)) {
                ++result.failed;
                continue;
            }
            if (!targetQuery.prepare(QStringLiteral(
                    "INSERT INTO memes(id,file_name,format,width,height,created_at,last_used_at,use_count) "
                    "VALUES(:id,:file,:format,:width,:height,:created,:used,:count)"))) {
                ++result.failed;
                QFile::remove(targetFile);
                continue;
            }
            targetQuery.bindValue(QStringLiteral(":id"), newId);
            targetQuery.bindValue(QStringLiteral(":file"), newFileName);
            targetQuery.bindValue(QStringLiteral(":format"), sourceQuery.value(2));
            targetQuery.bindValue(QStringLiteral(":width"), sourceQuery.value(3));
            targetQuery.bindValue(QStringLiteral(":height"), sourceQuery.value(4));
            targetQuery.bindValue(QStringLiteral(":created"), sourceQuery.value(5));
            targetQuery.bindValue(QStringLiteral(":used"), sourceQuery.value(6));
            targetQuery.bindValue(QStringLiteral(":count"), sourceQuery.value(7));
            if (!targetQuery.exec()) {
                ++result.failed;
                QFile::remove(targetFile);
                continue;
            }

            QSqlQuery sourceNames(source);
            // nickname 以源 meme_id 查询，再绑定到可能已改名的新 ID。
            sourceNames.prepare(QStringLiteral("SELECT nickname,normalized FROM nicknames WHERE meme_id=:id"));
            sourceNames.bindValue(QStringLiteral(":id"), sourceId);
            sourceNames.exec();
            while (sourceNames.next()) {
                ++result.nicknames;
                QSqlQuery insert(target);
                insert.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO nicknames(meme_id,nickname,normalized) VALUES(:meme,:name,:normal)"));
                insert.bindValue(QStringLiteral(":meme"), newId);
                insert.bindValue(QStringLiteral(":name"), sourceNames.value(0));
                insert.bindValue(QStringLiteral(":normal"), sourceNames.value(1));
                if (!insert.exec())
                    ++result.failed;
            }
        }
        targetQuery.exec(QStringLiteral("COMMIT"));
        source.close();
        target.close();
    }
    QSqlDatabase::removeDatabase(connection + QStringLiteral("-target"));
    QSqlDatabase::removeDatabase(connection);
    LogService::instance().info(QStringLiteral("外部数据导入任务结束"));
    return result;
}
