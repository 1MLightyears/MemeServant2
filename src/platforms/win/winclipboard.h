// 声明 Windows 剪贴板监听、图片读写和原生消息处理类。
#ifndef PLATFORMS_WIN_WINCLIPBOARD_H
#define PLATFORMS_WIN_WINCLIPBOARD_H

// 封装 Windows 剪贴板监听、图片识别、原始格式保留和自写入标记。
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
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

    /// 当前剪贴板的只读诊断快照；只读取，不发出捕获信号，供调试快捷键记录排查信息。
    struct ClipboardSnapshot
    {
        bool opened = false;      ///< 是否成功打开系统剪贴板
        QStringList formats;      ///< 剪贴板格式名；匿名格式用数字 ID
        bool selfWritten = false; ///< 是否带本程序自写标记
        CapturedImage image;      ///< 按正常捕获路径读到的图片结果
    };
    ClipboardSnapshot debugSnapshot();

private:
    /// 一种图片编码格式及其多个注册格式名；不同应用会用不同名字发布同一种编码数据。
    struct EncodedFormatFamily
    {
        QString encoding;      ///< 编码名/扩展名，如 "png"、"jpg"、"gif"。
        QVector<UINT> formats; ///< 按读取尝试顺序排列的注册格式 ID，首个沿用历史写入名。
    };

    /// 处理窗口创建、剪贴板更新和销毁等窗口消息。
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    /// 处理一次 WM_CLIPBOARDUPDATE：先用剪贴板序列号滤掉没有实际写入的重复通知，再读取内容。
    void handleClipboardUpdate();
    /// 从当前剪贴板读取单个图片并尽量保留原始编码。
    CapturedImage readClipboard();
    /// 过滤无效载荷后发出 imageCaptured 信号。
    void dispatchCaptured(const CapturedImage &image);
    /// 注册窗口类、消息窗口及自定义剪贴板格式。
    bool ensureMessageWindow(QString *error);
    /// 返回编码族的首选注册格式 ID；未注册成功时返回 0。
    UINT encodedFormatId(const QString &encoding) const;

    HWND m_window = nullptr;
    ATOM m_windowClass = 0;
    UINT m_ownerMarker = 0;
    /// 最近一次已经处理过的剪贴板序列号；WM_CLIPBOARDUPDATE 与它相同时说明剪贴板内容没变。
    DWORD m_lastSequence = 0;
    /// CF_HTML：浏览器复制图片时会在其中写入原图 URL，QQ/微信富文本也会写本地路径。
    UINT m_htmlFormat = 0;
    /// QQ/微信复制消息时写入的富文本 XML，EditElement 的 filepath 指向原图/表情文件。
    UINT m_qqRichFormat = 0;
    /// 旧版 QQ 使用的兼容格式名，内容与 m_qqRichFormat 相同。
    UINT m_qqRichLegacyFormat = 0;
    /// 按读取优先级排列的编码格式族：GIF 优先保留动图，其次 PNG、JPEG。
    QVector<EncodedFormatFamily> m_encodedFormats;

signals:
    void imageCaptured(const CapturedImage &image);
};

#endif
