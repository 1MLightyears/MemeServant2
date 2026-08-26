// 声明将前台窗口恢复并发送 Ctrl+V 的 best-effort 辅助函数。
#ifndef PLATFORMS_WIN_WINPASTE_H
#define PLATFORMS_WIN_WINPASTE_H

// 恢复原前台窗口并尽力发送 Ctrl+V。
#include <QtGlobal>

/// 恢复指定窗口、等待其取得焦点并发送 Ctrl+V。
bool restoreAndPasteWindow(quintptr nativeWindowHandle);
/// 返回当前前台窗口句柄，供后续自动粘贴恢复使用。
quintptr activeForegroundWindow();

#endif
