// nickname规则集中在这里，避免各界面各自解释换行与空白。
#include "core/nicknameutils.h"

// 保留每个非空行的用户文本，只去掉行首尾空白。
QStringList NicknameUtils::clean(const QString &input)
{
    QStringList result;
    const QStringList lines = input.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString cleaned = line.trimmed();
        if (!cleaned.isEmpty())
            result.append(cleaned);
    }
    return result;
}

// 统一大小写和首尾空白，供唯一性检查与搜索比较使用。
QString NicknameUtils::normalize(const QString &nickname)
{
    return nickname.trimmed().toLower();
}
