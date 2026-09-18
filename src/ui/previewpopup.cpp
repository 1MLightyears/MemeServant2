// 超大图按屏幕缩小，低分辨率图保持100%显示；预览贴边并限制在屏幕内。
#include "ui/previewpopup.h"

#include <QCursor>
#include <QGuiApplication>
#include <QLabel>
#include <QMovie>
#include <QFileInfo>
#include <QImageReader>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>

#include "core/appstrings.h"

// 创建不抢焦点且不接收鼠标事件的无边框预览窗口。
// WindowDoesNotAcceptFocus 让原生窗口带 WS_EX_NOACTIVATE，避免预览弹出时把快捷栏挤成失活状态。
PreviewPopup::PreviewPopup(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                           Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_label);
}

// 预览窗口失活后关闭，其他事件交给 QWidget 默认处理。
bool PreviewPopup::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate)
        closePreview();
    return QWidget::event(event);
}

// 清理旧动画，按屏幕可用区域缩放原图，并贴在触发缩略图旁边显示。
void PreviewPopup::showOriginal(const QString &sourcePath, const QRect &anchorRect)
{
    if (m_movie) {
        m_movie->stop();
        m_label->setMovie(nullptr);
        delete m_movie;
        m_movie = nullptr;
    }
    m_label->clear();

    const QMargins margins = layout()->contentsMargins();
    // 让窗口大小严格包住内容和固定边距，避免预览周围出现额外空白。
    const auto resizeForContent = [this, margins](const QSize &contentSize) {
        const QSize boundedSize = contentSize.expandedTo(QSize(1, 1));
        m_label->setFixedSize(boundedSize);
        setFixedSize(boundedSize + QSize(margins.left() + margins.right(),
                                         margins.top() + margins.bottom()));
    };

    // 缩放和定位都跟随触发缩略图所在屏幕；锚点无效时退回鼠标/父窗口屏幕。
    const QPoint anchorCenter = anchorRect.isValid() ? anchorRect.center() : QCursor::pos();
    QScreen *targetScreen = QGuiApplication::screenAt(anchorCenter);
    if (!targetScreen && parentWidget())
        targetScreen = parentWidget()->screen();
    if (!targetScreen)
        targetScreen = screen();
    const QRect availableRect = targetScreen ? targetScreen->availableGeometry()
                                             : QRect(0, 0, 1280, 720);
    QSize available = availableRect.size() * 0.9;
    available -= QSize(margins.left() + margins.right(), margins.top() + margins.bottom());

    if (!QFileInfo::exists(sourcePath)) {
        m_label->setStyleSheet(QStringLiteral("color:#ddd;background:#222;border-radius:8px"));
        m_label->setText(AppStrings::missingMemeText());
        resizeForContent(m_label->sizeHint());
    } else {
        m_label->setStyleSheet({});
        QImageReader reader(sourcePath);
        reader.setAutoTransform(true);
        QSize size = reader.size();
        if (!size.isValid()) {
            QImage image(sourcePath);
            size = image.size();
        }
        if (size.width() > available.width() || size.height() > available.height())
            size.scale(available, Qt::KeepAspectRatio);
        if (sourcePath.endsWith(QLatin1String(".gif"), Qt::CaseInsensitive)) {
            m_movie = new QMovie(sourcePath, QByteArray(), this);
            m_movie->setScaledSize(size);
            m_label->setMovie(m_movie);
            m_movie->start();
            m_movie->jumpToFrame(0);
            resizeForContent(size);
        } else {
            const QImage image = reader.read();
            const QPixmap pixmap = QPixmap::fromImage(image);
            if (!pixmap.isNull()) {
                const QPixmap displayed = pixmap.scaled(size, Qt::KeepAspectRatio,
                                                        Qt::SmoothTransformation);
                m_label->setPixmap(displayed);
                resizeForContent(displayed.size());
            } else {
                resizeForContent(size);
            }
        }
    }

    // 默认贴在缩略图右侧；右侧放不下时翻到左侧，最后统一夹紧到屏幕可用区域。
    const QRect anchor = anchorRect.isValid() ? anchorRect : QRect(anchorCenter, QSize(1, 1));
    const int gap = 8;
    QPoint target(anchor.right() + 1 + gap, anchor.top());
    if (target.x() + width() - 1 > availableRect.right())
        target.setX(anchor.left() - width() - gap);
    if (target.y() + height() - 1 > availableRect.bottom())
        target.setY(anchor.bottom() + 1 - height());
    target.setX(qBound(availableRect.left() + gap, target.x(),
                       qMax(availableRect.left() + gap, availableRect.right() - width() - gap + 1)));
    target.setY(qBound(availableRect.top() + gap, target.y(),
                       qMax(availableRect.top() + gap, availableRect.bottom() - height() - gap + 1)));
    move(target);
    show();
    raise();
}

// 停止 GIF、清空 QLabel 并隐藏窗口，下一次显示可重新加载资源。
void PreviewPopup::closePreview()
{
    if (m_movie)
        m_movie->stop();
    m_label->setMovie(nullptr);
    m_label->setPixmap(QPixmap());
    hide();
}
