// SQLite是索引权威来源；迁移失败时必须保持数据库不可继续使用。
#include "storage/databaseservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

// 目标schema版本由 cmake/ProjectConfig.cmake 经 CMake 生成。
#include <appmetadata.h>

#include "core/nicknameutils.h"
#include "core/appstrings.h"
#include "storage/logservice.h"

namespace {
// memes 表的列顺序在 memes() 与 findMeme() 之间共享，避免两处解析逻辑漂移。
constexpr char kMemeColumns[] =
    "id,file_name,format,width,height,created_at,last_used_at,use_count";

// 按 kMemeColumns 的顺序读取当前行。
MemeRecord readMemeRecord(const QSqlQuery &query)
{
    MemeRecord record;
    record.id = query.value(0).toString();
    record.fileName = query.value(1).toString();
    record.format = query.value(2).toString();
    record.width = query.value(3).toInt();
    record.height = query.value(4).toInt();
    record.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
    record.lastUsedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
    record.useCount = query.value(7).toInt();
    return record;
}
}

// 创建图库目录、打开固定连接名的 SQLite 数据库并执行 schema 检查。
bool DatabaseService::open(const QString &galleryPath, QString *error)
{
    QDir directory(galleryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (error)
            *error = AppStrings::galleryCreateFailed();
        return false;
    }
    if (!QFileInfo(directory.absolutePath()).isWritable()) {
        if (error)
            *error = AppStrings::galleryNotWritable();
        return false;
    }

    close();
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(directory.filePath(QStringLiteral("memeservant2.db")));
    if (!database.open()) {
        if (error)
            *error = AppStrings::sqliteOpenFailed(database.lastError().text());
        LogService::instance().error(QStringLiteral("SQLite连接失败"));
        return false;
    }
    m_galleryPath = directory.absolutePath();
    QSqlQuery query(database);
    query.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return ensureSchema(error);
}

// 关闭并移除固定连接，避免下一次切换图库复用旧连接。
void DatabaseService::close()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::database(m_connectionName).close();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_galleryPath.clear();
}

// 返回服务持有的连接对象；调用方只在主线程使用该连接。
QSqlDatabase DatabaseService::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

// 创建 schema_version，并将空数据库一次性迁移到当前配置的版本。
bool DatabaseService::ensureSchema(QString *error)
{
    QSqlDatabase databaseHandle = database();
    QSqlQuery query(databaseHandle);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version(version INTEGER NOT NULL)"))) {
        if (error)
            *error = AppStrings::databaseInitializeFailed(query.lastError().text());
        return false;
    }
    int version = 0;
    if (query.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1")) && query.next())
        version = query.value(0).toInt();

    // 版本为 0 且已经存在业务表，说明数据库缺少可信 schema 标记。
    if (version == 0 && !query.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='memes'"))) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    if (version == 0 && query.next() && query.value(0).toInt() > 0) {
        if (error) {
            *error = AppStrings::missingSchemaVersion();
        }
        return false;
    }
    if (version >= MEMESERVANT2_DB_SCHEMA_VERSION)
        return true;

    // 空库首次迁移必须先备份，再在一个事务中创建全部表和版本记录。
    if (!createBackup(MEMESERVANT2_DB_SCHEMA_VERSION, error))
        return false;
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE TRANSACTION"))) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    bool success = query.exec(QStringLiteral(
        "CREATE TABLE memes("
        "id TEXT PRIMARY KEY,"
        "file_name TEXT UNIQUE NOT NULL,"
        "format TEXT NOT NULL,"
        "width INTEGER NOT NULL,"
        "height INTEGER NOT NULL,"
        "created_at TEXT NOT NULL,"
        "last_used_at TEXT,"
        "use_count INTEGER NOT NULL DEFAULT 0)")) &&
        query.exec(QStringLiteral(
        "CREATE TABLE nicknames("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "meme_id TEXT NOT NULL REFERENCES memes(id) ON DELETE CASCADE,"
        "nickname TEXT NOT NULL,"
        "normalized TEXT NOT NULL,"
        "UNIQUE(meme_id, normalized))")) &&
        query.exec(QStringLiteral("DELETE FROM schema_version")) &&
        query.prepare(QStringLiteral("INSERT INTO schema_version(version) VALUES(:version)"));
    if (success) {
        query.bindValue(QStringLiteral(":version"), MEMESERVANT2_DB_SCHEMA_VERSION);
        success = query.exec();
    }
    if (success)
        success = query.exec(QStringLiteral("COMMIT"));
    else
        query.exec(QStringLiteral("ROLLBACK"));
    if (!success) {
        if (error)
            *error = AppStrings::migrationFailed(query.lastError().text());
        LogService::instance().error(QStringLiteral("Schema迁移失败"));
        return false;
    }
    LogService::instance().info(
        QStringLiteral("数据库schema升级到版本%1").arg(MEMESERVANT2_DB_SCHEMA_VERSION));
    return true;
}

// 在迁移前把现有数据库复制到 .backup，便于人工恢复。
bool DatabaseService::createBackup(int targetVersion, QString *error)
{
    QDir directory(m_galleryPath + QStringLiteral("/.backup"));
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("无法创建数据库备份目录。");
        return false;
    }
    const QString source = m_galleryPath + QStringLiteral("/memeservant2.db");
    const QString destination = directory.filePath(QStringLiteral(
        "memeservant2-before-schema-v%1-%2.db")
        .arg(targetVersion).arg(QDateTime::currentDateTimeUtc().toSecsSinceEpoch()));
    if (QFileInfo::exists(source) && !QFile::copy(source, destination)) {
        if (error)
            *error = AppStrings::migrationBackupFailed();
        return false;
    }
    LogService::instance().info(QStringLiteral("已完成迁移前数据库备份"));
    return true;
}

// 查询并转换 memes 表中的全部记录。
QVector<MemeRecord> DatabaseService::memes()
{
    QVector<MemeRecord> records;
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("SELECT ") + QLatin1String(kMemeColumns) +
                    QStringLiteral(" FROM memes ORDER BY id"))) {
        LogService::instance().error(QStringLiteral("读取表情包索引失败"));
        return records;
    }
    while (query.next())
        records.append(readMemeRecord(query));
    return records;
}

// 按主键查询单条记录；调用方用它取得文件名和已存尺寸，避免重新解码原图。
bool DatabaseService::findMeme(const QString &memeId, MemeRecord *record) const
{
    if (!record)
        return false;
    QSqlQuery query(database());
    if (!query.prepare(QStringLiteral("SELECT ") + QLatin1String(kMemeColumns) +
                       QStringLiteral(" FROM memes WHERE id=:id"))) {
        LogService::instance().error(QStringLiteral("读取表情包索引失败"));
        return false;
    }
    query.bindValue(QStringLiteral(":id"), memeId);
    if (!query.exec()) {
        LogService::instance().error(QStringLiteral("读取表情包索引失败"));
        return false;
    }
    // 记录不存在是正常业务分支（原图已被外部删除），不写错误日志。
    if (!query.next())
        return false;
    *record = readMemeRecord(query);
    return true;
}

// 查询 nickname 表并按 meme_id 组装内存索引。
QHash<QString, QVector<NicknameRecord>> DatabaseService::nicknames()
{
    QHash<QString, QVector<NicknameRecord>> result;
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("SELECT id,meme_id,nickname,normalized FROM nicknames ORDER BY id")))
        return result;
    while (query.next()) {
        NicknameRecord record;
        record.id = query.value(0).toLongLong();
        record.memeId = query.value(1).toString();
        record.nickname = query.value(2).toString();
        record.normalized = query.value(3).toString();
        result[record.memeId].append(record);
    }
    return result;
}

// 先插入表情包主记录，再复用 replaceNicknames 写入并校验 nickname。
bool DatabaseService::saveMeme(MemeRecord &meme, const QStringList &nicknames, QString *error)
{
    QSqlQuery query(database());
    if (!query.prepare(QStringLiteral(
        "INSERT INTO memes(id,file_name,format,width,height,created_at,last_used_at,use_count) "
        "VALUES(:id,:file,:format,:width,:height,:created,NULL,0)"))) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    query.bindValue(QStringLiteral(":id"), meme.id);
    query.bindValue(QStringLiteral(":file"), meme.fileName);
    query.bindValue(QStringLiteral(":format"), meme.format);
    query.bindValue(QStringLiteral(":width"), meme.width);
    query.bindValue(QStringLiteral(":height"), meme.height);
    query.bindValue(QStringLiteral(":created"), meme.createdAt.toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        if (error)
            *error = AppStrings::saveMemeFailed();
        return false;
    }
    return replaceNicknames(meme.id, nicknames, error);
}

// 清洗、去重 nickname 后在一个事务中删除旧值并插入新值。
bool DatabaseService::replaceNicknames(const QString &memeId, const QStringList &nicknames, QString *error)
{
    const QStringList cleaned = NicknameUtils::clean(nicknames.join(QLatin1Char('\n')));
    if (cleaned.isEmpty()) {
        if (error)
            *error = QStringLiteral("至少需要一个有效nickname。");
        return false;
    }
    QSet<QString> unique;
    // 规范化后去重，确保数据库唯一索引和界面校验使用同一规则。
    for (const QString &item : cleaned)
        unique.insert(NicknameUtils::normalize(item));
    if (unique.size() != cleaned.size()) {
        if (error)
            *error = QStringLiteral("同一表情包内nickname不能重复。");
        return false;
    }

    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE TRANSACTION")))
        return false;
    // 先删除旧集合，再逐条插入新集合；任一步骤失败都会回滚。
    bool success = query.prepare(QStringLiteral("DELETE FROM nicknames WHERE meme_id=:meme"));
    if (success) {
        query.bindValue(QStringLiteral(":meme"), memeId);
        success = query.exec();
    }
    if (success)
        success = query.prepare(QStringLiteral(
            "INSERT INTO nicknames(meme_id,nickname,normalized) VALUES(:meme,:name,:normal)"));
    for (const QString &item : cleaned) {
        if (!success)
            break;
        query.bindValue(QStringLiteral(":meme"), memeId);
        query.bindValue(QStringLiteral(":name"), item);
        query.bindValue(QStringLiteral(":normal"), NicknameUtils::normalize(item));
        success = query.exec();
    }
    if (success)
        success = query.exec(QStringLiteral("COMMIT"));
    else
        query.exec(QStringLiteral("ROLLBACK"));
    if (!success && error)
        *error = AppStrings::nicknameSaveFailed();
    return success;
}

// 以 UTC ISO 时间更新最近使用时间，并递增使用次数。
bool DatabaseService::updateUsage(const QString &memeId, QString *error)
{
    QSqlQuery query(database());
    if (!query.prepare(QStringLiteral(
        "UPDATE memes SET last_used_at=:used, use_count=use_count+1 WHERE id=:id"))) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    query.bindValue(QStringLiteral(":used"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":id"), memeId);
    if (query.exec())
        return true;
    if (error)
        *error = query.lastError().text();
    return false;
}

// 根据数据库文件名推导原图路径和 .thumbnails 下的 PNG 路径。
QStringList DatabaseService::memeFilePaths(const QString &memeId) const
{
    QStringList paths;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT file_name FROM memes WHERE id=:id"));
    query.bindValue(QStringLiteral(":id"), memeId);
    if (query.exec() && query.next()) {
        const QString fileName = query.value(0).toString();
        paths.append(m_galleryPath + QLatin1Char('/') + fileName);
        paths.append(m_galleryPath + QStringLiteral("/.thumbnails/") +
                     QFileInfo(fileName).completeBaseName() + QStringLiteral(".png"));
    }
    return paths;
}

// 只删除索引记录；文件删除由上层先移动、后清理以保证可回滚。
bool DatabaseService::deleteMeme(const QString &memeId, QString *error)
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral("DELETE FROM memes WHERE id=:id"));
    query.bindValue(QStringLiteral(":id"), memeId);
    if (!query.exec()) {
        if (error)
            *error = AppStrings::deleteRecordFailed();
        return false;
    }
    return true;
}
