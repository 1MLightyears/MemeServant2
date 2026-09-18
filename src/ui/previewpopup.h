// 声明候选表情包的原图/GIF 预览浮窗。
#ifndef UI_PREVIEWPOPUP_H
#define UI_PREVIEWPOPUP_H

// 原图预览浮窗不抢焦点，GIF 自动播放动画。
#include <QRect>
#include <QWidget>

class QLabel;
class QMovie;

class PreviewPopup : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewPopup(QWidget *parent = nullptr);
    /// 读取并显示原图；GIF 以动画形式播放。anchorRect为触发缩略图的全局矩形，
    /// 预览优先贴在它右侧，空间不足时翻到左侧并整体限制在屏幕内。
    void showOriginal(const QString &sourcePath, const QRect &anchorRect);
    /// 停止动画并隐藏预览窗口。
    void closePreview();

protected:
    /// 窗口失活时自动关闭预览。
    bool event(QEvent *event) override;

private:
    QLabel *m_label = nullptr;
    QMovie *m_movie = nullptr;
};

#endif
