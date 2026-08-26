// 只处理一个逻辑图片对象；文件列表和本程序自写事件不会触发捕获。
#include "platforms/win/winclipboard.h"

#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QImageReader>
#include <QScopeGuard>
#include "storage/logservice.h"
#include <QTextStream>

namespace {
constexpr wchar_t kClassName[] = L"MemeServant2ClipboardWindow";

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

// 保留剪贴板提供的完整编码块；PNG/JPEG/GIF 解码器会忽略尾部填充。
QByteArray encodedBytes(QByteArray data)
{
    // 全局内存可能按分配粒度补零，解码器只需要有效前缀。
    return data;
}

// 将 24/32 位 bottom-up 或 top-down DIB 转换为 Qt ARGB 图像。
QImage imageFromDib(const QByteArray &bytes)
{
    if (bytes.size() < int(sizeof(BITMAPINFOHEADER)))
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
    const qsizetype stride = ((width * header->biBitCount + 31) / 32) * 4;
    if (stride <= 0 || stride > (bytes.size() - qsizetype(sizeof(BITMAPINFOHEADER))) / height)
        return {};
    QImage image(width, height, QImage::Format_ARGB32);
    const char *pixels = bytes.constData() + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < height; ++y) {
        QRgb *target = reinterpret_cast<QRgb *>(image.scanLine(topDown ? y : height - y - 1));
        const char *source = pixels + stride * y;
        for (int x = 0; x < width; ++x) {
            const uchar *pixel = reinterpret_cast<const uchar *>(source + x * bytesPerPixel);
            const uchar blue = pixel[0];
            const uchar green = pixel[1];
            const uchar red = pixel[2];
            const uchar alpha = bytesPerPixel == 4 ? pixel[3] : 255;
            target[x] = qRgba(red, green, blue, alpha);
        }
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

// 注册自写标记和常见图片格式，后续读写复用这些格式 ID。
WinClipboard::WinClipboard(QObject *parent)
    : QObject(parent)
{
    m_ownerMarker = RegisterClipboardFormatW(L"MemeServant2.SelfWrite.v1");
    m_pngFormat = RegisterClipboardFormatW(L"PNG");
    m_gifFormat = RegisterClipboardFormatW(L"image/gif");
    m_jpegFormat = RegisterClipboardFormatW(L"JPEG");
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

// 按 GIF、PNG、JPEG、DIB 优先级读取单个图片并填充尺寸和原始编码。
CapturedImage WinClipboard::readClipboard()
{
    CapturedImage result;
    if (!OpenClipboard(nullptr))
        return result;
    auto closeGuard = qScopeGuard([] { CloseClipboard(); });
    if (GetClipboardData(m_ownerMarker)) {
        result.selfWritten = true;
        return result;
    }
    if (IsClipboardFormatAvailable(CF_HDROP))
        return result;

    const bool hasPng = IsClipboardFormatAvailable(m_pngFormat);
    const bool hasGif = IsClipboardFormatAvailable(m_gifFormat);
    const bool hasJpeg = IsClipboardFormatAvailable(m_jpegFormat);
    const bool hasDib = IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5);
    if (!hasPng && !hasGif && !hasJpeg && !hasDib)
        return result;

    UINT selectedFormat = 0;
    QString format;
    if (hasGif) {
        selectedFormat = m_gifFormat;
        format = QStringLiteral("gif");
    } else if (hasPng) {
        selectedFormat = m_pngFormat;
        format = QStringLiteral("png");
    } else if (hasJpeg) {
        selectedFormat = m_jpegFormat;
        format = QStringLiteral("jpg");
    }
    if (selectedFormat) {
        HANDLE handle = GetClipboardData(selectedFormat);
        if (handle) {
            result.encoded = encodedBytes(globalBytes(handle));
            result.format = format;
        }
    }
    if (!result.encoded.isEmpty()) {
        result.isValid = decodeSize(result.encoded, &result.width, &result.height);
    } else if (hasDib) {
        const UINT dibFormat = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB;
        HANDLE handle = GetClipboardData(dibFormat);
        const QByteArray bytes = handle ? globalBytes(handle) : QByteArray();
        QImage image = imageFromDib(bytes);
        if (!image.isNull()) {
            QBuffer pngBuffer(&result.encoded);
            pngBuffer.open(QIODevice::WriteOnly);
            result.isValid = image.save(&pngBuffer, "PNG");
            result.format = QStringLiteral("png");
            result.width = image.width();
            result.height = image.height();
        }
    }
    if (result.isValid && (result.width > 8192 || result.height > 8192)) {
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
    const bool pngWritten = !png.isEmpty()
                            && putBytes(m_pngFormat, png, QStringLiteral("PNG"));

    // Also retain the original encoded stream when it has a distinct registered format.
    if (format == QLatin1String("gif"))
        putBytes(m_gifFormat, encoded, QStringLiteral("GIF原图"));
    else if (format == QLatin1String("jpg") || format == QLatin1String("jpeg"))
        putBytes(m_jpegFormat, encoded, QStringLiteral("JPEG原图"));

    const bool dibWritten = putBytes(CF_DIB, imageToDib(image), QStringLiteral("CF_DIB"));
    const bool dibV5Written = putBytes(CF_DIBV5, imageToDibV5(image), QStringLiteral("CF_DIBV5"));
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
