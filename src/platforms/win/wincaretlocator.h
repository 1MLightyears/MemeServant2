// 声明定位当前 Windows 输入焦点 caret 的辅助函数。
#ifndef PLATFORMS_WIN_WINCARETLOCATOR_H
#define PLATFORMS_WIN_WINCARETLOCATOR_H

// 尽力获取前台输入 caret 屏幕坐标，失败时返回鼠标位置。
struct CaretPoint
{
    bool success = false;
    int x = 0;
    int y = 0;
};

/// 优先返回当前前台窗口 caret 坐标，失败时回退到鼠标坐标。
CaretPoint locateWindowsCaret();

#endif
