// 定义剪贴板图片在平台层与业务层之间传递的纯数据载荷。
#ifndef CLIPBOARD_CLIPBOARDTYPES_H
#define CLIPBOARD_CLIPBOARDTYPES_H

// 剪贴板业务只依赖该抽象载荷，不直接接触Win32类型。
#include <QByteArray>
#include <QString>

struct CapturedImage
{
    bool isValid = false;
    bool selfWritten = false;
    QByteArray encoded;
    QString format;
    int width = 0;
    int height = 0;
};

#endif
