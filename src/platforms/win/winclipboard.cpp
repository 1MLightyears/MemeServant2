// 只处理一个逻辑图片对象；文件列表和本程序自写事件不会触发捕获。
#include "platforms/win/winclipboard.h"

#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QImageReader>
#include <QScopeGuard>
#include "storage/logservice.h"
#include <QTextStream>
#include <initializer_list>
#include <utility>

namespace {
constexpr wchar_t kClassName[] = L"MemeServant2ClipboardWindow";
// 剪贴板图片边长上限：超过视为异常载荷，避免解码/编码超大图。
constexpr int kMaxClipboardImageSide = 8192;

// 从可移动全局内存复制有限大小的字节，避免锁定失败或异常大小导致崩溃。
QByteArray globalBytes(HANDLE handle)
{
    const SIZE_T size = GlobalSize(handle);
    if (!size || size > 64 * 1024 * 1024)
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

// 只读取编码头得到宽高，避免捕获时为尺寸校验解码整张图片。
bool decodeSize(const QByteArray &bytes, int *width, int *height)
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

// 接收剪贴板更新消息，读取当前内容并转交业务层过滤。
LRESULT CALLBACK WinClipboard::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *clipboard = reinterpret_cast<WinClipboard *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CLIPBOARDUPDATE && clipboard) {
        clipboard->dispatchCaptured(clipboard->readClipboard());
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

// 按注册编码格式、原生 DIBV5、CF_DIB 的优先级读取单个图片。剪贴板全局锁内只做格式判断和
// 字节拷贝，DIB 转换与 PNG 编码放到关闭剪贴板之后，避免长时间阻塞其他程序。
CapturedImage WinClipboard::readClipboard()
{
    CapturedImage result;
    QByteArray payload;
    QString encoding;   // 非空表示 payload 是注册编码格式（png/jpg/gif）的字节流
    UINT dibFormat = 0; // 非 0 表示 payload 是 DIB 字节流

    if (!OpenClipboard(nullptr))
        return result;
    {
        auto closeGuard = qScopeGuard([] { CloseClipboard(); });

        // 一次枚举同时拿到格式集合和顺序：集合用于判断可用性，顺序用于区分原生 DIBV5 与系统合成项。
        QVector<UINT> formats;
        UINT format = 0;
        while ((format = EnumClipboardFormats(format)) != 0)
            formats.append(format);
        const auto hasFormat = [&formats](UINT wanted) { return formats.contains(wanted); };

        // 本程序写回的内容必须跳过，否则会形成捕获循环。
        if (m_ownerMarker && hasFormat(m_ownerMarker)) {
            result.selfWritten = true;
            return result;
        }
        // 复制的是文件而不是图片时剪贴板里没有像素数据，交给系统粘贴即可。
        if (hasFormat(CF_HDROP))
            return result;

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
        if (encoding.isEmpty()) {
            if (!hasDib && !hasDibV5)
                return result;
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
    }
    // 到这里剪贴板已经关闭，解码、DIB 转换和 PNG 编码不再持有全局锁。

    if (!encoding.isEmpty()) {
        result.encoded = std::move(payload);
        result.format = encoding;
        result.isValid = decodeSize(result.encoded, &result.width, &result.height);
    } else if (dibFormat) {
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
        }
    }
    if (result.width > kMaxClipboardImageSide || result.height > kMaxClipboardImageSide) {
        LogService::instance().warning(QStringLiteral("剪贴板图片尺寸异常，已忽略"));
        result.isValid = false;
    }
    return result;
}

// 丢弃本程序回写、无效或非单图事件，只向上层发出有效图片。
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
    return true;
}

// 仅消费属于本消息窗口的 WM_CLIPBOARDUPDATE。
bool WinClipboard::processMessage(MSG *message)
{
    return message->hwnd == m_window && message->message == WM_CLIPBOARDUPDATE;
}
