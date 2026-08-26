// 声明基于隐藏消息窗口的单实例检测与唤醒类。
#ifndef PLATFORMS_WIN_WINSINGLEINSTANCE_H
#define PLATFORMS_WIN_WINSINGLEINSTANCE_H

#include <QObject>
#include <windows.h>

class WinSingleInstance : public QObject
{
    Q_OBJECT
public:
    explicit WinSingleInstance(QObject *parent = nullptr);
    ~WinSingleInstance() override;
    /// 创建互斥体和消息窗口；已有实例时返回 false。
    bool becomePrimary();
    /// 向已有实例发送唤醒消息。
    static bool notifyExisting();
    /// 处理唤醒消息并发出 activateRequested 信号。
    bool processMessage(MSG *message);

signals:
    void activateRequested();

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HANDLE m_mutex = nullptr;
    HWND m_window = nullptr;
    UINT m_activateMessage = 0;
};

#endif
