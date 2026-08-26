// 声明从旧图库或 ZIP 归档导入表情包数据的服务。
#ifndef STORAGE_IMPORTSERVICE_H
#define STORAGE_IMPORTSERVICE_H

// 后台合并另一个 MemeServant2 图库，源库保持只读。
#include <QString>

struct ImportResult
{
    int memes = 0;
    int nicknames = 0;
    int failed = 0;
    QString error;
};

class ImportService
{
public:
    /// 根据 .db 或 .zip 后缀选择直接导入或先解压再导入。
    static ImportResult importSource(const QString &currentGallery, const QString &sourcePath);
    /// 只读打开源数据库，将文件和索引合并到当前图库。
    static ImportResult importGallery(const QString &currentGallery, const QString &sourceLibraryPath);

private:
    /// 在归档目录或其一级子目录中查找图库数据库。
    static QString locateDatabase(const QString &sourceLibraryPath);
    /// 在允许的相对路径候选位置中查找源图片。
    static QString resolveSourceImage(const QString &sourceLibraryPath, const QString &fileName);
};

#endif
