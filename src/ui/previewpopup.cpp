// 超大图按屏幕缩小，低分辨率图保持100%显示。
#include "ui/previewpopup.h"

#include <QLabel>
#include <QMovie>
#include <QFileInfo>
#include <QImageReader>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>

#include "core/appstrings.h"

// 创建不抢焦点且不接收鼠标事件的无边框预览窗口。
PreviewPopup::PreviewPopup(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
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

// 清理旧动画，按屏幕可用区域缩放原图，并在窗口右侧显示。
void PreviewPopup::showOriginal(const QString &sourcePath)
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
        const QScreen *targetScreen = parentWidget() ? parentWidget()->screen() : screen();
        QSize available = targetScreen->availableGeometry().size() * 0.9;
        available -= QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
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
    move(parentWidget()->geometry().topRight() + QPoint(8, 0));
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
