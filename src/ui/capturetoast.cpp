// Toast中的AI结果只填入输入框，永远不会自动保存表情包。
#include "ui/capturetoast.h"

#include <QBuffer>
#include <QColor>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QImageReader>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QScreen>
#include <QToolTip>
#include <QVBoxLayout>

#include "core/nicknameutils.h"
#include "core/appstrings.h"
#include "platforms/win/wincaretlocator.h"

namespace {
// 统一封装 caret 定位，失败时由平台函数回退到鼠标位置。
CaretPoint currentCaret() { return locateWindowsCaret(); }
}

// 创建固定深色面板，避免系统浅色背景影响 placeholder 和输入文字可读性。
CaptureToast::CaptureToast(QWidget *parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setObjectName(QStringLiteral("captureToast"));
    setStyleSheet(QStringLiteral(
        "QFrame#captureToast{background-color:#202124;border:1px solid #4a4b50;"
        "border-radius:11px;color:#f7f7f7}"
        "QLabel#captureThumbnail{background-color:#131417;border:1px solid #3f4147;"
        "border-radius:8px;color:#aeb4bd;padding:0}"
        "QPlainTextEdit#nicknameEditor{background-color:#2c2d31;border:1px solid #55575e;"
        "border-radius:8px;color:#f7f7f7;padding:8px;selection-background-color:#3978d4;"
        "selection-color:#ffffff}"
        "QPlainTextEdit#nicknameEditor:focus{border-color:#70a5f5}"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_thumbnail = new QLabel(this);
    m_thumbnail->setObjectName(QStringLiteral("captureThumbnail"));
    m_thumbnail->setAlignment(Qt::AlignCenter);
    m_thumbnail->setFixedSize(324, 132);

    m_editor = new QPlainTextEdit(this);
    m_editor->setObjectName(QStringLiteral("nicknameEditor"));
    m_editor->setFixedHeight(92);
    QPalette editorPalette = m_editor->palette();
    editorPalette.setColor(QPalette::Base, QColor(QStringLiteral("#2c2d31")));
    editorPalette.setColor(QPalette::Text, QColor(QStringLiteral("#f7f7f7")));
    editorPalette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#aeb4bd")));
    m_editor->setPalette(editorPalette);
    m_editor->installEventFilter(this);
    layout->addWidget(m_thumbnail);
    layout->addWidget(m_editor);

    m_provider = new OpenAiCompatibleProvider(this);
    connect(m_provider, &OpenAiCompatibleProvider::stageChanged, this,
            [this](OpenAiCompatibleProvider::RequestStage stage) { applyStage(stage); });
    connect(m_provider, &OpenAiCompatibleProvider::succeeded, this, [this](const QString &value) { applyResult(value); });
    connect(m_provider, &OpenAiCompatibleProvider::failed, this, [this](const QString &reason) { applyFailure(reason); });
}

// 将浮窗放在输入 caret 右下方，并限制在当前屏幕可用区域内。
void CaptureToast::positionNear(const QPoint &point)
{
    resize(340, sizeHint().height());
    QPoint target = point + QPoint(16, 16);
    if (QScreen *screen = QGuiApplication::screenAt(target)) {
        const QRect available = screen->availableGeometry();
        target.setX(qMin(target.x(), available.right() - width() - 8));
        target.setY(qMin(target.y(), available.bottom() - height() - 8));
        target.setX(qMax(target.x(), available.left() + 8));
        target.setY(qMax(target.y(), available.top() + 8));
    }
    move(target);
}

// 重置上一次状态，显示缩略图并在需要时立即启动 AI 识别。
void CaptureToast::showImage(const CapturedImage &image, const AppConfig &config, const AiSettings &aiSettings)
{
    m_image = image;
    m_config = config;
    m_aiSettings = aiSettings;
    m_cachedNickname.clear();
    m_manualRequest = false;
    m_editor->clear();
    m_editor->setToolTip(QString());
    updatePlaceholder();
    updateThumbnail();
    const CaretPoint caret = currentCaret();
    positionNear(caret.success ? QPoint(caret.x, caret.y) : QCursor::pos());
    show();
    raise();
    activateWindow();
    m_editor->setFocus();
    if (m_config.autoAi)
        beginRequest(false);
}

// 将动态状态和保存/取消/AI 快捷键合并到输入框提示中。
void CaptureToast::updatePlaceholder(const QString &status)
{
    const QString instructions = AppStrings::nicknamePlaceholder(
        m_config.saveShortcut, m_config.cancelShortcut, m_config.aiShortcut);
    m_editor->setPlaceholderText(status.isEmpty() ? instructions
                                                   : status + QLatin1Char('\n') + instructions);
}

// 从原始编码读取第一帧，按输入框宽度等比例缩放后显示。
void CaptureToast::updateThumbnail()
{
    m_thumbnail->clear();
    QBuffer buffer(&m_image.encoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    if (!m_image.format.trimmed().isEmpty())
        reader.setFormat(m_image.format.toLatin1());
    const QImage preview = reader.read();
    if (preview.isNull()) {
        m_thumbnail->setText(AppStrings::thumbnailUnavailable());
        return;
    }

    const QSize available = m_thumbnail->size() - QSize(12, 12);
    m_thumbnail->setPixmap(QPixmap::fromImage(preview).scaled(
        available, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// 在多行编辑器中优先处理配置的快捷键，其他按键保持正常换行行为。
bool CaptureToast::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const int combined = int(key->keyCombination().toCombined());
        if (QKeySequence(combined) == QKeySequence(m_config.saveShortcut)) { accept(); return true; }
        if (QKeySequence(combined) == QKeySequence(m_config.cancelShortcut)) { cancel(); return true; }
        if (QKeySequence(combined) == QKeySequence(m_config.aiShortcut)) { beginRequest(true); return true; }
    }
    return QFrame::eventFilter(watched, event);
}

// 校验 AI 配置；自动请求复用缓存，手动请求允许重新发起网络请求。
void CaptureToast::beginRequest(bool manual)
{
    if (!m_image.isValid)
        return;
    const bool aiConfigured = !m_aiSettings.endpoint.trimmed().isEmpty() &&
                              !m_aiSettings.model.trimmed().isEmpty() &&
                              !m_aiSettings.apiKey.trimmed().isEmpty();
    if (!aiConfigured) {
        updatePlaceholder(AppStrings::aiNotConfigured());
        m_editor->setToolTip(AppStrings::aiNotConfigured());
        return;
    }
    // 自动识别只复用同一浮窗生命周期内的缓存；手动快捷键始终可重试。
    if (!m_manualRequest && !m_cachedNickname.isEmpty()) {
        if (manual)
            m_editor->setPlainText(m_cachedNickname);
        else if (m_editor->toPlainText().isEmpty())
            m_editor->setPlainText(m_cachedNickname);
        return;
    }
    m_manualRequest = manual;
    applyStage(OpenAiCompatibleProvider::RequestStage::Preparing);
    m_provider->requestNickname(m_image.encoded, m_image.format, m_aiSettings);
}

// AI 状态始终显示在当前捕获浮窗内，避免系统通知打断连续输入。
void CaptureToast::applyStage(OpenAiCompatibleProvider::RequestStage stage)
{
    QString status;
    switch (stage) {
    case OpenAiCompatibleProvider::RequestStage::Preparing:
        status = AppStrings::aiPreparing();
        break;
    case OpenAiCompatibleProvider::RequestStage::Sending:
        status = AppStrings::aiSending();
        break;
    case OpenAiCompatibleProvider::RequestStage::Recognizing:
        status = AppStrings::aiRecognizing();
        break;
    case OpenAiCompatibleProvider::RequestStage::Processing:
        status = AppStrings::aiProcessing();
        break;
    }
    updatePlaceholder(status);
    m_editor->setToolTip(status);
}

// 缓存 AI 结果，并仅在用户没有输入或明确手动请求时填入编辑器。
void CaptureToast::applyResult(const QString &nickname)
{
    m_cachedNickname = nickname;
    if ((m_manualRequest || m_editor->toPlainText().isEmpty()))
        m_editor->setPlainText(nickname);
    updatePlaceholder();
    m_editor->setToolTip(QString());
}

// 显示简短失败状态，同时保留服务端详细错误供 tooltip 查看。
void CaptureToast::applyFailure(const QString &message)
{
    updatePlaceholder(AppStrings::aiFailed());
    m_editor->setToolTip(message);
    QToolTip::showText(m_editor->mapToGlobal(QPoint(0, m_editor->height())), message, m_editor);
}

// 清洗多行 nickname；没有有效内容时保持窗口打开等待用户输入。
void CaptureToast::accept()
{
    const QStringList nicknames = NicknameUtils::clean(m_editor->toPlainText());
    if (nicknames.isEmpty())
        return;
    m_accepting = true;
    emit saveRequested(m_image, nicknames);
    hide();
    deleteLater();
    m_editor->deleteLater();
}

// 失焦默认视为取消，保存信号发出后的延迟销毁阶段除外。
void CaptureToast::focusOutEvent(QFocusEvent *event)
{
    QFrame::focusOutEvent(event);
    if (!m_accepting)
        cancel();
}

// 中止 AI、隐藏并销毁浮窗，不发出保存信号。
void CaptureToast::cancel()
{
    if (m_accepting)
        return;
    m_provider->cancel();
    hide();
    emit canceled();
    deleteLater();
}
