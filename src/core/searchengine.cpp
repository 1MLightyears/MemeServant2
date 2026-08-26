// 搜索结果必须按匹配等级、昵称长度、Unicode顺序和内部ID稳定排序。
#include "core/searchengine.h"

#include <algorithm>

// 用数据库最新快照替换搜索引擎的内存数据。
void SearchEngine::setRecords(const QVector<MemeRecord> &memes,
                              const QHash<QString, QVector<NicknameRecord>> &nicknames)
{
    m_memes = memes;
    m_nicknames = nicknames;
}

// 用有序子序列匹配实现轻量模糊搜索，例如“bd”可以匹配“表情包的”。
bool SearchEngine::fuzzyMatch(const QString &normalizedText, const QString &query)
{
    qsizetype offset = 0;
    for (const QChar character : query) {
        offset = normalizedText.indexOf(character, offset);
        if (offset < 0)
            return false;
        ++offset;
    }
    return true;
}

// 计算每个表情包的最佳 nickname 匹配，并按匹配质量或最近使用时间排序。
QVector<SearchResult> SearchEngine::search(const QString &query, int limit) const
{
    QVector<SearchResult> results;
    const QString wanted = query.trimmed().toLower();
    // 先为每个表情包找出最佳 nickname，并保留其完整 nickname 列表供界面展示。
    for (const MemeRecord &meme : m_memes) {
        SearchResult current;
        current.meme = meme;
        current.nicknames = m_nicknames.value(meme.id);
        std::sort(current.nicknames.begin(), current.nicknames.end(),
                  [](const NicknameRecord &left, const NicknameRecord &right) {
                      return left.normalized < right.normalized;
                  });

        for (const NicknameRecord &nickname : std::as_const(current.nicknames)) {
            int level = 5;
            if (nickname.normalized == wanted)
                level = 1;
            else if (wanted.isEmpty() || nickname.normalized.startsWith(wanted))
                level = wanted.isEmpty() ? 4 : 2;
            else if (nickname.normalized.contains(wanted))
                level = 3;
            else if (!wanted.isEmpty() && fuzzyMatch(nickname.normalized, wanted))
                level = 4;

            if (level < current.matchLevel) {
                current.bestMatch = nickname;
                current.matchLevel = level;
            }
        }
        if (!current.nicknames.isEmpty())
            results.append(current);
    }

    // 有搜索词时按匹配质量排序；空搜索则按最近使用和创建时间排序。
    if (!wanted.isEmpty()) {
        std::stable_sort(results.begin(), results.end(),
                         [](const SearchResult &left, const SearchResult &right) {
                             if (left.matchLevel != right.matchLevel)
                                 return left.matchLevel < right.matchLevel;
                             const qsizetype leftLength = left.bestMatch.nickname.size();
                             const qsizetype rightLength = right.bestMatch.nickname.size();
                             if (leftLength != rightLength)
                                 return leftLength < rightLength;
                             if (left.bestMatch.normalized != right.bestMatch.normalized)
                                 return left.bestMatch.normalized.localeAwareCompare(right.bestMatch.normalized) < 0;
                             return left.meme.id < right.meme.id;
                         });
    } else {
        std::stable_sort(results.begin(), results.end(),
                         [](const SearchResult &left, const SearchResult &right) {
                             if (left.meme.lastUsedAt != right.meme.lastUsedAt)
                                 return left.meme.lastUsedAt > right.meme.lastUsedAt;
                             if (left.meme.createdAt != right.meme.createdAt)
                                 return left.meme.createdAt > right.meme.createdAt;
                             return left.meme.id < right.meme.id;
                         });
    }
    if (limit >= 0 && results.size() > limit)
        results.resize(limit);
    return results;
}
