// 声明 Windows 剪贴板监听、图片读写和原生消息处理类。
#ifndef PLATFORMS_WIN_WINCLIPBOARD_H
#define PLATFORMS_WIN_WINCLIPBOARD_H

// 封装 Windows 剪贴板监听、图片识别、原始格式保留和自写入标记。
#include <QObject>
#include <QString>
#include <windows.h>
#include "clipboard/clipboardtypes.h"

class WinClipboard : public QObject
{
    Q_OBJECT
public:
    explicit WinClipboard(QObject *parent = nullptr);
    ~WinClipboard() override;
    /// 注册隐藏消息窗口并开始监听剪贴板更新。
    bool start(QString *error);
    /// 移除剪贴板监听；隐藏窗口在对象析构时销毁。
    void stop();
    /// 将捕获图片以原始格式和本程序标记写回剪贴板。
    bool writeImage(const CapturedImage &image, QString *error);
    /// 处理一个 Windows 原生消息，并返回是否由本类消费。
    bool processMessage(MSG *message);

private:
    /// 处理窗口创建、剪贴板更新和销毁等窗口消息。
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    /// 从当前剪贴板读取单个图片并尽量保留原始编码。
    CapturedImage readClipboard();
    /// 过滤无效载荷后发出 imageCaptured 信号。
    void dispatchCaptured(const CapturedImage &image);
    /// 注册窗口类、消息窗口及自定义剪贴板格式。
    bool ensureMessageWindow(QString *error);

    HWND m_window = nullptr;
    ATOM m_windowClass = 0;
    UINT m_ownerMarker = 0;
    UINT m_pngFormat = 0;
    UINT m_gifFormat = 0;
    UINT m_jpegFormat = 0;

signals:
    void imageCaptured(const CapturedImage &image);
};

#endif
