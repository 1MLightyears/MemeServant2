// 声明捕获图片后的 nickname 输入浮窗及 AI 识别交互。
#ifndef UI_CAPTURETOAST_H
#define UI_CAPTURETOAST_H

// 捕获 Toast 主动取焦，失焦视为取消且不落盘。
#include <QFrame>
#include "ai/openaicompatibleprovider.h"
#include "clipboard/clipboardtypes.h"
#include "storage/appconfig.h"

class QPlainTextEdit;
class QLabel;

class CaptureToast : public QFrame
{
    Q_OBJECT
public:
    explicit CaptureToast(QWidget *parent = nullptr);
    /// 显示捕获图片、初始化输入框并按配置启动 AI 请求。
    void showImage(const CapturedImage &image, const AppConfig &config, const AiSettings &aiSettings);
    /// 取消 AI 请求并销毁浮窗，不保存当前输入。
    void cancel();

signals:
    void saveRequested(const CapturedImage &image, const QStringList &nicknames);
    void canceled();

protected:
    /// 浮窗失焦时取消，除非保存流程已经开始。
    void focusOutEvent(QFocusEvent *event) override;
    /// 拦截输入框中的保存、取消和 AI 快捷键。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// 校验配置后发起 AI nickname 请求，手动请求可覆盖缓存结果。
    void beginRequest(bool manual);
    /// 将 AI 返回值填入输入框（不自动保存）。
    void applyResult(const QString &nickname);
    /// 显示失败状态并把详细原因放入输入框提示。
    void applyFailure(const QString &message);
    /// 根据当前快捷键和状态生成多行编辑框 placeholder。
    void updatePlaceholder(const QString &status = QString());
    /// 从捕获载荷解码并在顶部显示等比例缩略图。
    void updateThumbnail();
    /// 将浮窗放在 caret 或鼠标附近并限制在屏幕可用区域内。
    void positionNear(const QPoint &point);
    /// 清洗 nickname，成功后发出保存信号并销毁浮窗。
    void accept();

    CapturedImage m_image;
    AppConfig m_config;
    QPlainTextEdit *m_editor = nullptr;
    QLabel *m_thumbnail = nullptr;
    OpenAiCompatibleProvider *m_provider = nullptr;
    AiSettings m_aiSettings;
    QString m_cachedNickname;
    bool m_manualRequest = false;
    bool m_accepting = false;
};

#endif
