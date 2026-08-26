// 自动粘贴是best-effort行为，调用方必须保证失败时保持静默。
#include "platforms/win/winpaste.h"

#include <QThread>
#include "storage/logservice.h"
#include <windows.h>

// 保存当前前台窗口句柄，供选择表情包后恢复焦点。
quintptr activeForegroundWindow()
{
    return quintptr(GetForegroundWindow());
}

// 等待目标窗口真正成为前台窗口后，再发送完整的 Ctrl+V 按键序列。
bool restoreAndPasteWindow(quintptr nativeWindowHandle)
{
    HWND window = reinterpret_cast<HWND>(nativeWindowHandle);
    if (!window || !IsWindow(window)) {
        LogService::instance().warning(QStringLiteral("自动粘贴失败：原前台窗口已失效"));
        return false;
    }
    if (IsIconic(window))
        ShowWindowAsync(window, SW_RESTORE);
    if (!SetForegroundWindow(window)) {
        LogService::instance().warning(QStringLiteral("自动粘贴失败：无法恢复原前台窗口"));
        return false;
    }

    // SetForegroundWindow成功只表示切换请求已接受；等待目标窗口真正取得前台焦点。
    const HWND targetRoot = GetAncestor(window, GA_ROOT);
    bool activated = false;
    for (int attempt = 0; attempt < 30; ++attempt) {
        const HWND foreground = GetForegroundWindow();
        if (foreground == window || GetAncestor(foreground, GA_ROOT) == targetRoot) {
            activated = true;
            break;
        }
        QThread::msleep(10);
    }
    if (!activated) {
        LogService::instance().warning(QStringLiteral("自动粘贴失败：原窗口未能取得输入焦点"));
        return false;
    }
    QThread::msleep(40);

    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    const UINT sent = SendInput(4, inputs, sizeof(INPUT));
    if (sent != 4) {
        LogService::instance().warning(
            QStringLiteral("自动粘贴失败：SendInput仅发送了%1/4个键盘事件").arg(sent));
        return false;
    }
    LogService::instance().info(QStringLiteral("原窗口已恢复焦点，自动粘贴请求已发送"));
    return true;
}
