// 集中定义界面配色变量与全局QSS；图标按当前配色方案着色。
#ifndef UI_APPSTYLE_H
#define UI_APPSTYLE_H

#include <QColor>
#include <QIcon>
#include <QString>

namespace AppStyle
{
/// 当前调色板是否为深色方案。
bool isDarkPalette();
/// 图标应使用的前景色（跟随系统配色，深浅色模式自动切换）。
QColor iconColor();
/// 读取qrc中的SVG并按iconColor()重新着色；渲染失败时退化为原始QIcon。
QIcon themedIcon(const QString &resourcePath);
/// 根据配色方案生成并应用全局QSS（按钮圆角、边框、输入控件配色等）。
void applyTheme();
}

#endif