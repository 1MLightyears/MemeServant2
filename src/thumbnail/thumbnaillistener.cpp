// 缓存文件与源图同名但扩展名固定PNG；小于目标尺寸时不放大。
#include "thumbnail/thumbnaillistener.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QThread>

#include "storage/logservice.h"

// 逐个读取缺失源图，按最长边缩小后写入固定命名的 PNG 缓存。
void ThumbnailWorker::generate(const QVector<MemeRecord> &records, const QString &galleryPath, int maximumSide)
{
    QDir cacheDirectory(galleryPath + QStringLiteral("/.thumbnails"));
    if (!cacheDirectory.exists())
        cacheDirectory.mkpath(QStringLiteral("."));
    for (const MemeRecord &record : records) {
        if (QThread::currentThread()->isInterruptionRequested())
            break;
        const QString source = galleryPath + QLatin1Char('/') + record.fileName;
        const QString target = cacheDirectory.filePath(QString(record.fileName).section(QLatin1Char('.'), 0, -2)
                                                       + QStringLiteral(".png"));
        if (!QFile::exists(source) || QFile::exists(target))
            continue;
        QImageReader reader(source);
        reader.setAutoTransform(true);
        if (record.format == QLatin1String("gif"))
            reader.setFormat(QByteArrayLiteral("GIF"));
        QImage image = reader.read();
        if (image.isNull()) {
            LogService::instance().warning(QStringLiteral("缩略图解码失败"));
            continue;
        }
        QSize displaySize = image.size();
        displaySize.scale(maximumSide, maximumSide, Qt::KeepAspectRatio);
        if (displaySize.width() < image.width() || displaySize.height() < image.height())
            image = image.scaled(displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!image.save(target, "PNG"))
            LogService::instance().warning(QStringLiteral("缩略图写入失败"));
    }
    emit finished();
}
