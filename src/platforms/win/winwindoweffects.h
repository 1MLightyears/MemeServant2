// 声明给无边框 Qt 窗口应用 Windows DWM 背景效果的辅助函数。
#ifndef PLATFORMS_WIN_WINWINDOWEFFECTS_H
#define PLATFORMS_WIN_WINWINDOWEFFECTS_H

// 为浮窗应用背景虚化，失败时由调用方保持普通半透明。
/// 为指定窗口设置 DWM 背景效果；失败时仅记录警告。
void applyWindowsBlur(class QWidget *window);

#endif
