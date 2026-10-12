// 快捷栏关联窗口不会导致主浮窗误关闭；空查询按最近使用排序。
#include "ui/quickbar.h"

#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QScrollBar>
#include <QScrollArea>
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
#include <QSizePolicy>
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
        "QLabel{background:transparent;color:#fff}"
        "QScrollBar:vertical{background:rgba(0,0,0,96);width:8px;border-radius:4px}"
        "QScrollBar::handle:vertical{background:rgba(255,255,255,72);border-radius:4px;min-height:24px}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent}"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 9, 10, 9);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(AppStrings::searchPlaceholder());
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &QuickBar::rebuild);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent}"));
    m_candidateHost = new QWidget;
    m_candidateHost->setObjectName(QStringLiteral("candidateHost"));
    m_grid = new QGridLayout(m_candidateHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(6);
    m_grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_scroll->setWidget(m_candidateHost);
    layout->addWidget(m_search);
    layout->addWidget(m_scroll, 1);
    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, [this]() {
        // 预览只由鼠标悬停触发；键盘选中不再弹出原图。
        if (!m_previewAllowed || !isVisible() || m_hovered < 0 || m_hovered >= m_results.size())
            return;
        // 以悬停卡片为锚点，让预览贴在缩略图旁而不是整个快捷栏旁。
        const QuickCandidate *card = m_hovered < m_cards.size() ? m_cards.at(m_hovered) : nullptr;
        const QRect anchor = card ? QRect(card->mapToGlobal(QPoint(0, 0)), card->size())
                                  : QRect(QCursor::pos(), QSize(1, 1));
        m_preview->showOriginal(sourcePath(m_results.at(m_hovered).meme), anchor);
    });
    m_preview = new PreviewPopup(this);
    m_scroll->viewport()->installEventFilter(this);
}

// 更新悬停项并按悬停延迟重启原图预览计时器。
void QuickBar::setHovered(int index)
{
    if (index == m_hovered)
        return;
    m_hovered = index;
    m_preview->closePreview();
    m_previewTimer.stop();
    if (m_previewAllowed && isVisible() && index >= 0 && index < m_results.size() &&
        m_config.thumbnailPreviewDelayMs > 0) {
        m_previewTimer.start(m_config.thumbnailPreviewDelayMs);
    }
}

// 根据鼠标当前全局位置重新判断悬停在哪个卡片上；滚动后卡片移位时也需要调用。
void QuickBar::updateHoverFromCursor()
{
    if (m_cards.isEmpty() || !m_scroll->isVisible()) {
        setHovered(-1);
        return;
    }
    const QPoint hostPosition = m_candidateHost->mapFromGlobal(QCursor::pos());
    for (int index = 0; index < m_cards.size(); ++index) {
        if (m_cards.at(index)->geometry().contains(hostPosition)) {
            setHovered(index);
            return;
        }
    }
    setHovered(-1);
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
        // 滚动列表内容可能超过屏幕；把窗口限制在可用区域内，其余候选交给滚动条。
        size = size.boundedTo(area.size() - QSize(24, 48));
        resize(size);
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
    if (m_separator) {
        m_separator->deleteLater();
        m_separator = nullptr;
    }
    // 不限制结果数量：候选区改为可滚动列表，用户能浏览全部已缓存表情包。
    m_results = m_engine.search(query, -1);
    const int columns = qMax(1, m_config.columns);
    const QString wanted = query.trimmed().toLower();
    int separatorRow = -1;
    // 有搜索词时，在匹配结果与未匹配结果之间插入一条横向分割线。
    if (!wanted.isEmpty()) {
        while (separatorRow < m_results.size() && m_results.at(separatorRow + 1).matchLevel < 5)
            ++separatorRow;
    }
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
        connect(card, &QuickCandidate::mouseEntered, this, [this, index]() {
            setHovered(index);
            setCurrent(index);
        });
        connect(card, &QuickCandidate::clicked, this, [this, index]() {
            setCurrent(index);
            confirmCurrent();
        });
        connect(card, &QuickCandidate::mouseLeft, this, [this, index]() {
            if (index == m_hovered)
                setHovered(-1);
        });
        // 悬停离开后清掉空悬停态即可；蓝框保留在最后悬停的卡片上，
        // 方向键仍从该卡片继续移动。
        m_cards.append(card);
        m_grid->addWidget(card, index / columns, index % columns);
        // 分割线独占一整行（跨全部列）；其后的卡片随后统一重排。
        if (separatorRow >= 0 && index == separatorRow && !m_separator) {
            m_separator = makeSeparator();
            m_grid->addWidget(m_separator, index / columns + 1, 0, 1, columns);
        }
    }
    // 全部候选都匹配时在末尾补一条分割线，明确下方没有未匹配项。
    if (separatorRow >= 0 && !m_separator && !m_cards.isEmpty()) {
        m_separator = makeSeparator();
        m_grid->addWidget(m_separator, (m_results.size() - 1) / columns + 1, 0, 1, columns);
    }
    // 分割线独占一行；其后的未匹配卡片从分割线下一行的行首开始紧凑排列，
    // 无论匹配段最后一行是否铺满，都不会与分割线同行或留出空格。
    if (m_separator && separatorRow >= 0) {
        const int firstRowAfterSeparator = separatorRow / columns + 2;
        for (int index = separatorRow + 1; index < m_cards.size(); ++index) {
            const int offset = index - (separatorRow + 1);
            m_grid->addWidget(m_cards.at(index),
                              firstRowAfterSeparator + offset / columns, offset % columns);
        }
    }
    syncScrollRange();
    setCurrent(m_cards.isEmpty() ? -1 : 0);
    adjustSize();
}

// 创建带提示文字的横向分割线：中间是“未匹配”标签，两侧是较亮的线段。
QFrame *QuickBar::makeSeparator()
{
    auto *separator = new QFrame(m_candidateHost);
    separator->setAttribute(Qt::WA_StyledBackground);
    separator->setFixedHeight(2);
    separator->setStyleSheet(QStringLiteral(
        "QFrame{background:rgba(255,255,255,120);border:none;border-radius:1px}"));
    separator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return separator;
}

// 按配置的行列数计算候选区的固定尺寸；行数超出可视高度的部分交给滚动条。
void QuickBar::syncScrollRange()
{
    if (!m_scroll)
        return;
    const int columns = qMax(1, m_config.columns);
    const int spacing = m_grid->horizontalSpacing();
    const int displaySize = m_config.thumbnailDisplaySize;
    // 明确给出网格宽度，避免 QScrollArea 回退到很窄的默认尺寸。
    m_scroll->setFixedWidth(columns * displaySize + (columns - 1) * spacing);
    const int cardCount = m_cards.size();
    const int contentRows = qMax(1, (cardCount + columns - 1) / columns + (m_separator ? 1 : 0));
    const int visibleRows = qBound(1, qMax(1, m_config.rows), contentRows);
    const int rowHeight = displaySize + m_grid->verticalSpacing();
    m_scroll->setFixedHeight(visibleRows * rowHeight - m_grid->verticalSpacing());
}

// 把当前候选滚动到可视区域内，方向键导航时用户始终能看到选中卡片。
void QuickBar::ensureCurrentVisible(int index)
{
    if (!m_scroll || index < 0 || index >= m_cards.size())
        return;
    QuickCandidate *card = m_cards.at(index);
    const int row = card->y();
    const int cardHeight = card->height();
    const int visibleHeight = m_scroll->viewport()->height();
    const int verticalOffset = m_scroll->verticalScrollBar()->value();
    if (row < verticalOffset)
        m_scroll->verticalScrollBar()->setValue(row);
    else if (row + cardHeight > verticalOffset + visibleHeight)
        m_scroll->verticalScrollBar()->setValue(row + cardHeight - visibleHeight);
}

// 更新选中边框；键盘移动选中项时同步滚动，但不触发悬停预览。
void QuickBar::setCurrent(int index)
{
    m_current = index;
    for (int item = 0; item < m_cards.size(); ++item)
        m_cards.at(item)->setActive(item == index);
    if (index >= 0)
        ensureCurrentVisible(index);
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
    // 滚动后卡片整体位移，即使鼠标没动悬停的卡片也可能变了，按光标位置重算。
    if (watched == m_scroll->viewport() && event->type() == QEvent::Scroll) {
        m_preview->closePreview();
        m_previewTimer.stop();
        updateHoverFromCursor();
        return false;
    }
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
