// 声明基于 nickname 的模糊搜索与稳定排序引擎。
#ifndef CORE_SEARCHENGINE_H
#define CORE_SEARCHENGINE_H

// 根据指南实现完全匹配到模糊子序列的四级稳定排序。
#include <QVector>
#include "core/models.h"

struct SearchResult
{
    MemeRecord meme;
    QVector<NicknameRecord> nicknames;
    NicknameRecord bestMatch;
    int matchLevel = 5;
};

class SearchEngine
{
public:
    /// 替换当前内存中的表情包和 nickname 索引快照。
    void setRecords(const QVector<MemeRecord> &memes,
                    const QHash<QString, QVector<NicknameRecord>> &nicknames);
    /// 按精确、前缀、包含和子序列匹配等级返回稳定排序结果。
    QVector<SearchResult> search(const QString &query, int limit) const;

private:
    /// 判断 query 的字符是否按顺序出现在 normalizedText 中。
    static bool fuzzyMatch(const QString &normalizedText, const QString &query);

    QVector<MemeRecord> m_memes;
    QHash<QString, QVector<NicknameRecord>> m_nicknames;
};

#endif
