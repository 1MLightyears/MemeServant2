// 声明在后台线程生成图库缩略图缓存的工作对象。
#ifndef THUMBNAIL_THUMBNAILLISTENER_H
#define THUMBNAIL_THUMBNAILLISTENER_H

// 后台线程生成静态 PNG 缩略图，GIF 固定使用第一帧。
#include <QObject>
#include <QString>
#include <QVector>
#include "core/models.h"

class ThumbnailWorker : public QObject
{
    Q_OBJECT
public:
    /// 为缺少缓存的源图生成固定最长边的 PNG 缩略图。
    void generate(const QVector<MemeRecord> &records, const QString &galleryPath, int maximumSide);

signals:
    void generated(const QString &memeId);
};

#endif
