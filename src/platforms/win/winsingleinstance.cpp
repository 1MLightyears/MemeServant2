// 第二个进程只负责唤醒主进程后退出。
#include "platforms/win/winsingleinstance.h"

#include <QCoreApplication>

// 互斥体名称来自 cmake/ProjectConfig.cmake 经 CMake 生成的头文件。
#include <appmetadata.h>

namespace {
constexpr wchar_t kMutexName[] = MEMESERVANT2_SINGLE_INSTANCE_MUTEX_WIDE;
constexpr wchar_t kWindowClass[] = L"MemeServant2SingleInstanceWindow";
constexpr wchar_t kActivateMessage[] = L"MemeServant2.Activate.v1";
}

// 单实例窗口不承载 UI，所有消息使用默认处理。
LRESULT CALLBACK WinSingleInstance::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}

// 注册跨进程唤醒消息，实际互斥体和窗口在 becomePrimary 中创建。
WinSingleInstance::WinSingleInstance(QObject *parent)
    : QObject(parent)
{
    m_activateMessage = RegisterWindowMessageW(kActivateMessage);
}

// 创建命名互斥体；若已有实例则释放句柄并返回 false。
bool WinSingleInstance::becomePrimary()
{
    m_mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!m_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (m_mutex) {
            CloseHandle(m_mutex);
            m_mutex = nullptr;
        }
        return false;
    }

    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = windowProcedure;
    description.hInstance = GetModuleHandleW(nullptr);
    description.lpszClassName = kWindowClass;
    const ATOM windowClass = RegisterClassExW(&description);
    if (!windowClass)
        return false;
    m_window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kWindowClass,
                               L"MemeServant2 Single Instance", WS_OVERLAPPED,
                               0, 0, 0, 0, HWND_MESSAGE, nullptr, description.hInstance, nullptr);
    return m_window != nullptr;
}

// 查找主实例消息窗口并投递唤醒消息。
bool WinSingleInstance::notifyExisting()
{
    const UINT message = RegisterWindowMessageW(kActivateMessage);
    const HWND window = FindWindowExW(HWND_MESSAGE, nullptr, kWindowClass, nullptr);
    return window && PostMessageW(window, message, 0, 0);
}

// 只接受当前消息窗口收到的注册唤醒消息。
bool WinSingleInstance::processMessage(MSG *message)
{
    if (!m_window || !m_activateMessage || message->hwnd != m_window ||
        message->message != m_activateMessage)
        return false;
    emit activateRequested();
    return true;
}

// 销毁消息窗口并释放命名互斥体。
WinSingleInstance::~WinSingleInstance()
{
    if (m_window)
        DestroyWindow(m_window);
    if (m_mutex) {
        ReleaseMutex(m_mutex);
        CloseHandle(m_mutex);
    }
}
