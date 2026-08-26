// 快捷键使用独立消息窗口接收WM_HOTKEY，便于动态换绑和注销。
#include "platforms/win/winglobalhotkey.h"

#include <QKeySequence>

#include "storage/logservice.h"

namespace {
constexpr int kHotkeyId = 0x4D53;
constexpr wchar_t kClassName[] = L"MemeServant2HotkeyWindow";

// 从隐藏消息窗口接收指定 ID 的 WM_HOTKEY 并发出 activated。
LRESULT CALLBACK hotkeyProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *hotkey = reinterpret_cast<WinGlobalHotkey *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (hotkey && message == WM_HOTKEY && wParam == kHotkeyId) {
        emit hotkey->activated();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

// 注册窗口类并创建不激活的消息窗口。
WinGlobalHotkey::WinGlobalHotkey(QObject *parent)
    : QObject(parent)
{
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = hotkeyProcedure;
    description.hInstance = GetModuleHandleW(nullptr);
    description.lpszClassName = kClassName;
    const ATOM atom = RegisterClassExW(&description);
    m_window = CreateWindowExW(WS_EX_NOACTIVATE, kClassName, L"MemeServant2 Hotkey", WS_OVERLAPPED,
                               0, 0, 0, 0, HWND_MESSAGE, nullptr, description.hInstance, nullptr);
    if (m_window)
        SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

// 注销快捷键后销毁消息窗口。
WinGlobalHotkey::~WinGlobalHotkey()
{
    unregister();
    if (m_window)
        DestroyWindow(m_window);
}

// 将 Qt 单键组合转换为 Win32 修饰键和虚拟键后完成注册。
bool WinGlobalHotkey::registerSequence(const QString &sequence, QString *error)
{
    unregister();
    const QKeySequence keys(sequence);
    if (keys.count() != 1 || m_window == nullptr) {
        if (error)
            *error = QStringLiteral("快捷键格式无效或不可注册。");
        return false;
    }
    const int combination = int(keys[0].toCombined());
    const int key = combination & 0x03FFFFFF;
    const int nativeModifiers = ((combination & Qt::CTRL) ? MOD_CONTROL : 0) |
                                ((combination & Qt::SHIFT) ? MOD_SHIFT : 0) |
                                ((combination & Qt::ALT) ? MOD_ALT : 0) |
                                ((combination & Qt::META) ? MOD_WIN : 0);
    if (!nativeModifiers || !key) {
        if (error)
            *error = QStringLiteral("全局快捷键必须包含修饰键。");
        return false;
    }
    m_registered = RegisterHotKey(m_window, kHotkeyId, UINT(nativeModifiers), UINT(key));
    if (!m_registered && error)
        *error = QStringLiteral("快捷键已被其他程序占用或不可注册。");
    else if (m_registered)
        LogService::instance().info(QStringLiteral("全局快捷键注册成功"));
    return m_registered;
}

// 幂等地注销当前快捷键并清除注册标志。
void WinGlobalHotkey::unregister()
{
    if (m_registered && m_window)
        UnregisterHotKey(m_window, kHotkeyId);
    m_registered = false;
}

// 检查消息窗口、消息类型和快捷键 ID 是否全部匹配。
bool WinGlobalHotkey::processHotkey(MSG *message)
{
    return message->message == WM_HOTKEY && message->hwnd == m_window && message->wParam == kHotkeyId;
}
