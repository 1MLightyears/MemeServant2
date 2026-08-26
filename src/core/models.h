// 定义表情包、nickname 和搜索结果等核心领域数据结构。
#ifndef CORE_MODELS_H
#define CORE_MODELS_H

// 本文件定义贯穿存储、搜索与界面层的核心记录类型。
#include <QString>
#include <QDateTime>

struct MemeRecord
{
    QString id;
    QString fileName;
    QString format;
    int width = 0;
    int height = 0;
    QDateTime createdAt;
    QDateTime lastUsedAt;
    int useCount = 0;
};

struct NicknameRecord
{
    qint64 id = 0;
    QString memeId;
    QString nickname;
    QString normalized;
};

#endif
