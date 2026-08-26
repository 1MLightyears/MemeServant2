// 尝试使用 DWM accent 效果；系统不支持或调用失败时由 Qt 自己绘制背景。
#include "platforms/win/winwindoweffects.h"

#include <QWidget>
#include <windows.h>
#include <dwmapi.h>

#include "storage/logservice.h"

namespace {
// DWM accent API 所需的策略结构；数值对应系统 acrylic/accent 状态。
struct AccentPolicy
{
    int accentState;
    int accentFlags;
    unsigned int gradientColor;
    int animationId;
};
}

// 把策略应用到 Qt 原生窗口句柄，失败时保留窗口自身的背景绘制。
void applyWindowsBlur(QWidget *window)
{
    AccentPolicy policy{};
    policy.accentState = 4;
    policy.accentFlags = 2;
    policy.gradientColor = 0xCC202020;
    const HRESULT result = DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), 19,
                                                 &policy, sizeof(policy));
    if (FAILED(result))
        LogService::instance().warning(QStringLiteral("背景虚化效果不可用，使用普通半透明窗口"));
}
