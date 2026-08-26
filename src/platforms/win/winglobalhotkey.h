// 声明通过隐藏消息窗口注册和处理全局快捷键的类。
#ifndef PLATFORMS_WIN_WINGLOBALHOTKEY_H
#define PLATFORMS_WIN_WINGLOBALHOTKEY_H

// 实际向系统尝试注册快捷键，冲突时保持原状态并报告失败。
#include <QObject>
#include <QString>
#include <windows.h>

class WinGlobalHotkey : public QObject
{
    Q_OBJECT
public:
    explicit WinGlobalHotkey(QObject *parent = nullptr);
    ~WinGlobalHotkey() override;
    /// 解析并注册一个带修饰键的单键快捷键，失败时保持未注册状态。
    bool registerSequence(const QString &sequence, QString *error);
    /// 注销当前快捷键（若已注册）。
    void unregister();
    /// 判断消息是否是本对象注册的 WM_HOTKEY。
    bool processHotkey(MSG *message);

signals:
    void activated();

private:
    HWND m_window = nullptr;
    bool m_registered = false;
};

#endif
