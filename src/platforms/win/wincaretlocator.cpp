// 使用前台线程GUI信息定位caret，任何失败都不影响调用方继续工作。
#include "platforms/win/wincaretlocator.h"

#include <QCursor>
#include <windows.h>

// 将前台线程的 caret 客户区坐标映射到屏幕；失败时返回鼠标位置。
CaretPoint locateWindowsCaret()
{
    CaretPoint result;
    const GUITHREADINFO info{ sizeof(GUITHREADINFO) };
    const DWORD threadId = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    GUITHREADINFO actual{ sizeof(GUITHREADINFO) };
    if (threadId && GetGUIThreadInfo(threadId, &actual) && actual.hwndCaret) {
        POINT leftTop{ actual.rcCaret.left, actual.rcCaret.top };
        if (MapWindowPoints(actual.hwndCaret, nullptr, &leftTop, 1) != 0 || leftTop.x || leftTop.y) {
            result.success = true;
            result.x = leftTop.x;
            result.y = leftTop.y;
            return result;
        }
    }
    const QPointF cursorPosition = QCursor::pos();
    POINT cursor{ qRound(cursorPosition.x()), qRound(cursorPosition.y()) };
    result.x = cursor.x;
    result.y = cursor.y;
    return result;
}
