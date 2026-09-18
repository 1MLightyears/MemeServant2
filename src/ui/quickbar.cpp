// 快捷栏关联窗口不会导致主浮窗误关闭；空查询按最近使用排序。
#include "ui/quickbar.h"

#include <QCursor>
#include <QEvent>
#include <QEnterEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QGuiApplication>
#include <QApplication>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>

#include "storage/appconfig.h"
#include "core/appstrings.h"
#include "platforms/win/wincaretlocator.h"
#include "platforms/win/winwindoweffects.h"
#include "ui/nicknameeditdialog.h"
#include "ui/previewpopup.h"

class QuickCandidate : public QFrame
{
    Q_OBJECT
public:
    // 创建固定尺寸的候选卡片，图片铺底，nickname 标签和选中框覆盖在上层。
    QuickCandidate(const SearchResult &result, int displaySize, QWidget *parent)
        : QFrame(parent), m_result(result)
    {
        setFixedSize(displaySize, displaySize);
        m_image = new QLabel(this);
        m_image->setGeometry(0, 0, displaySize, displaySize);
        m_image->setAlignment(Qt::AlignCenter);
        m_image->setScaledContents(false);
        m_image->setStyleSheet(QStringLiteral("background:rgba(255,255,255,18);border-radius:9px"));
        m_text = new QLabel(this);
        m_text->setTextInteractionFlags(Qt::NoTextInteraction);
        m_text->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
        m_text->setWordWrap(false);
        m_text->setStyleSheet(QStringLiteral(
            "QLabel{background:rgba(0,0,0,168);color:white;padding:4px 6px;"
            "border-bottom-left-radius:9px;border-bottom-right-radius:9px}"));
        setStyleSheet(QStringLiteral("background:rgba(38,38,38,214);border-radius:9px"));
        m_selectionFrame = new QFrame(this);
        m_selectionFrame->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_selectionFrame->setGeometry(rect());
        setActive(false);
        setCursor(Qt::PointingHandCursor);
    }

    void setText(const QString &html) { m_text->setText(html); }
    void setPixmap(const QPixmap &pixmap) { m_image->setPixmap(pixmap); }
    void setActive(bool active)
    {
        m_selectionFrame->setStyleSheet(
            active ? QStringLiteral("background:transparent;border:3px solid rgba(64,156,255,245);"
                                    "border-radius:9px")
                   : QStringLiteral("background:transparent;border:3px solid transparent;"
                                    "border-radius:9px"));
        m_selectionFrame->raise();
    }
    const SearchResult &result() const { return m_result; }

signals:
    void clicked();
    void mouseEntered();
    void mouseLeft();

protected:
    // 根据卡片大小重新放置底部 nickname 标签和选中框。
    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        const int labelHeight = qBound(24, m_text->sizeHint().height(), height() / 2);
        m_text->setGeometry(0, height() - labelHeight, width(), labelHeight);
        m_selectionFrame->setGeometry(rect());
        m_selectionFrame->raise();
    }

    // 左键点击候选卡片时发出 clicked 信号。
    void mousePressEvent(QMouseEvent *event) override
    {
        QFrame::mousePressEvent(event);
        if (event->button() == Qt::LeftButton)
            emit clicked();
    }

    // 鼠标进入卡片时通知快捷栏更新当前项。
    void enterEvent(QEnterEvent *event) override
    {
        QFrame::enterEvent(event);
        emit mouseEntered();
    }

    // 鼠标离开卡片时停止该项的延迟预览。
    void leaveEvent(QEvent *event) override
    {
        QFrame::leaveEvent(event);
        emit mouseLeft();
    }

private:
    SearchResult m_result;
    QLabel *m_image = nullptr;
    QLabel *m_text = nullptr;
    QFrame *m_selectionFrame = nullptr;
};

#include "ui/quickbar.moc"

// 创建搜索框、候选网格和延迟预览对象。
QuickBar::QuickBar(QWidget *parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("root"));
    // 保留透明度，同时提供足够深的底色，避免快捷栏叠在白色窗口上时白字失去对比度。
    setStyleSheet(QStringLiteral(
        "QFrame#root{background-color:rgba(24,24,28,224);"
        "border:1px solid rgba(255,255,255,48);border-radius:13px}"
        "QWidget#candidateHost{background:transparent}"
        "QLineEdit{background:rgba(0,0,0,96);border:1px solid rgba(255,255,255,32);"
        "border-radius:9px;color:#fff;padding:8px}"
        "QLabel{background:transparent;color:#fff}"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 9, 10, 9);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(AppStrings::searchPlaceholder());
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &QuickBar::rebuild);
    m_candidateHost = new QWidget(this);
    m_candidateHost->setObjectName(QStringLiteral("candidateHost"));
    m_grid = new QGridLayout(m_candidateHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(6);
    m_grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(m_search); layout->addWidget(m_candidateHost);
    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, [this]() {
        if (!m_previewAllowed || !isVisible() || m_current < 0 || m_current >= m_results.size())
            return;
        // 以当前卡片为锚点，让预览贴在缩略图旁而不是整个快捷栏旁。
        const QuickCandidate *card = m_current < m_cards.size() ? m_cards.at(m_current) : nullptr;
        const QRect anchor = card ? QRect(card->mapToGlobal(QPoint(0, 0)), card->size())
                                  : QRect(QCursor::pos(), QSize(1, 1));
        m_preview->showOriginal(sourcePath(m_results.at(m_current).meme), anchor);
    });
    m_preview = new PreviewPopup(this);
}

// 仅更新配置快照；候选卡片由下一次 rebuild 按新配置重建。
void QuickBar::applyConfig(const AppConfig &config)
{
    m_config = config;
}

// 更新搜索引擎数据，窗口可见时立即重建当前查询结果。
void QuickBar::updateRecords(const QVector<MemeRecord> &memes,
                             const QHash<QString, QVector<NicknameRecord>> &nicknames)
{
    m_engine.setRecords(memes, nicknames);
    if (isVisible())
        rebuild(m_search->text());
}

// 计算 caret 附近位置、重置搜索状态并显示快捷栏。
void QuickBar::openAt(const AppConfig &config, quintptr previousForeground)
{
    applyConfig(config);
    m_foregroundWindow = previousForeground;
    const CaretPoint caret = locateWindowsCaret();
    QPoint point(caret.success ? caret.x : QCursor::pos().x(), caret.success ? caret.y : QCursor::pos().y());
    adjustSize();
    QSize size = this->size();
    if (QScreen *screen = QGuiApplication::screenAt(point)) {
        QRect area = screen->availableGeometry();
        if (point.x() + size.width() > area.right())
            point.setX(area.right() - size.width());
        if (point.y() + size.height() > area.bottom())
            point.setY(qMax(point.y() - size.height() - 24, area.top()));
    }
    move(point + QPoint(12, 12));
    m_previewAllowed = false;
    m_previewTimer.stop();
    m_preview->closePreview();
    m_search->clear();
    rebuild(QString());
    show(); raise(); activateWindow();
    m_previewAllowed = true;
    setCurrent(m_current);
    m_search->setFocus();
}

// 将数据库中的文件名拼接为当前图库下的绝对/规范路径。
QString QuickBar::sourcePath(const MemeRecord &record) const
{
    return m_config.resolvedGalleryPath(QCoreApplication::applicationDirPath()) + QLatin1Char('/') + record.fileName;
}

// 返回与 ThumbnailWorker 输出规则一致的缩略图缓存路径。
QString QuickBar::thumbnailPath(const MemeRecord &record) const
{
    return m_config.resolvedGalleryPath(QCoreApplication::applicationDirPath()) +
           QStringLiteral("/.thumbnails/") + QFileInfo(record.fileName).completeBaseName() +
           QStringLiteral(".png");
}

// 激活状态丢失时关闭预览；右键菜单或管理对话框期间暂不关闭。
void QuickBar::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || isActiveWindow())
        return;
    m_previewAllowed = false;
    m_previewTimer.stop();
    m_preview->closePreview();
    // 右键菜单是关联弹窗；管理对话框由managementActive标记保护。
    if (!m_managementActive && QApplication::activePopupWidget() == nullptr)
        close();
}

// 删除旧卡片，按搜索结果创建新卡片、加载缩略图并绑定交互信号。
void QuickBar::rebuild(const QString &query)
{
    // 旧卡片及布局项全部移除，避免搜索后残留不可见 QWidget。
    qDeleteAll(m_cards);
    m_cards.clear();
    while (m_grid->count())
        m_grid->removeItem(m_grid->itemAt(0));
    m_results = m_engine.search(query, m_config.rows * m_config.columns);
    // 有足够分辨率的缓存时优先读取；否则对原图按显示尺寸解码，避免整图解码。
    for (int index = 0; index < m_results.size(); ++index) {
        const SearchResult &result = m_results.at(index);
        auto *card = new QuickCandidate(result, m_config.thumbnailDisplaySize, m_candidateHost);
        const QSize displaySize(m_config.thumbnailDisplaySize, m_config.thumbnailDisplaySize);
        const QString sourceFilePath = sourcePath(result.meme);
        const QString cachedPath = thumbnailPath(result.meme);
        const bool sourceExists = QFileInfo::exists(sourceFilePath);
        const bool useCachedThumbnail = QFileInfo::exists(cachedPath) &&
                                        (m_config.thumbnailCacheSize >= m_config.thumbnailDisplaySize ||
                                         !sourceExists);
        QImageReader thumbnailReader(useCachedThumbnail ? cachedPath : sourceFilePath);
        thumbnailReader.setAutoTransform(true);
        if (!useCachedThumbnail) {
            if (result.meme.format == QLatin1String("gif"))
                thumbnailReader.setFormat(QByteArrayLiteral("GIF"));
            const QSize sourceSize = thumbnailReader.size();
            if (sourceSize.isValid()) {
                const QSize decodedSize = sourceSize.scaled(displaySize, Qt::KeepAspectRatio);
                if (decodedSize.width() < sourceSize.width() || decodedSize.height() < sourceSize.height())
                    thumbnailReader.setScaledSize(decodedSize);
            }
        }
        const QImage thumbnail = thumbnailReader.read();
        if (!thumbnail.isNull()) {
            card->setPixmap(QPixmap::fromImage(thumbnail.scaled(displaySize, Qt::KeepAspectRatio,
                                                                Qt::SmoothTransformation)));
        }
        QString html;
        const bool hasQuery = !query.trimmed().isEmpty();
        for (const NicknameRecord &record : result.nicknames) {
            const QString escaped = record.nickname.toHtmlEscaped();
            const bool primary = hasQuery && record.id == result.bestMatch.id;
            if (!html.isEmpty())
                html += QStringLiteral("&nbsp;");
            html += QStringLiteral(
                        "<span style='border:1px solid lightgray;border-radius:5px;"
                        "font-size:1rem;color:%1;font-weight:%2'>%3</span>")
                        .arg(primary ? QStringLiteral("white") : QStringLiteral("rgba(255,255,255,145)"),
                             primary ? QStringLiteral("700") : QStringLiteral("400"), escaped);
        }
        card->setText(html);
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QWidget::customContextMenuRequested, this, [this, card](const QPoint &position) {
            showContextMenu(card, card->mapToGlobal(position));
        });
        connect(card, &QuickCandidate::mouseEntered, this, [this, index]() { setCurrent(index); });
        connect(card, &QuickCandidate::clicked, this, [this, index]() {
            setCurrent(index);
            confirmCurrent();
        });
        connect(card, &QuickCandidate::mouseLeft, this, [this, index]() {
            if (index == m_current) {
                m_previewTimer.stop();
                m_preview->closePreview();
            }
        });
        m_cards.append(card);
        m_grid->addWidget(card, index / m_config.columns, index % m_config.columns);
    }
    setCurrent(m_cards.isEmpty() ? -1 : 0);
    adjustSize();
}

// 更新选中边框，并按“常规”页的悬停延迟为当前候选启动原图预览。
void QuickBar::setCurrent(int index)
{
    m_current = index;
    for (int item = 0; item < m_cards.size(); ++item)
        m_cards.at(item)->setActive(item == index);
    m_preview->closePreview();
    m_previewTimer.stop();
    if (m_previewAllowed && isVisible() && index >= 0 && index < m_results.size() &&
        m_config.thumbnailPreviewDelayMs > 0) {
        m_previewTimer.start(m_config.thumbnailPreviewDelayMs);
    }
}

// 关闭快捷栏并把当前候选 ID 交给控制器写回剪贴板。
void QuickBar::confirmCurrent()
{
    if (m_current < 0 || m_current >= m_results.size())
        return;
    const QString memeId = m_results.at(m_current).meme.id;
    close();
    emit memeSelected(memeId);
}

// 把 Escape、Enter 和方向键映射到关闭、确认和网格导航。
bool QuickBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const int columnCount = qMax(1, m_config.columns);
        switch (key->key()) {
        case Qt::Key_Escape:
            close(); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            confirmCurrent();
            return true;
        case Qt::Key_Left: if (m_current > 0) setCurrent(m_current - 1); return true;
        case Qt::Key_Right: if (m_current + 1 < m_cards.size()) setCurrent(m_current + 1); return true;
        case Qt::Key_Up: if (m_current - columnCount >= 0) setCurrent(m_current - columnCount); return true;
        case Qt::Key_Down: if (m_current + columnCount < m_cards.size()) setCurrent(m_current + columnCount); return true;
        default: break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

// 失焦时清理预览，并在没有管理弹窗时关闭快捷栏。
void QuickBar::focusOutEvent(QFocusEvent *event)
{
    QFrame::focusOutEvent(event);
    m_previewAllowed = false;
    m_previewTimer.stop();
    m_preview->closePreview();
    if (!m_managementActive && !isActiveWindow())
        close();
}

// 隐藏时无条件停止延迟预览并关闭预览窗口。
void QuickBar::hideEvent(QHideEvent *event)
{
    m_previewAllowed = false;
    m_previewTimer.stop();
    m_preview->closePreview();
    QFrame::hideEvent(event);
}

// 处理候选卡片的 nickname 编辑或整条表情包删除操作。
void QuickBar::showContextMenu(QuickCandidate *candidate, const QPoint &globalPosition)
{
    QMenu menu(this);
    QAction *modifyNicknames = menu.addAction(AppStrings::modifyNicknamesAction());
    QAction *remove = menu.addAction(AppStrings::deleteMemeAction());
    QAction *chosen = menu.exec(globalPosition);
    if (!chosen)
        return;

    m_managementActive = true;
    auto leaveManagement = qScopeGuard([this]() { m_managementActive = false; });
    if (chosen == modifyNicknames) {
        QStringList currentNames;
        for (const NicknameRecord &record : candidate->result().nicknames)
            currentNames.append(record.nickname);
        const QStringList updatedNames = NicknameEditDialog::multiLine(this, currentNames);
        if (!updatedNames.isEmpty() && updatedNames != currentNames)
            emit nicknamesChanged(candidate->result().meme.id, updatedNames);
        return;
    }
    if (chosen == remove && QMessageBox::question(this, AppStrings::deleteMemeTitle(),
                                                  AppStrings::deleteMemeQuestion()) == QMessageBox::Yes) {
        emit memeDeleted(candidate->result().meme.id);
    }
}
