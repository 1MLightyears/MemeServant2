// 快捷键使用独立消息窗口接收WM_HOTKEY，便于动态换绑和注销。
#include "platforms/win/winglobalhotkey.h"

#include <QKeySequence>

#include "storage/logservice.h"

namespace {
constexpr wchar_t kClassName[] = L"MemeServant2HotkeyWindow";

// 从隐藏消息窗口接收本窗口注册的 WM_HOTKEY 并发出 activated。
// 每个实例拥有独立的消息窗口，系统只会把该窗口注册的热键投递过来，无需再比对 ID。
LRESULT CALLBACK hotkeyProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *hotkey = reinterpret_cast<WinGlobalHotkey *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (hotkey && message == WM_HOTKEY) {
        emit hotkey->activated();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

// 注册窗口类并创建不激活的消息窗口。
WinGlobalHotkey::WinGlobalHotkey(int hotkeyId, QObject *parent)
    : QObject(parent), m_hotkeyId(hotkeyId)
{
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = hotkeyProcedure;
    description.hInstance = GetModuleHandleW(nullptr);
    description.lpszClassName = kClassName;
    // 同一进程的多个实例共用窗口类；第二个实例会得到“类已存在”错误，复用它创建消息窗口即可。
    RegisterClassExW(&description);
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
// Qt 的字母/数字键值与 Win32 虚拟键码一致，因此当前只适合注册这类按键（如默认的 Alt+D）；
// 标点和 F 键的值与 VK 不同，需要额外换算才能注册，暂不在支持范围内。
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
    m_registered = RegisterHotKey(m_window, UINT(m_hotkeyId), UINT(nativeModifiers), UINT(key));
    if (!m_registered && error)
        *error = QStringLiteral("快捷键已被其他程序占用或不可注册。");
    else if (m_registered)
        LogService::instance().info(QStringLiteral("全局快捷键注册成功：") + sequence);
    return m_registered;
}

// 幂等地注销当前快捷键并清除注册标志。
void WinGlobalHotkey::unregister()
{
    if (m_registered && m_window)
        UnregisterHotKey(m_window, UINT(m_hotkeyId));
    m_registered = false;
}

// 检查消息窗口、消息类型和快捷键 ID 是否全部匹配。
bool WinGlobalHotkey::processHotkey(MSG *message)
{
    return message->message == WM_HOTKEY && message->hwnd == m_window
           && message->wParam == UINT(m_hotkeyId);
}
