// 声明 nickname 的清洗和规范化工具函数。
#ifndef CORE_NICKNAMEUTILS_H
#define CORE_NICKNAMEUTILS_H

// 提供nickname输入的统一清洗与比较规则。
#include <QStringList>

namespace NicknameUtils {
/// 按行拆分输入，去除首尾空白和空行。
QStringList clean(const QString &input);
/// 生成用于大小写不敏感比较和搜索的规范化 nickname。
QString normalize(const QString &nickname);
}

#endif
