// 只处理一个逻辑图片对象；优先从剪贴板引用的原始文件恢复图片，本程序自写事件不会触发捕获。
#include "platforms/win/winclipboard.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStringList>
#include "storage/logservice.h"
#include <QThread>
#include <QUrl>
#include <QXmlStreamReader>
#include <initializer_list>
#include <utility>

#include "clipboard/imagebytes.h"

namespace {
constexpr wchar_t kClassName[] = L"MemeServant2ClipboardWindow";
// 剪贴板图片边长上限：超过视为异常载荷，避免解码/编码超大图。
constexpr int kMaxClipboardImageSide = 8192;
// 剪贴板格式和它引用的文件/网页内容共用的字节上限，避免异常数据把内存拉满。
constexpr qsizetype kMaxClipboardPayloadBytes = 64 * 1024 * 1024;
// Markdown 兜底只解析短文本；超过该长度不可能是单条图片引用。
constexpr qsizetype kMaxReferenceTextBytes = 64 * 1024;

// 诊断开关：环境变量 MEMESERVANT2_CLIPBOARD_DEBUG=1，或设置中的调试模式开启时，输出格式清单、
// 引用路径等详细诊断信息，便于定位 QQ/微信/浏览器行为。调试模式可在运行期切换，因此不缓存结果。
bool clipboardDebugEnabled()
{
    return qEnvironmentVariableIsSet("MEMESERVANT2_CLIPBOARD_DEBUG")
           || LogService::instance().isDebugFileEnabled();
}

// 调试模式专用：描述当前剪贴板所有者进程，用于确认是哪一路程序重新发布了剪贴板。
// 只在调试日志里使用，正常运行时不会调用。
QString clipboardOwnerSummary()
{
    const HWND owner = GetClipboardOwner();
    if (!owner)
        return QStringLiteral("无（打开剪贴板时未指定窗口）");
    DWORD processId = 0;
    GetWindowThreadProcessId(owner, &processId);
    wchar_t className[128] = {};
    const int classLength = GetClassNameW(owner, className, 128);
    QString processName;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)) {
        wchar_t imagePath[1024] = {};
        DWORD imagePathLength = 1024;
        if (QueryFullProcessImageNameW(process, 0, imagePath, &imagePathLength)) {
            processName = QFileInfo(QString::fromWCharArray(imagePath,
                                                           static_cast<qsizetype>(imagePathLength)))
                              .fileName();
        }
        CloseHandle(process);
    }
    return QStringLiteral("PID=") + QString::number(processId)
           + QStringLiteral("，进程=")
           + (processName.isEmpty() ? QStringLiteral("未知") : processName)
           + QStringLiteral("，窗口类=")
           + (classLength > 0 ? QString::fromWCharArray(className, classLength)
                              : QStringLiteral("未知"));
}

// 从可移动全局内存复制有限大小的字节，避免锁定失败或异常大小导致崩溃。
QByteArray globalBytes(HANDLE handle)
{
    const SIZE_T size = GlobalSize(handle);
    if (!size || size > static_cast<SIZE_T>(kMaxClipboardPayloadBytes))
        return {};
    const void *data = GlobalLock(handle);
    if (!data)
        return {};
    QByteArray result(static_cast<const char *>(data), static_cast<qsizetype>(size));
    GlobalUnlock(handle);
    return result;
}

// 读取指定剪贴板格式的 HGLOBAL 字节；调用方必须已经打开剪贴板。
QByteArray clipboardBytes(UINT format)
{
    const HANDLE handle = GetClipboardData(format);
    return handle ? globalBytes(handle) : QByteArray();
}

// 枚举当前剪贴板格式：formats 返回注册格式 ID；includeNames 为 true 时才查询可读名称
// （匿名格式用数字 ID）。调用方必须已经打开剪贴板。
QStringList clipboardFormatNames(QVector<UINT> *formats, bool includeNames)
{
    if (formats)
        formats->clear();
    QStringList names;
    UINT format = 0;
    while ((format = EnumClipboardFormats(format)) != 0) {
        if (formats)
            formats->append(format);
        if (!includeNames)
            continue;
        wchar_t name[128] = {};
        const int length = GetClipboardFormatNameW(format, name, 128);
        names.append(length > 0 ? QString::fromWCharArray(name, length) : QString::number(format));
    }
    return names;
}

// 保留剪贴板提供的完整编码块；PNG/JPEG/GIF 解码器会忽略尾部填充。
QByteArray encodedBytes(QByteArray data)
{
    // 全局内存可能按分配粒度补零，解码器只需要有效前缀。
    return data;
}

// 判断 DIB 是否真的声明了 alpha 通道：只有 V4/V5 头部里的 AlphaMask 非零才算。
// 40 字节 BITMAPINFOHEADER 的 32 位 BI_RGB/三掩码 BI_BITFIELDS 没有 alpha 语义：由
// HBITMAP 转出的 DIB 和部分截图工具会把第 4 字节填 0，若当成 alpha 会得到整张透明图。
bool dibDeclaresAlpha(const BITMAPINFOHEADER *header, qsizetype headerSize)
{
    if (header->biBitCount != 32)
        return false;
    if (headerSize >= qsizetype(sizeof(BITMAPV5HEADER)))
        return reinterpret_cast<const BITMAPV5HEADER *>(header)->bV5AlphaMask != 0;
    if (headerSize >= qsizetype(sizeof(BITMAPV4HEADER)))
        return reinterpret_cast<const BITMAPV4HEADER *>(header)->bV4AlphaMask != 0;
    return false;
}

// 将 24/32 位 bottom-up 或 top-down DIB 转换为 Qt ARGB 图像。
QImage imageFromDib(const QByteArray &bytes)
{
    if (bytes.size() < qsizetype(sizeof(BITMAPINFOHEADER)))
        return {};
    auto *header = reinterpret_cast<const BITMAPINFOHEADER *>(bytes.constData());
    if (header->biBitCount != 24 && header->biBitCount != 32)
        return {};
    const int width = header->biWidth;
    const int height = std::abs(header->biHeight);
    if (width <= 0 || height <= 0)
        return {};
    const bool topDown = header->biHeight < 0;
    const qsizetype bytesPerPixel = header->biBitCount / 8;
    const qsizetype stride = ((qsizetype(width) * header->biBitCount + 31) / 32) * 4;

    // CF_DIBV5 的头部是 BITMAPV5HEADER（124 字节），CF_DIB 通常是 BITMAPINFOHEADER（40 字节）。
    // 像素起点必须按头部自己声明的 biSize 计算，不能写死 sizeof(BITMAPINFOHEADER)：32bpp 的
    // DIBV5 会因此从头部内部提前 84 字节开始读，整幅图出现 21 像素的水平错位。
    // V4/V5 头部的颜色掩码已包含在头部长度内；只有 40 字节 BITMAPINFOHEADER + BI_BITFIELDS
    // 才在头部之后额外跟 3 个 DWORD 掩码，另外 biClrUsed 个 RGBQUAD 调色板也要跳过。
    const qsizetype headerSize = qsizetype(header->biSize);
    if (headerSize < qsizetype(sizeof(BITMAPINFOHEADER)) || headerSize > bytes.size())
        return {};
    qsizetype pixelOffset = headerSize;
    if (header->biCompression == BI_BITFIELDS && headerSize == qsizetype(sizeof(BITMAPINFOHEADER)))
        pixelOffset += 3 * qsizetype(sizeof(DWORD));
    pixelOffset += qsizetype(header->biClrUsed) * qsizetype(sizeof(RGBQUAD));
    if (pixelOffset > bytes.size() || stride <= 0
        || stride > (bytes.size() - pixelOffset) / height)
        return {};

    const bool alphaDeclared = dibDeclaresAlpha(header, headerSize);
    QImage image(width, height, QImage::Format_ARGB32);
    if (image.isNull())
        return {};
    const char *pixels = bytes.constData() + pixelOffset;
    bool hasNonZeroAlpha = false;
    for (int y = 0; y < height; ++y) {
        QRgb *target = reinterpret_cast<QRgb *>(image.scanLine(topDown ? y : height - y - 1));
        const char *source = pixels + stride * y;
        for (int x = 0; x < width; ++x) {
            const uchar *pixel = reinterpret_cast<const uchar *>(source + x * bytesPerPixel);
            const uchar blue = pixel[0];
            const uchar green = pixel[1];
            const uchar red = pixel[2];
            // 没有 alpha 声明的 32bpp DIB 第 4 字节不可信（常见为 0），一律按不透明处理。
            const uchar alpha = bytesPerPixel == 4 && alphaDeclared ? pixel[3] : 255;
            if (alpha != 0)
                hasNonZeroAlpha = true;
            target[x] = qRgba(red, green, blue, alpha);
        }
    }
    // 极少数 DIBV5 声明了 AlphaMask 却没有填 alpha（全 0），整张图完全透明没有可捕获内容；
    // 按不透明兜底，避免用户存下一张看不见的图。
    if (alphaDeclared && !hasNonZeroAlpha) {
        for (int y = 0; y < height; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < width; ++x)
                line[x] |= 0xff000000u;
        }
        LogService::instance().warning(QStringLiteral("剪贴板 DIB 声明了透明度但 alpha 全为 0，已按不透明处理"));
    }
    return image;
}

// 生成兼容性最好的 bottom-up 24 位 CF_DIB 数据。
QByteArray imageToDib(const QImage &source)
{
    // CF_DIB is deliberately emitted as a conventional bottom-up 24-bit bitmap. A number of
    // Windows applications reject top-down 32-bit BI_RGB DIBs or interpret their unused alpha
    // byte as fully transparent, which made a successful SetClipboardData call unpasteable.
    const QImage image = source.convertToFormat(QImage::Format_RGB888);
    const qsizetype stride = ((qsizetype(image.width()) * 24 + 31) / 32) * 4;
    QByteArray result(int(sizeof(BITMAPINFOHEADER) + stride * image.height()), '\0');
    auto *header = reinterpret_cast<BITMAPINFOHEADER *>(result.data());
    ZeroMemory(header, sizeof(BITMAPINFOHEADER));
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = image.width();
    header->biHeight = image.height();
    header->biPlanes = 1;
    header->biBitCount = 24;
    header->biCompression = BI_RGB;
    header->biSizeImage = DWORD(stride * image.height());
    char *pixels = result.data() + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < image.height(); ++y) {
        const uchar *sourceLine = image.constScanLine(image.height() - y - 1);
        uchar *targetLine = reinterpret_cast<uchar *>(pixels + stride * y);
        for (int x = 0; x < image.width(); ++x) {
            targetLine[x * 3] = sourceLine[x * 3 + 2];
            targetLine[x * 3 + 1] = sourceLine[x * 3 + 1];
            targetLine[x * 3 + 2] = sourceLine[x * 3];
        }
    }
    return result;
}

// 生成保留 alpha 通道的 CF_DIBV5 数据，供支持透明度的目标程序使用。
QByteArray imageToDibV5(const QImage &source)
{
    // CF_DIBV5 preserves transparency for consumers that support it. CF_DIB above remains the
    // compatibility fallback for editors and chat clients that only understand classic DIBs.
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const qsizetype pixelBytes = image.bytesPerLine() * image.height();
    QByteArray result(int(sizeof(BITMAPV5HEADER) + pixelBytes), '\0');
    auto *header = reinterpret_cast<BITMAPV5HEADER *>(result.data());
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = image.width();
    header->bV5Height = image.height();
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = DWORD(pixelBytes);
    header->bV5RedMask = 0x00ff0000;
    header->bV5GreenMask = 0x0000ff00;
    header->bV5BlueMask = 0x000000ff;
    header->bV5AlphaMask = 0xff000000;
    header->bV5CSType = LCS_sRGB;
    header->bV5Intent = LCS_GM_IMAGES;
    char *pixels = result.data() + sizeof(BITMAPV5HEADER);
    for (int y = 0; y < image.height(); ++y) {
        memcpy(pixels + image.bytesPerLine() * y,
               image.constScanLine(image.height() - y - 1), image.bytesPerLine());
    }
    return result;
}

// 判断图像是否真的需要 alpha：带 alpha 通道但全部不透明的图像不值得再投递一份 32 位 DIB。
bool hasTransparency(const QImage &source)
{
    if (!source.hasAlphaChannel())
        return false;
    // ARGB32 系列可以直接按像素读；其他带 alpha 的格式先统一转换再扫描。
    const bool direct = source.format() == QImage::Format_ARGB32 ||
                        source.format() == QImage::Format_ARGB32_Premultiplied;
    const QImage image = direct ? source : source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) != 255)
                return true;
        }
    }
    return false;
}

// 分配 Windows 剪贴板要求的可移动全局内存并复制字节。
HANDLE allocateBytes(const QByteArray &bytes)
{
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, std::max<size_t>(1, bytes.size()));
    if (!global)
        return nullptr;
    void *target = GlobalLock(global);
    if (!target) {
        GlobalFree(global);
        return nullptr;
    }
    memcpy(target, bytes.constData(), bytes.size());
    GlobalUnlock(global);
    return global;
}

// 从 CF_HDROP 读取文件路径；调用方必须已经打开剪贴板。只拷贝路径，不在这里读文件内容。
QStringList clipboardDropPaths()
{
    const HANDLE handle = GetClipboardData(CF_HDROP);
    if (!handle)
        return {};
    const auto drop = reinterpret_cast<HDROP>(handle);
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    QStringList paths;
    // 捕获只处理单个逻辑图片，异常多的文件列表直接截断。
    for (UINT index = 0; index < qMin<UINT>(count, 16); ++index) {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        if (!length)
            continue;
        QVector<wchar_t> buffer(static_cast<qsizetype>(length) + 1);
        if (DragQueryFileW(drop, index, buffer.data(), length + 1) == length)
            paths.append(QString::fromWCharArray(buffer.constData(), static_cast<qsizetype>(length)));
    }
    return paths;
}

// 前置声明：富文本 XML 解析失败时的按属性名兜底分支会用到。
QString unescapeMarkupAttribute(QString value);

// 解析 QQ/微信富文本剪贴板格式（QQ_Unicode_RichEdit_Format 等）里的图片路径。
// 格式是 UTF-8 XML，后面可能跟着全局内存补零；EditElement 的 filepath 指向原始表情/图片文件。
QStringList richEditFilePaths(const QByteArray &document)
{
    QStringList paths;
    if (document.isEmpty() || document.size() > kMaxClipboardPayloadBytes)
        return paths;
    const qsizetype nul = document.indexOf('\0');
    const QByteArray xmlBytes = nul < 0 ? document : document.left(nul);
    if (xmlBytes.isEmpty())
        return paths;
    const QString xml = QString::fromUtf8(xmlBytes);
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement
            && reader.name() == QStringLiteral("EditElement")) {
            const QString filePath =
                reader.attributes().value(QLatin1String("filepath")).toString().trimmed();
            if (!filePath.isEmpty())
                paths.append(filePath);
        }
        if (reader.hasError())
            break;
    }
    // 私有格式偶尔会在 XML 前后带非 XML 数据；解析失败时退回按属性名抓取，避免整条线索丢失。
    if (paths.isEmpty()) {
        static const QRegularExpression attributePattern(
            QStringLiteral(R"RX(\bfilepath\s*=\s*"([^"]*)")RX"));
        QRegularExpressionMatchIterator matches = attributePattern.globalMatch(xml);
        while (matches.hasNext()) {
            const QString filePath =
                unescapeMarkupAttribute(matches.next().captured(1)).trimmed();
            if (!filePath.isEmpty())
                paths.append(filePath);
        }
    }
    return paths;
}

// 解码 HTML/XML 属性里的常见实体；先处理具名实体，最后处理 &amp; 避免二次解码。
QString unescapeMarkupAttribute(QString value)
{
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    value.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return value;
}

// 调试模式专用：输出富文本 XML 的元素名和属性名（不输出任何属性值），
// 用于确认 QQ/微信私有格式的结构是否发生变化；消息正文不会进入日志。
QString describeRichEditStructure(const QByteArray &document)
{
    if (document.isEmpty() || document.size() > kMaxClipboardPayloadBytes)
        return QStringLiteral("（空）");
    const qsizetype nul = document.indexOf('\0');
    const QByteArray xmlBytes = nul < 0 ? document : document.left(nul);
    if (xmlBytes.isEmpty())
        return QStringLiteral("（空）");
    QStringList elements;
    QXmlStreamReader reader(QString::fromUtf8(xmlBytes));
    while (!reader.atEnd() && elements.size() < 20) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token != QXmlStreamReader::StartElement)
            continue;
        QString descriptor = reader.name().toString();
        QStringList attributeNames;
        for (const QXmlStreamAttribute &attribute : reader.attributes())
            attributeNames.append(attribute.name().toString());
        if (!attributeNames.isEmpty()) {
            descriptor += QLatin1Char('[') + attributeNames.join(QLatin1Char(','))
                          + QLatin1Char(']');
        }
        elements.append(descriptor);
        if (reader.hasError())
            break;
    }
    if (elements.isEmpty())
        return QStringLiteral("（非XML或编码不受支持）");
    return elements.join(QStringLiteral(" > "));
}

// 读取 CF_HTML 头部里的字节偏移（StartFragment/EndFragment 等）。
qsizetype htmlHeaderOffset(const QByteArray &data, const QByteArray &header)
{
    const qsizetype start = data.indexOf(header);
    if (start < 0)
        return -1;
    qsizetype position = start + header.size();
    while (position < data.size() && (data.at(position) == ' ' || data.at(position) == '\t'))
        ++position;
    const qsizetype numberStart = position;
    qsizetype number = 0;
    while (position < data.size() && data.at(position) >= '0' && data.at(position) <= '9') {
        number = number * 10 + (data.at(position) - '0');
        ++position;
    }
    return position == numberStart ? -1 : number;
}

struct HtmlClipboard
{
    QStringList imageSources;
};

// 解析 CF_HTML 里的 <img src> 本地图片引用（QQ/微信富文本等会写 file:// 路径）。
HtmlClipboard parseHtmlClipboard(const QByteArray &bytes)
{
    HtmlClipboard result;
    if (bytes.isEmpty() || bytes.size() > kMaxClipboardPayloadBytes)
        return result;
    const qsizetype nul = bytes.indexOf('\0');
    const QByteArray document = nul < 0 ? bytes : bytes.left(nul);
    const qsizetype fragmentStart = htmlHeaderOffset(document, QByteArrayLiteral("StartFragment:"));
    const qsizetype fragmentEnd = htmlHeaderOffset(document, QByteArrayLiteral("EndFragment:"));
    QByteArray fragment = document;
    if (fragmentStart >= 0 && fragmentEnd > fragmentStart && fragmentEnd <= document.size())
        fragment = document.mid(fragmentStart, fragmentEnd - fragmentStart);
    static const QRegularExpression imageSourcePattern(
        QStringLiteral(R"RX(<img\b[^>]*\bsrc\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))RX"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator matches = imageSourcePattern.globalMatch(QString::fromUtf8(fragment));
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        QString source = match.captured(1);
        if (source.isEmpty())
            source = match.captured(2);
        if (source.isEmpty())
            source = match.captured(3);
        source = unescapeMarkupAttribute(source).trimmed();
        if (!source.isEmpty())
            result.imageSources.append(source);
    }
    return result;
}

// 把剪贴板引用（file: URL、Windows 绝对路径、Markdown 路径）转成本地文件路径。
QString localPathFromReference(const QString &reference)
{
    QString value = reference.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        value = value.mid(1, value.size() - 2).trimmed();
    if (value.isEmpty() || value.size() > 4096
        || value.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive))
        return {};
    if (value.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(value);
        if (url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) == 0) {
            const QString localFile = url.toLocalFile();
            if (!localFile.isEmpty())
                return QDir::fromNativeSeparators(localFile);
        }
        // QQ 等应用偶尔写入未转义空格/中文的 file: 路径，QUrl 解析失败时手工去掉前缀再解码百分号。
        QString manual = value.mid(5);
        while (manual.startsWith(QLatin1Char('/')))
            manual.remove(0, 1);
        manual = QUrl::fromPercentEncoding(manual.toUtf8());
        if (manual.size() > 1 && manual.at(1) == QLatin1Char(':'))
            return QDir::fromNativeSeparators(manual);
        return {};
    }
    if (value.contains(QStringLiteral("://")))
        return {};
    const QString native = QDir::fromNativeSeparators(value);
    // 相对路径缺少基准目录，无法安全解析；只接受绝对路径和 UNC 路径。
    return QDir::isAbsolutePath(native) ? native : QString();
}

// 解码 HTML/Markdown 里内嵌的 data:image/gif;base64 数据，无网络时也能拿到动图。
QByteArray decodeGifDataUri(const QString &reference)
{
    const QString value = reference.trimmed();
    if (!value.startsWith(QStringLiteral("data:image/gif"), Qt::CaseInsensitive))
        return {};
    const qsizetype separator = value.indexOf(QStringLiteral(";base64,"), 0, Qt::CaseInsensitive);
    if (separator < 0)
        return {};
    constexpr int kBase64MarkerLength = 8; // ";base64,"
    const QByteArray bytes = QByteArray::fromBase64(value.mid(separator + kBase64MarkerLength).toLatin1());
    if (bytes.isEmpty() || bytes.size() > kMaxClipboardPayloadBytes)
        return {};
    return ImageBytes::formatOf(bytes) == QStringLiteral("gif") ? bytes : QByteArray();
}

// 读取剪贴板引用的本地文件；刚复制时来源程序可能还在写临时文件，短暂重试避免读到半截数据。
QByteArray readReferencedImageFile(const QString &path)
{
    // 拒绝设备命名空间，避免剪贴板文本构造出 \\\\.\\ 之类的特殊路径。
    if (path.startsWith(QStringLiteral("\\\\.\\")))
        return {};
    const QFileInfo info(path);
    if (!info.isFile())
        return {};
    const qint64 expectedSize = info.size();
    if (expectedSize <= 0 || expectedSize > kMaxClipboardPayloadBytes)
        return {};
    for (int attempt = 0; attempt < 3; ++attempt) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            // 只读到上限多一个字节，避免文件在检查后继续增长时一次性读进超大内容。
            const QByteArray bytes = file.read(kMaxClipboardPayloadBytes + 1);
            if (bytes.size() > 0 && bytes.size() <= kMaxClipboardPayloadBytes
                && !ImageBytes::formatOf(bytes).isEmpty())
                return bytes;
        }
        if (attempt < 2)
            QThread::msleep(20);
    }
    return {};
}

// 读取剪贴板纯文本；只用于 Markdown 图片引用的兜底，因此限制长度。
QString clipboardPlainText()
{
    const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle)
        return {};
    const SIZE_T bytes = GlobalSize(handle);
    if (bytes <= sizeof(wchar_t) || bytes > static_cast<SIZE_T>(kMaxReferenceTextBytes))
        return {};
    const void *data = GlobalLock(handle);
    if (!data)
        return {};
    QString text = QString::fromWCharArray(static_cast<const wchar_t *>(data),
                                           static_cast<qsizetype>(bytes / sizeof(wchar_t)));
    GlobalUnlock(handle);
    const qsizetype nul = text.indexOf(QChar(0));
    if (nul >= 0)
        text.truncate(nul);
    return text;
}

// 从纯文本里识别“整个剪贴板就是一条 Markdown 图片引用”的场景（如 Obsidian/Typora 复制图片）。
// 只接受单行且整行都是图片语法的文本，避免复制普通聊天文本时误触发。
QString markdownImageTarget(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 2048
        || trimmed.contains(QLatin1Char('\n')) || trimmed.contains(QLatin1Char('\r')))
        return {};
    // 兼容两种写法：![alt](<带空格的路径>) 和 ![alt](普通路径)；可带 "title"。
    static const QRegularExpression pattern(
        QStringLiteral(R"RX(^!\[[^\]]*\]\(\s*(?:<([^>]*)>|([^)\s]+))(?:\s+(?:"[^"]*"|'[^']*'))?\s*\)$)RX"));
    const QRegularExpressionMatch match = pattern.match(trimmed);
    if (!match.hasMatch())
        return {};
    const QString bracketed = match.captured(1).trimmed();
    return bracketed.isEmpty() ? match.captured(2).trimmed() : bracketed;
}
}

// 注册自写标记和常见图片编码格式；同一编码的多个注册名归为一族，共用同一套读取逻辑。
WinClipboard::WinClipboard(QObject *parent)
    : QObject(parent)
{
    m_ownerMarker = RegisterClipboardFormatW(L"MemeServant2.SelfWrite.v1");
    // 每个编码族的首个名字沿用历史写入名，写回行为保持不变；其余名字只用于读取兼容。
    const auto registerFamily = [](const QString &encoding, std::initializer_list<const wchar_t *> names) {
        EncodedFormatFamily family;
        family.encoding = encoding;
        family.formats.reserve(static_cast<qsizetype>(names.size()));
        for (const wchar_t *name : names) {
            const UINT format = RegisterClipboardFormatW(name);
            if (format != 0 && !family.formats.contains(format))
                family.formats.append(format);
        }
        return family;
    };
    // GIF 排在最前：只有来源愿意把原始 GIF 字节放进注册格式时才能保住动图，PNG/JPEG 都是静态帧。
    m_encodedFormats.append(registerFamily(QStringLiteral("gif"), {L"image/gif", L"GIF"}));
    m_encodedFormats.append(registerFamily(QStringLiteral("png"), {L"PNG", L"image/png"}));
    m_encodedFormats.append(registerFamily(QStringLiteral("jpg"), {L"JPEG", L"JFIF", L"image/jpeg"}));
    // 剪贴板引用格式：浏览器图片的 HTML 地址，以及 QQ/微信富文本里的原始文件路径。
    m_htmlFormat = RegisterClipboardFormatW(L"HTML Format");
    m_qqRichFormat = RegisterClipboardFormatW(L"QQ_Unicode_RichEdit_Format");
    m_qqRichLegacyFormat = RegisterClipboardFormatW(L"QQ_RichEdit_Format");
}

// 返回编码族的首选注册格式 ID：读取时作为该族首个尝试项，写入时沿用历史注册名。
UINT WinClipboard::encodedFormatId(const QString &encoding) const
{
    for (const EncodedFormatFamily &family : m_encodedFormats) {
        if (family.encoding == encoding)
            return family.formats.value(0);
    }
    return 0;
}

// 停止监听、销毁消息窗口并注销窗口类。
WinClipboard::~WinClipboard()
{
    stop();
    if (m_window)
        DestroyWindow(m_window);
    if (m_windowClass)
        UnregisterClassW(reinterpret_cast<LPCWSTR>(MAKEINTATOM(m_windowClass)), GetModuleHandleW(nullptr));
}

// 接收剪贴板更新消息，过滤重复通知后读取当前内容并转交业务层过滤。
LRESULT CALLBACK WinClipboard::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *clipboard = reinterpret_cast<WinClipboard *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CLIPBOARDUPDATE && clipboard) {
        clipboard->handleClipboardUpdate();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// 延迟创建隐藏消息窗口并保存 this 指针供静态窗口过程回调。
bool WinClipboard::ensureMessageWindow(QString *error)
{
    if (m_window)
        return true;
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = windowProcedure;
    description.hInstance = GetModuleHandleW(nullptr);
    description.lpszClassName = kClassName;
    m_windowClass = RegisterClassExW(&description);
    if (!m_windowClass) {
        if (error)
            *error = QStringLiteral("剪贴板监听窗口注册失败。");
        return false;
    }
    m_window = CreateWindowExW(WS_EX_NOACTIVATE, kClassName, L"MemeServant2 Clipboard", WS_OVERLAPPED,
                               0, 0, 0, 0, HWND_MESSAGE, nullptr, description.hInstance, nullptr);
    if (!m_window) {
        if (error)
            *error = QStringLiteral("剪贴板监听窗口创建失败。");
        return false;
    }
    SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

// 确保窗口存在后注册系统剪贴板监听器。
bool WinClipboard::start(QString *error)
{
    if (!ensureMessageWindow(error))
        return false;
    if (!AddClipboardFormatListener(m_window)) {
        if (error)
            *error = QStringLiteral("系统拒绝注册剪贴板监听器。");
        return false;
    }
    LogService::instance().info(QStringLiteral("剪贴板监听已启动"));
    return true;
}

// 移除监听器；窗口和格式 ID 由对象生命周期管理。
void WinClipboard::stop()
{
    if (m_window)
        RemoveClipboardFormatListener(m_window);
}

// 按“剪贴板引用的原始文件 > 内嵌 GIF > 注册编码格式 > DIB”的优先级读取单个图片，并附带
// 可联网获取的网页 GIF 地址。剪贴板全局锁内只做格式判断和字节/路径拷贝，文件读取、解码、
// DIB 转换和 PNG 编码都放到关闭剪贴板之后，避免长时间阻塞其他程序。
CapturedImage WinClipboard::readClipboard()
{
    CapturedImage result;
    QByteArray payload;
    QString encoding;   // 非空表示 payload 是注册编码格式（png/jpg/gif）的字节流
    UINT dibFormat = 0; // 非 0 表示 payload 是 DIB 字节流
    QStringList dropPaths;
    QVector<QByteArray> richEditDocuments;
    QByteArray htmlPayload;
    QString plainText;
    // 调试模式/环境变量开启时输出详细诊断；同一次读取内保持不变。
    const bool verbose = clipboardDebugEnabled();

    if (!OpenClipboard(nullptr))
        return result;
    {
        auto closeGuard = qScopeGuard([] { CloseClipboard(); });

        // 一次枚举同时拿到格式集合和顺序：集合用于判断可用性，顺序用于区分原生 DIBV5 与系统合成项。
        QVector<UINT> formats;
        const QStringList formatNames = clipboardFormatNames(&formats, verbose);
        const auto hasFormat = [&formats](UINT wanted) {
            return wanted != 0 && formats.contains(wanted);
        };
        // 诊断开关：输出格式清单，便于定位 QQ/微信/浏览器的私有格式。
        if (verbose)
            LogService::instance().info(QStringLiteral("剪贴板格式 %1 个：%2")
                                            .arg(formats.size())
                                            .arg(formatNames.join(QStringLiteral("、"))));

        // 本程序写回的内容必须跳过，否则会形成捕获循环。
        if (hasFormat(m_ownerMarker)) {
            result.selfWritten = true;
            return result;
        }

        // 文件列表不再直接忽略：QQ/微信复制图片时 FileDrop 可能直接指向原始文件。
        if (hasFormat(CF_HDROP))
            dropPaths = clipboardDropPaths();

        // 系统会把可转换格式追加在枚举末尾：先遇到 CF_DIB 再遇到 CF_DIBV5 说明后者是合成项。
        // 此时直接读 CF_DIB，既省掉一次系统转换，也避免合成导致原始透明度丢失（与 Qt 的判断一致）。
        bool hasDibV5 = false;
        bool hasDib = false;
        bool nativeDibV5 = false;
        for (UINT available : formats) {
            if (available == CF_DIBV5) {
                hasDibV5 = true;
                nativeDibV5 = !hasDib;
                if (nativeDibV5)
                    break;
            } else if (available == CF_DIB) {
                hasDib = true;
            }
        }

        // 同一编码可能有多个注册名，按族内顺序逐个尝试，读取逻辑完全共用。
        for (const EncodedFormatFamily &family : m_encodedFormats) {
            for (UINT candidate : family.formats) {
                if (!hasFormat(candidate))
                    continue;
                payload = encodedBytes(clipboardBytes(candidate));
                if (payload.isEmpty())
                    continue;
                encoding = family.encoding;
                break;
            }
            if (!encoding.isEmpty())
                break;
        }
        if (encoding.isEmpty() && (hasDib || hasDibV5)) {
            // 原生 DIBV5 优先；其余情况优先读 CF_DIB，避免系统把 DIB 再合成一份 DIBV5。
            if (nativeDibV5)
                dibFormat = CF_DIBV5;
            else if (hasDib)
                dibFormat = CF_DIB;
            else
                dibFormat = CF_DIBV5;
            payload = clipboardBytes(dibFormat);
            if (payload.isEmpty())
                dibFormat = 0;
        }

        // QQ/微信复制消息时会写富文本格式，其中 EditElement 的 filepath 指向原始图片/表情文件。
        const auto collectRichEdit = [&hasFormat, &richEditDocuments](UINT formatId) {
            if (!hasFormat(formatId))
                return;
            const QByteArray document = clipboardBytes(formatId);
            if (!document.isEmpty())
                richEditDocuments.append(document);
        };
        collectRichEdit(m_qqRichFormat);
        collectRichEdit(m_qqRichLegacyFormat);
        // 兼容格式名不同但内容同为 RichEdit XML 的版本；只按名称匹配，不解析任意格式。
        for (UINT available : formats) {
            if (available < 0xC000 || available == m_qqRichFormat
                || available == m_qqRichLegacyFormat || available == m_htmlFormat
                || available == m_ownerMarker)
                continue;
            wchar_t name[128] = {};
            const int length = GetClipboardFormatNameW(available, name, 128);
            if (length <= 0)
                continue;
            if (!QString::fromWCharArray(name, length).contains(QStringLiteral("RichEdit"),
                                                               Qt::CaseInsensitive))
                continue;
            const QByteArray document = clipboardBytes(available);
            if (!document.isEmpty())
                richEditDocuments.append(document);
        }

        if (hasFormat(m_htmlFormat))
            htmlPayload = clipboardBytes(m_htmlFormat);

        // Markdown 兜底：完全没有图片载荷和引用时才读文本，避免普通文本复制做无谓解析。
        if (encoding.isEmpty() && !dibFormat && dropPaths.isEmpty()
            && richEditDocuments.isEmpty() && htmlPayload.isEmpty())
            plainText = clipboardPlainText();
    }
    // 到这里剪贴板已经关闭，文件读取、解码、DIB 转换和 PNG 编码不再持有全局锁。

    // 解析所有引用来源：FileDrop、QQ/微信富文本 filepath、HTML 的 <img src>、Markdown 图片语法。
    // 同时保留来源类别，调试日志据此区分“用户多选文件”和“同一张图被多个格式重复引用”。
    struct LocalReference
    {
        QString path;
        QString source;
        int priority = 2; ///< 越小越可信：来源程序声明的引用优先于系统文件列表/临时副本
    };
    HtmlClipboard html;
    if (!htmlPayload.isEmpty())
        html = parseHtmlClipboard(htmlPayload);
    QVector<LocalReference> localReferences;
    const auto addLocalReference = [&localReferences](const QString &path, const QString &source,
                                                      int priority) {
        localReferences.append(LocalReference{path, source, priority});
    };
    for (const QString &path : std::as_const(dropPaths))
        addLocalReference(path, QStringLiteral("文件列表"), 2);
    for (const QByteArray &document : std::as_const(richEditDocuments)) {
        for (const QString &path : richEditFilePaths(document))
            addLocalReference(path, QStringLiteral("QQ/微信富文本"), 0);
    }
    QVector<QByteArray> inlineGifPayloads;
    for (const QString &source : std::as_const(html.imageSources)) {
        QByteArray inlineGif = decodeGifDataUri(source);
        if (!inlineGif.isEmpty()) {
            inlineGifPayloads.append(std::move(inlineGif));
            continue;
        }
        // 捕获流程不访问网络：HTML 里只使用 file:// 等本地路径，http(s) URL 一律忽略，
        // 因为仅凭 URL 后缀无法确认内容真的是图片，主动下载会引入额外攻击面。
        if (!localPathFromReference(source).isEmpty())
            addLocalReference(source, QStringLiteral("HTML"), 1);
    }
    if (!plainText.isEmpty()) {
        const QString markdownTarget = markdownImageTarget(plainText);
        if (!markdownTarget.isEmpty()) {
            QByteArray inlineGif = decodeGifDataUri(markdownTarget);
            if (!inlineGif.isEmpty())
                inlineGifPayloads.append(std::move(inlineGif));
            else if (!localPathFromReference(markdownTarget).isEmpty())
                addLocalReference(markdownTarget, QStringLiteral("Markdown"), 1);
        }
    }

    // 调试模式（或 MEMESERVANT2_CLIPBOARD_DEBUG=1）下输出引用来源和每个候选文件的详情，
    // 便于定位“复制了动图却只捕获到静态帧”这类问题；正常运行时保持安静。
    if (verbose) {
        for (int index = 0; index < richEditDocuments.size(); ++index) {
            LogService::instance().info(
                QStringLiteral("调试：富文本格式 %1 结构：%2")
                    .arg(index + 1)
                    .arg(describeRichEditStructure(richEditDocuments.at(index))));
        }
        QHash<QString, int> sourceCounts;
        for (const LocalReference &reference : std::as_const(localReferences))
            ++sourceCounts[reference.source];
        LogService::instance().info(
            QStringLiteral("剪贴板引用统计：本地路径 %1 个（文件列表 %2、富文本 %3、HTML %4、Markdown %5），"
                           "HTML 图片 %6 个，富文本格式 %7 份，内嵌 GIF %8 个")
                .arg(localReferences.size())
                .arg(sourceCounts.value(QStringLiteral("文件列表")))
                .arg(sourceCounts.value(QStringLiteral("QQ/微信富文本")))
                .arg(sourceCounts.value(QStringLiteral("HTML")))
                .arg(sourceCounts.value(QStringLiteral("Markdown")))
                .arg(html.imageSources.size())
                .arg(richEditDocuments.size())
                .arg(inlineGifPayloads.size()));
        for (const LocalReference &reference : std::as_const(localReferences)) {
            LogService::instance().info(QStringLiteral("调试：剪贴板引用（%1）：%2")
                                            .arg(reference.source,
                                                 QDir::toNativeSeparators(reference.path)));
        }
    }

    // 1) 本地文件引用：QQ/微信、资源管理器、Markdown 绝对路径里最保真的来源。
    struct LocalCandidate
    {
        QByteArray bytes;
        QString format;
        QString path;
        QString source;
        int priority = 2;
    };
    QVector<LocalCandidate> localCandidates;
    QStringList seenPaths;
    QVector<QByteArray> seenContents;
    for (const LocalReference &reference : std::as_const(localReferences)) {
        const QString path = localPathFromReference(reference.path);
        if (path.isEmpty())
            continue;
        const QString dedupeKey = QDir::cleanPath(path).toLower();
        if (seenPaths.contains(dedupeKey))
            continue;
        seenPaths.append(dedupeKey);
        const QByteArray bytes = readReferencedImageFile(path);
        if (bytes.isEmpty()) {
            if (verbose) {
                LogService::instance().info(QStringLiteral("调试：剪贴板引用文件不可读或不是图片（%1）：%2")
                                                .arg(reference.source,
                                                     QDir::toNativeSeparators(path)));
            }
            continue;
        }
        const QString format = ImageBytes::formatOf(bytes);
        int width = 0;
        int height = 0;
        if (format.isEmpty() || !ImageBytes::sizeOf(bytes, &width, &height)) {
            if (verbose) {
                LogService::instance().info(QStringLiteral("调试：剪贴板引用文件无法识别图片信息（%1）：%2")
                                                .arg(reference.source,
                                                     QDir::toNativeSeparators(path)));
            }
            continue;
        }
        // 同一个逻辑图片可能被多个来源以不同路径重复发布（QQ/微信常见）；内容相同只保留一次，
        // 否则多候选保护规则会把同一张动图误判成多张不同图片而放弃捕获。
        const QByteArray fingerprint = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        if (seenContents.contains(fingerprint)) {
            if (verbose) {
                LogService::instance().info(QStringLiteral("调试：剪贴板引用内容重复，已合并（%1）：%2")
                                                .arg(reference.source,
                                                     QDir::toNativeSeparators(path)));
            }
            continue;
        }
        seenContents.append(fingerprint);
        LocalCandidate candidate;
        candidate.bytes = bytes;
        candidate.format = format;
        candidate.path = path;
        candidate.source = reference.source;
        candidate.priority = reference.priority;
        localCandidates.append(candidate);
        if (verbose) {
            LogService::instance().info(
                QStringLiteral("调试：剪贴板图片候选 %1（%2）：格式=%3，尺寸=%4x%5，多帧=%6，字节=%7，路径=%8")
                    .arg(localCandidates.size())
                    .arg(reference.source)
                    .arg(format)
                    .arg(width)
                    .arg(height)
                    .arg(ImageBytes::looksAnimated(bytes) ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(bytes.size())
                    .arg(QDir::toNativeSeparators(path)));
        }
    }
    if (verbose && localCandidates.isEmpty() && !localReferences.isEmpty())
        LogService::instance().info(QStringLiteral("调试：剪贴板引用的本地文件都不可读或不是图片"));
    const LocalCandidate *chosenLocal = nullptr;
    if (localCandidates.size() == 1) {
        chosenLocal = &localCandidates.first();
    } else if (localCandidates.size() > 1) {
        // 多个文件同时被引用时只接受唯一一个多帧动图，避免多选文件后抓到错误图片。
        int animatedCount = 0;
        const LocalCandidate *animatedCandidate = nullptr;
        for (const LocalCandidate &candidate : std::as_const(localCandidates)) {
            if (!ImageBytes::looksAnimated(candidate.bytes))
                continue;
            ++animatedCount;
            animatedCandidate = &candidate;
        }
        if (animatedCount == 1) {
            chosenLocal = animatedCandidate;
        } else {
            // 单帧 GIF 也在候选里唯一时接受：FileDrop 可能给的是转码静态图，富文本 filepath 才是 GIF。
            int gifCount = 0;
            const LocalCandidate *gifCandidate = nullptr;
            for (const LocalCandidate &candidate : std::as_const(localCandidates)) {
                if (candidate.format != QStringLiteral("gif"))
                    continue;
                ++gifCount;
                gifCandidate = &candidate;
            }
            if (gifCount == 1) {
                chosenLocal = gifCandidate;
            } else {
                // 仍无法唯一确定时，优先采用来源程序自己声明的引用（富文本 > HTML/Markdown > 文件列表）：
                // QQ/微信会把原图写进富文本 filepath，同时用 HTML/文件列表再放一份临时转码副本。
                // 只采用“最高优先级来源恰好一个候选”的情况；同一来源多个文件仍按多图处理，避免多选后猜错。
                int bestPriority = 3;
                for (const LocalCandidate &candidate : std::as_const(localCandidates)) {
                    if (candidate.priority < bestPriority)
                        bestPriority = candidate.priority;
                }
                int bestCount = 0;
                const LocalCandidate *bestCandidate = nullptr;
                for (const LocalCandidate &candidate : std::as_const(localCandidates)) {
                    if (candidate.priority != bestPriority)
                        continue;
                    ++bestCount;
                    bestCandidate = &candidate;
                }
                if (bestCount == 1) {
                    chosenLocal = bestCandidate;
                    if (verbose) {
                        LogService::instance().info(
                            QStringLiteral("调试：多候选无法按动图唯一化，按来源优先级选择（%1）：%2")
                                .arg(bestCandidate->source,
                                     QDir::toNativeSeparators(bestCandidate->path)));
                    }
                } else {
                    LogService::instance().info(
                        QStringLiteral("剪贴板引用了多张图片，未自动捕获（候选 %1 个）")
                            .arg(localCandidates.size()));
                }
            }
        }
    }
    if (chosenLocal) {
        result.encoded = chosenLocal->bytes;
        result.format = chosenLocal->format;
        result.isValid = ImageBytes::sizeOf(result.encoded, &result.width, &result.height);
        if (verbose) {
            LogService::instance().info(QStringLiteral("调试：已选择剪贴板引用文件（%1）：%2")
                                            .arg(chosenLocal->source,
                                                 QDir::toNativeSeparators(chosenLocal->path)));
        }
        LogService::instance().info(QStringLiteral("已从剪贴板引用的本地文件捕获图片（%1）")
                                        .arg(result.format));
    }

    // 2) 注册编码格式（GIF 族优先于静态 PNG/JPEG）。
    if (!result.isValid && !encoding.isEmpty()) {
        result.encoded = std::move(payload);
        result.format = encoding;
        result.isValid = ImageBytes::sizeOf(result.encoded, &result.width, &result.height);
        if (verbose && result.isValid)
            LogService::instance().info(QStringLiteral("调试：回退到剪贴板注册编码格式 %1").arg(encoding));
    }

    // 3) HTML/Markdown 里内嵌的 data URI GIF，不需要联网。
    if (!result.isValid) {
        for (const QByteArray &inlineGif : std::as_const(inlineGifPayloads)) {
            int width = 0;
            int height = 0;
            if (!ImageBytes::sizeOf(inlineGif, &width, &height))
                continue;
            result.encoded = inlineGif;
            result.format = QStringLiteral("gif");
            result.width = width;
            result.height = height;
            result.isValid = true;
            LogService::instance().info(QStringLiteral("已从剪贴板引用的内嵌数据捕获 GIF"));
            break;
        }
    }

    // 4) DIB 静态兜底（截图、普通图片复制）。
    if (!result.isValid && dibFormat) {
        const QImage image = imageFromDib(payload);
        if (!image.isNull()) {
            result.width = image.width();
            result.height = image.height();
            // 超限图不再做整图 PNG 编码，交给末尾统一的尺寸校验丢弃。
            if (result.width <= kMaxClipboardImageSide && result.height <= kMaxClipboardImageSide) {
                QBuffer pngBuffer(&result.encoded);
                pngBuffer.open(QIODevice::WriteOnly);
                result.isValid = image.save(&pngBuffer, "PNG");
                result.format = QStringLiteral("png");
            }
            if (verbose && result.isValid) {
                LogService::instance().info(QStringLiteral("调试：回退到剪贴板 DIB 静态位图（%1x%2）")
                                                .arg(result.width)
                                                .arg(result.height));
            }
        }
    }

    if (result.width > kMaxClipboardImageSide || result.height > kMaxClipboardImageSide) {
        LogService::instance().warning(QStringLiteral("剪贴板图片尺寸异常，已忽略"));
        result.isValid = false;
    }
    return result;
}

// 调试快捷键专用：读取一次当前剪贴板格式和捕获结果，不改状态、不发捕获信号。
// 格式清单用于确认来源程序写了哪些格式，捕获结果用于判断“为什么没有弹出捕获浮窗”。
WinClipboard::ClipboardSnapshot WinClipboard::debugSnapshot()
{
    ClipboardSnapshot snapshot;
    if (!OpenClipboard(nullptr))
        return snapshot;
    {
        auto closeGuard = qScopeGuard([] { CloseClipboard(); });
        snapshot.opened = true;
        QVector<UINT> formats;
        snapshot.formats = clipboardFormatNames(&formats, true);
        snapshot.selfWritten = m_ownerMarker != 0 && formats.contains(m_ownerMarker);
    }
    // 关闭剪贴板后再按正常捕获路径读取：文件读取、解码和 DIB 转换都不持有全局锁。
    snapshot.image = readClipboard();
    // 读取延迟渲染格式可能让所有者把数据真正写进剪贴板，从而给自己投递一条新的
    // WM_CLIPBOARDUPDATE；刷新序列号，避免调试快照的副作用被当成新的捕获事件。
    m_lastSequence = GetClipboardSequenceNumber();
    snapshot.selfWritten = snapshot.selfWritten || snapshot.image.selfWritten;
    return snapshot;
}

// 处理一次 WM_CLIPBOARDUPDATE。这个通知只说明“系统向剪贴板写过内容”，并不等于用户刚复制了
// 新图片：读取延迟渲染格式会让所有者落盘数据、安全桌面/UAC 切换等系统行为也可能重新发布剪贴板，
// 此时通知里的图片仍是上一张。先用剪贴板序列号滤掉没有实际写入的重复通知，再把内容交给业务层；
// 业务层再按图片内容指纹过滤“内容相同的重新发布”。
void WinClipboard::handleClipboardUpdate()
{
    const bool verbose = clipboardDebugEnabled();
    const DWORD sequence = GetClipboardSequenceNumber();
    if (verbose) {
        LogService::instance().info(QStringLiteral("调试：剪贴板更新，序列号=%1，所有者：%2")
                                        .arg(sequence)
                                        .arg(clipboardOwnerSummary()));
    }
    if (sequence != 0 && sequence == m_lastSequence) {
        LogService::instance().info(QStringLiteral("剪贴板序列号未变化（%1），没有新的剪贴板写入，"
                                                   "已忽略本次更新通知")
                                        .arg(sequence));
        return;
    }

    const CapturedImage image = readClipboard();
    // 读取延迟渲染格式同样会让剪贴板序列号前进；记录读取后的序列号，使这一自激产生的
    // 后续通知在入口处就被过滤掉，而不是再完整读取一遍剪贴板。
    const DWORD sequenceAfterRead = GetClipboardSequenceNumber();
    m_lastSequence = sequenceAfterRead != 0 ? sequenceAfterRead : sequence;
    if (verbose && sequenceAfterRead != 0 && sequenceAfterRead != sequence) {
        LogService::instance().info(QStringLiteral("调试：本次读取使剪贴板序列号从 %1 变为 %2")
                                        .arg(sequence)
                                        .arg(sequenceAfterRead));
    }
    dispatchCaptured(image);
}

// 丢弃本程序回写和无效事件，只向上层发出有效图片。
void WinClipboard::dispatchCaptured(const CapturedImage &image)
{
    if (image.selfWritten) {
        LogService::instance().info(QStringLiteral("已忽略本程序写入的剪贴板图片事件"));
        return;
    }
    if (!image.isValid) {
        LogService::instance().info(QStringLiteral("剪贴板更新未包含可捕获的单个逻辑图片"));
        return;
    }
    emit imageCaptured(image);
}

// 同时发布自写标记、PNG、原始格式、CF_DIB 和 CF_DIBV5，兼容不同粘贴目标。
bool WinClipboard::writeImage(const CapturedImage &captured, QString *error)
{
    QImage image;
    QByteArray encoded = captured.encoded;
    QString format = captured.format.toLower();
    if (encoded.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有可用于写回的原始图片数据。");
        return false;
    }
    image.loadFromData(encoded, captured.format.toUpper().toLatin1().constData());
    if (image.isNull())
        image.loadFromData(encoded);
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("选择表情包时原图解码失败。");
        return false;
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(m_window ? m_window : nullptr))
            break;
        Sleep(30);
        if (attempt == 9) {
            if (error)
                *error = QStringLiteral("系统剪贴板暂时被其他程序占用。");
            return false;
        }
    }
    auto closeGuard = qScopeGuard([] { CloseClipboard(); });
    if (!EmptyClipboard()) {
        if (error)
            *error = QStringLiteral("清空系统剪贴板失败。");
        return false;
    }

    QStringList writtenFormats;
    QStringList failedFormats;
    // 只有 SetClipboardData 成功后句柄所有权才转移给系统，失败时必须手动释放。
    auto putBytes = [&writtenFormats, &failedFormats](UINT clipboardFormat, const QByteArray &bytes,
                                                      const QString &name) {
        HANDLE handle = allocateBytes(bytes);
        if (!handle) {
            failedFormats.append(name + QStringLiteral("(内存分配失败)"));
            return false;
        }
        if (!SetClipboardData(clipboardFormat, handle)) {
            const DWORD nativeError = GetLastError();
            GlobalFree(handle); // Clipboard only owns the handle after SetClipboardData succeeds.
            failedFormats.append(name + QStringLiteral("(Win32错误 %1)").arg(nativeError));
            return false;
        }
        writtenFormats.append(name);
        return true;
    };

    const QByteArray marker = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8();
    putBytes(m_ownerMarker, marker, QStringLiteral("自写标记"));

    // Always publish a PNG representation. Registered PNG is broadly supported by Chromium,
    // Office and Qt applications, regardless of whether the stored original was GIF or JPEG.
    QByteArray png = encoded;
    const QByteArray pngSignature = QByteArray::fromHex("89504e470d0a1a0a");
    if (!png.startsWith(pngSignature)) {
        png.clear();
        QBuffer pngBuffer(&png);
        pngBuffer.open(QIODevice::WriteOnly);
        if (!image.save(&pngBuffer, "PNG"))
            png.clear();
    }
    // 写入仍使用每个编码族的历史注册名（族内首个格式），写回行为保持不变。
    const bool pngWritten = !png.isEmpty()
                            && putBytes(encodedFormatId(QStringLiteral("png")), png, QStringLiteral("PNG"));

    // Also retain the original encoded stream when it has a distinct registered format.
    if (format == QLatin1String("gif"))
        putBytes(encodedFormatId(QStringLiteral("gif")), encoded, QStringLiteral("GIF原图"));
    else if (format == QLatin1String("jpg") || format == QLatin1String("jpeg"))
        putBytes(encodedFormatId(QStringLiteral("jpg")), encoded, QStringLiteral("JPEG原图"));

    const bool dibWritten = putBytes(CF_DIB, imageToDib(image), QStringLiteral("CF_DIB"));
    // 只有原图确实带透明像素时才投递 CF_DIBV5：这份 32 位 DIB 会一直留到剪贴板被替换，
    // 对不透明图片额外占用一份整图大小的私有内存，却没有任何粘贴方需要它。
    bool dibV5Written = false;
    if (hasTransparency(image))
        dibV5Written = putBytes(CF_DIBV5, imageToDibV5(image), QStringLiteral("CF_DIBV5"));
    const bool imageWritten = pngWritten || dibWritten || dibV5Written;
    if (!imageWritten) {
        if (error) {
            *error = QStringLiteral("系统剪贴板未接受任何可粘贴的图片格式：%1")
                         .arg(failedFormats.join(QStringLiteral("，")));
        }
        LogService::instance().error(QStringLiteral("图片写入系统剪贴板失败：")
                                     + failedFormats.join(QStringLiteral("，")));
        return false;
    }

    const QString failureSuffix = failedFormats.isEmpty()
                                      ? QString()
                                      : QStringLiteral("；失败项：")
                                            + failedFormats.join(QStringLiteral("，"));
    LogService::instance().info(QStringLiteral("已将图片写入系统剪贴板，格式：")
                                + writtenFormats.join(QStringLiteral("、")) + failureSuffix);
    // 写回本身会触发一条 WM_CLIPBOARDUPDATE；记录写入后的序列号让它在入口处被跳过，
    // selfWritten 标记仍保留为兜底（序列号取不到时继续依赖标记过滤）。
    m_lastSequence = GetClipboardSequenceNumber();
    return true;
}

// 仅消费属于本消息窗口的 WM_CLIPBOARDUPDATE。
bool WinClipboard::processMessage(MSG *message)
{
    return message->hwnd == m_window && message->message == WM_CLIPBOARDUPDATE;
}
