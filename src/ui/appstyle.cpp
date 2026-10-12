// 全局样式实现：从系统调色板提取配色变量，再拼装QSS与主题化图标。
#include "ui/appstyle.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTemporaryDir>

namespace
{
// 用纯色替换位图的alpha通道以外的所有颜色（保留形状，只换颜色）。
QPixmap colorize(const QImage &source, const QColor &color)
{
    QImage result = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    painter.end();
    return QPixmap::fromImage(result);
}
}

namespace AppStyle
{
bool isDarkPalette()
{
    const QPalette palette = QApplication::palette();
    return palette.color(QPalette::Window).lightness() < 128;
}

QColor iconColor()
{
    // 深色模式用浅灰、浅色模式用深灰，保证与按钮底色有足够对比度。
    return isDarkPalette() ? QColor(0xE6, 0xE6, 0xE6) : QColor(0x33, 0x33, 0x33);
}

QIcon themedIcon(const QString &resourcePath)
{
    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid())
        return QIcon(resourcePath);
    // 64x64足够覆盖常见显示尺寸，配合Qt的高DPI缩放仍清晰。
    QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter, image.rect());
    painter.end();
    QIcon icon;
    icon.addPixmap(colorize(image, iconColor()));
    return icon;
}

// QSS的image无法走QIcon着色路径，把SVG按当前主题着色后写入磁盘供url()引用。
QString decideBySysTheme(const QString &resourcePath)
{
    // 同一来源在同一主题下只渲染一次；主题切换时全局QSS整体重设，缓存键自然分离。
    static QHash<QString, QString> cache;
    const QString cacheKey = resourcePath + (isDarkPalette() ? QStringLiteral("|dark")
                                                            : QStringLiteral("|light"));
    if (const auto it = cache.constFind(cacheKey); it != cache.constEnd())
        return it.value();

    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid())
        return resourcePath;

    // 着色结果放在用户临时目录的专属子目录，进程结束后由系统清理。
    static QTemporaryDir tempRoot(
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("memeservant2-styled-XXXXXX")));
    if (!tempRoot.isValid())
        return resourcePath;

    const QString themeSuffix = isDarkPalette() ? QStringLiteral("dark") : QStringLiteral("light");
    const QString baseName = QFileInfo(resourcePath).completeBaseName();
    const QString outputPath = tempRoot.filePath(baseName + QStringLiteral("-") + themeSuffix +
                                                 QStringLiteral(".png"));
    // 64x64与themedIcon保持一致，QSS里会按需缩小。
    QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter, image.rect());
    painter.end();
    if (!colorize(image, iconColor()).save(outputPath, "PNG"))
        return resourcePath;
    // QSS的url()按CSS规则解析，反斜杠会被当转义字符吞掉，必须保持正斜杠。
    const QString url = outputPath;
    cache.insert(cacheKey, url);
    return url;
}

void applyTheme()
{
    const bool dark = isDarkPalette();
    // 配色变量：所有控件样式都从这几个变量取值，避免散落的魔法色值。
    const QString field = dark ? QStringLiteral("#3A3A3A") : QStringLiteral("#FFFFFF");
    const QString text = dark ? QStringLiteral("#E6E6E6") : QStringLiteral("#1F1F1F");
    const QString border = dark ? QStringLiteral("#555555") : QStringLiteral("#BFBFBF");
    const QString hover = dark ? QStringLiteral("#454545") : QStringLiteral("#F0F0F0");
    const QString pressed = dark ? QStringLiteral("#505050") : QStringLiteral("#E0E0E0");

    const QString qss = QStringLiteral(
        // 按控件类型精确着色，不用QWidget通配底色——避免影响QuickBar、Toast等
        // 自绘半透明窗口（它们的局部样式表优先级更高，但子控件会被通配规则误染）。
        "QLineEdit, QComboBox, QKeySequenceEdit, QSpinBox, QDoubleSpinBox { background: %2; color: %1;"
        "  border: 1px solid %3; border-radius: 4px; padding: 2px 6px; }"
        "QLineEdit:focus, QComboBox:focus, QKeySequenceEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: %3; }"
        "QPushButton { background: %2; color: %1; border: 1px solid %3;"
        "  border-radius: 6px; padding: 4px 14px; }"
        "QPushButton:hover { background: %4; }"
        "QPushButton:pressed { background: %5; }"
        // 图标按钮不带文字，去掉内边距避免图标被挤出固定尺寸。
        "QPushButton[iconButton=\"true\"] { padding: 0; }"
        "QGroupBox { border: 1px solid %3; border-radius: 6px; margin-top: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QTabWidget::pane { border: 1px solid %3; border-radius: 4px; }"
        "QToolTip { background: %2; color: %1; border: 1px solid %3; padding: 2px; }"
        // QSpinBox 一旦被样式表命中就整体交给样式引擎绘制；必须显式声明上下按钮
        // 的子控件几何，否则默认布局与原生风格错位，上键点击区会被文本框覆盖。
        "QSpinBox::up-button, QSpinBox::down-button {"
        "  subcontrol-origin: border; width: 18px; border: none; background: %2;"
        "}"
        "QSpinBox::up-button { subcontrol-position: top right; }"
        "QSpinBox::down-button { subcontrol-position: bottom right; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: %4; }"
        "QSpinBox::up-button:pressed, QSpinBox::down-button:pressed { background: %5; }"
        "QSpinBox::up-arrow { image: url(%6); width: 8px; height: 8px; }"
        "QSpinBox::down-arrow { image: url(%7); width: 8px; height: 8px; }"
        // QDoubleSpinBox与QSpinBox共享同一套子控件规则，保证两种调节框观感一致。
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  subcontrol-origin: border; width: 18px; border: none; background: %2;"
        "}"
        "QDoubleSpinBox::up-button { subcontrol-position: top right; }"
        "QDoubleSpinBox::down-button { subcontrol-position: bottom right; }"
        "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background: %4; }"
        "QDoubleSpinBox::up-button:pressed, QDoubleSpinBox::down-button:pressed { background: %5; }"
        "QDoubleSpinBox::up-arrow { image: url(%6); width: 8px; height: 8px; }"
        "QDoubleSpinBox::down-arrow { image: url(%7); width: 8px; height: 8px; }"
        // QComboBox被QSS命中后下拉箭头也交给样式引擎；显式声明几何与图标，避免旧版默认箭头。
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right;"
        "  width: 18px; border: none; background: transparent; }"
        "QComboBox::drop-down:hover { background: %4; border-radius: 0 4px 4px 0; }"
        "QComboBox::drop-down:pressed { background: %5; border-radius: 0 4px 4px 0; }"
        "QComboBox::down-arrow { image: url(%7); width: 8px; height: 8px; }")
        .arg(text, field, border, hover, pressed,
             decideBySysTheme(QStringLiteral(":/icons/up.svg")),
             decideBySysTheme(QStringLiteral(":/icons/down.svg")));
    // 启动时和首次构造设置窗口时都会调用这里，而 Qt 不比较内容：同一条样式表再设一次
    // 也会让全部控件重新 polish。只在内容真正变化（含深浅色切换）时才应用。
    static QString appliedStyleSheet;
    if (qss == appliedStyleSheet)
        return;
    appliedStyleSheet = qss;
    qApp->setStyleSheet(qss);
}
}