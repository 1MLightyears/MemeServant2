// 声明 SQLite 图库数据库的打开、迁移、查询和修改服务。
#ifndef STORAGE_DATABASESERVICE_H
#define STORAGE_DATABASESERVICE_H

// 所有主线程数据库操作通过该服务执行，schema版本和迁移备份集中管理。
#include <QHash>
#include <QString>
#include <QVector>
#include "core/models.h"

class QSqlDatabase;

class DatabaseService
{
public:
    /// 创建或打开图库目录中的 SQLite 数据库并确保 schema 可用。
    bool open(const QString &galleryPath, QString *error);
    /// 关闭数据库连接并清空当前图库路径。
    void close();
    QString galleryPath() const { return m_galleryPath; }

    /// 查询全部表情包索引记录。
    QVector<MemeRecord> memes();
    /// 查询单个表情包索引记录；记录不存在或查询失败时返回 false。
    bool findMeme(const QString &memeId, MemeRecord *record) const;
    /// 查询全部 nickname，并按 meme_id 分组。
    QHash<QString, QVector<NicknameRecord>> nicknames();
    /// 插入表情包索引并写入其初始 nickname 集合。
    bool saveMeme(MemeRecord &meme, const QStringList &nicknames, QString *error);
    /// 在事务中校验并替换一个表情包的 nickname 集合。
    bool replaceNicknames(const QString &memeId, const QStringList &nicknames, QString *error);
    /// 更新表情包最近使用时间和使用次数。
    bool updateUsage(const QString &memeId, QString *error);
    /// 返回原图和缩略图的完整路径。
    QStringList memeFilePaths(const QString &memeId) const;
    /// 删除表情包数据库记录；原图和缩略图由应用控制器负责清理。
    bool deleteMeme(const QString &memeId, QString *error);

private:
    /// 创建当前 schema，并在旧数据库缺少版本信息时拒绝继续。
    bool ensureSchema(QString *error);
    /// 在 schema 迁移前复制数据库文件。
    bool createBackup(int targetVersion, QString *error);
    /// 返回当前连接名对应的 SQLite 连接。
    QSqlDatabase database() const;

    QString m_galleryPath;
    QString m_connectionName = QStringLiteral("primary");
};

#endif
