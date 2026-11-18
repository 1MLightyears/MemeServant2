// 声明与平台无关的内存图片字节嗅探和尺寸读取工具。
#ifndef CLIPBOARD_IMAGEBYTES_H
#define CLIPBOARD_IMAGEBYTES_H

// 捕获路径、剪贴板引用文件和联网取回的原图都需要一致的格式/尺寸判断。
#include <QBuffer>
#include <QByteArray>
#include <QImageReader>
#include <QSize>
#include <QString>

namespace ImageBytes {

// 按文件魔数判断图片编码；不信任剪贴板格式名或文件扩展名。返回扩展名，无法识别时返回空。
inline QString formatOf(const QByteArray &bytes)
{
    if (bytes.size() >= 6) {
        const QByteArray signature = bytes.left(6);
        if (signature == QByteArrayLiteral("GIF87a") || signature == QByteArrayLiteral("GIF89a"))
            return QStringLiteral("gif");
    }
    static const QByteArray pngSignature = QByteArray::fromHex("89504e470d0a1a0a");
    if (bytes.size() >= pngSignature.size() && bytes.startsWith(pngSignature))
        return QStringLiteral("png");
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes.at(0)) == 0xff
        && static_cast<unsigned char>(bytes.at(1)) == 0xd8
        && static_cast<unsigned char>(bytes.at(2)) == 0xff)
        return QStringLiteral("jpg");
    if (bytes.size() >= 12 && bytes.startsWith(QByteArrayLiteral("RIFF"))
        && bytes.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return QStringLiteral("webp");
    if (bytes.size() >= 2 && bytes.startsWith(QByteArrayLiteral("BM")))
        return QStringLiteral("bmp");
    return {};
}

// 只读取编码头得到宽高，避免为了尺寸校验解码整张图片。
inline bool sizeOf(const QByteArray &bytes, int *width, int *height)
{
    QBuffer buffer(const_cast<QByteArray *>(&bytes));
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (!size.isValid())
        return false;
    *width = size.width();
    *height = size.height();
    return true;
}

// 直接按 GIF 块结构数帧，不依赖图片插件。QQ/微信缓存文件等来源存在异常或超长 GIF 时，
// QImageReader::imageCount() 可能拿不到真实帧数；块结构扫描只做头部跳读，代价很小。
inline int gifFrameCount(const QByteArray &bytes)
{
    if (bytes.size() < 13 || formatOf(bytes) != QStringLiteral("gif"))
        return 0;
    qsizetype position = 13; // 6 字节头 + 7 字节逻辑屏幕描述符
    const uchar screenPacked = static_cast<uchar>(bytes.at(10));
    if (screenPacked & 0x80) // 全局颜色表
        position += 3 * (1 << ((screenPacked & 0x07) + 1));
    const auto skipSubBlocks = [&bytes, &position]() {
        while (position < bytes.size()) {
            const int length = static_cast<uchar>(bytes.at(position++));
            if (length == 0)
                return true;
            position += length;
        }
        return false;
    };
    int frames = 0;
    while (position < bytes.size()) {
        const uchar block = static_cast<uchar>(bytes.at(position++));
        if (block == 0x3B) // trailer
            break;
        if (block == 0x2C) {
            // 图像描述符：left/top/width/height 各 2 字节 + packed 1 字节。
            ++frames;
            if (position + 9 > bytes.size())
                break;
            const uchar imagePacked = static_cast<uchar>(bytes.at(position + 8));
            position += 9;
            if (imagePacked & 0x80) // 局部颜色表
                position += 3 * (1 << ((imagePacked & 0x07) + 1));
            if (position >= bytes.size())
                break;
            ++position; // LZW minimum code size
            if (!skipSubBlocks())
                break;
        } else if (block == 0x21) {
            if (position >= bytes.size())
                break;
            ++position; // 扩展块 label
            if (!skipSubBlocks())
                break;
        } else {
            break; // 非法块，按解析失败处理
        }
    }
    return frames;
}

// 判断编码自身是否真的包含多帧；多候选时用它优先挑出真正的动图。
inline bool looksAnimated(const QByteArray &bytes)
{
    const QString format = formatOf(bytes);
    if (format == QStringLiteral("gif") && gifFrameCount(bytes) > 1)
        return true;
    if (format != QStringLiteral("gif") && format != QStringLiteral("webp"))
        return false;
    QBuffer buffer(const_cast<QByteArray *>(&bytes));
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    return reader.canRead() && reader.imageCount() > 1;
}

} // namespace ImageBytes

#endif
