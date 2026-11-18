// GIF兼容模式的独立处理路线：测量每帧大小后按帧序号均匀采样，把多帧拼成一张静态长图。
#include "ai/gifcompat.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPoint>
#include <QVector>
#include <algorithm>
#include <climits>
#include <utility>

namespace {
// 扫描阶段各种上限：异常长/异常大的动画不应把内存或 CPU 拉满。
constexpr int kMaxScanFrames = 400;
constexpr qint64 kMaxScanPixelBytes = 512 * 1024 * 1024;
// 拼接画布原始像素内存上限；超过时减少帧数，避免分配超大 QImage。
constexpr qint64 kMaxCanvasPixelBytes = 64 * 1024 * 1024;
// 最终长图长边上限：视觉模型会按长边降采样，再大只会浪费请求体。
constexpr int kMaxFinalSide = 2048;
// 拼接图的帧间距（缩放后的像素）。
constexpr int kFrameGap = 6;
// 拼接后仍超过请求上限时逐级缩小重编码。
constexpr int kMaxShrinkAttempts = 6;

struct FrameInfo
{
    qint64 encodedBytes = 0;
    int width = 0;
    int height = 0;
};

// 按帧序号均匀采样，始终包含头尾（例如 10 帧取 4 帧就是第 0/3/6/9 帧）。
QVector<int> sampleFrameIndexes(const QVector<FrameInfo> &frames, int count)
{
    QVector<int> indexes;
    if (count <= 0 || frames.isEmpty())
        return indexes;
    indexes.reserve(count);
    if (count == 1) {
        indexes.append(0);
        return indexes;
    }
    for (int index = 0; index < count; ++index) {
        const int frame = qRound(static_cast<double>(index) * (frames.size() - 1) / (count - 1));
        if (indexes.isEmpty() || indexes.last() != frame)
            indexes.append(frame);
    }
    return indexes;
}

// 第一遍：只扫描每帧的编码后大小和尺寸，不保留像素。
bool inspectFrames(const QByteArray &gifBytes, QVector<FrameInfo> *frames)
{
    QBuffer buffer(const_cast<QByteArray *>(&gifBytes));
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "GIF");
    if (!reader.canRead() || reader.imageCount() <= 1)
        return false;

    qint64 scannedPixelBytes = 0;
    for (int index = 0; index < kMaxScanFrames; ++index) {
        const QImage frame = reader.read();
        if (frame.isNull())
            break;
        FrameInfo info;
        info.width = frame.width();
        info.height = frame.height();
        // “每帧大小”用编码后的 PNG 字节数衡量；编码失败时退回原始像素大小估算。
        QByteArray png;
        QBuffer pngBuffer(&png);
        pngBuffer.open(QIODevice::WriteOnly);
        if (frame.save(&pngBuffer, "PNG") && !png.isEmpty())
            info.encodedBytes = png.size();
        else
            info.encodedBytes = static_cast<qint64>(frame.width()) * frame.height() * 4;
        frames->append(info);
        scannedPixelBytes += static_cast<qint64>(info.width) * info.height * 4;
        if (scannedPixelBytes > kMaxScanPixelBytes)
            break;
    }
    return frames->size() >= 2;
}

// 根据每帧大小和上限，从多到少找能放下的最大帧数；连头尾两帧都放不下时返回空。
QVector<int> selectFrames(const QVector<FrameInfo> &frames)
{
    qint64 smallestFrameBytes = frames.first().encodedBytes;
    for (const FrameInfo &frame : frames)
        smallestFrameBytes = qMin(smallestFrameBytes, frame.encodedBytes);
    smallestFrameBytes = qMax<qint64>(smallestFrameBytes, 1);
    // 用最小帧大小估算帧数上限，避免从异常大的帧数开始逐个尝试。
    const int frameCount = static_cast<int>(frames.size());
    const int estimatedMaximum = static_cast<int>(
        qMin<qint64>(frameCount, GifCompat::kMaxCompositeBytes / smallestFrameBytes));
    const int start = qBound(2, estimatedMaximum, frameCount);
    for (int count = start; count >= 2; --count) {
        const QVector<int> indexes = sampleFrameIndexes(frames, count);
        if (indexes.size() != count)
            continue;
        qint64 estimatedBytes = 16 * 1024; // 给图片容器和请求开销留余量
        qint64 canvasHeight = 0;
        int canvasWidth = 0;
        for (int index : indexes) {
            const FrameInfo &frame = frames.at(index);
            estimatedBytes += frame.encodedBytes;
            canvasWidth = qMax(canvasWidth, frame.width);
            canvasHeight += frame.height;
        }
        canvasHeight += static_cast<qint64>(kFrameGap) * (indexes.size() - 1);
        const qint64 canvasPixelBytes = static_cast<qint64>(canvasWidth) * canvasHeight * 4;
        if (estimatedBytes <= GifCompat::kMaxCompositeBytes
            && canvasPixelBytes <= kMaxCanvasPixelBytes) {
            return indexes;
        }
    }
    return {};
}

// 第二遍：只解码选中的帧，按最终长边上限统一缩放后纵向拼接。
QImage composeImage(const QByteArray &gifBytes, const QVector<FrameInfo> &frames,
                    const QVector<int> &indexes)
{
    int sourceWidth = 0;
    qint64 sourceHeight = 0;
    for (int index : indexes) {
        sourceWidth = qMax(sourceWidth, frames.at(index).width);
        sourceHeight += frames.at(index).height;
    }
    sourceHeight += static_cast<qint64>(kFrameGap) * (indexes.size() - 1);
    if (sourceWidth <= 0 || sourceHeight <= 0)
        return {};

    const int longestSide = qMax(sourceWidth, static_cast<int>(qMin<qint64>(sourceHeight, INT_MAX)));
    const double scale = qMin(1.0, static_cast<double>(kMaxFinalSide) / qMax(longestSide, 1));
    // 先用扫描记录算出每帧缩放后的目标高度，画布高度因此可以在解码前确定。
    QVector<QSize> targetSizes;
    targetSizes.reserve(indexes.size());
    int canvasWidth = 0;
    qint64 canvasHeight = 0;
    for (int index : indexes) {
        const FrameInfo &frame = frames.at(index);
        const int width = qMax(1, qRound(frame.width * scale));
        const int height = qMax(1, qRound(frame.height * scale));
        targetSizes.append(QSize(width, height));
        canvasWidth = qMax(canvasWidth, width);
        canvasHeight += height;
    }
    canvasHeight += static_cast<qint64>(kFrameGap) * (indexes.size() - 1);
    if (canvasWidth <= 0 || canvasHeight <= 0
        || static_cast<qint64>(canvasWidth) * canvasHeight * 4 > kMaxCanvasPixelBytes)
        return {};

    QImage strip(canvasWidth, static_cast<int>(canvasHeight), QImage::Format_RGB32);
    if (strip.isNull())
        return {};
    // 原生 RGB32 画布铺白底，透明 GIF 帧也能清晰呈现。
    strip.fill(Qt::white);
    QPainter painter(&strip);

    QBuffer buffer(const_cast<QByteArray *>(&gifBytes));
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "GIF");
    if (!reader.canRead())
        return {};
    int frameIndex = 0;
    int targetIndex = 0;
    int y = 0;
    while (targetIndex < indexes.size()) {
        const QImage frame = reader.read();
        if (frame.isNull())
            break;
        if (frameIndex++ != indexes.at(targetIndex))
            continue;
        const QSize target = targetSizes.at(targetIndex);
        const QImage scaled = frame.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!scaled.isNull())
            painter.drawImage(QPoint((canvasWidth - scaled.width()) / 2, y), scaled);
        y += target.height() + kFrameGap;
        ++targetIndex;
    }
    painter.end();
    if (targetIndex < indexes.size())
        return {};
    return strip;
}

// 把画布编码为 PNG；极端情况下仍超上限时逐级缩小重编码。
QByteArray encodeWithinLimit(QImage image)
{
    for (int attempt = 0; attempt < kMaxShrinkAttempts; ++attempt) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG") || png.isEmpty())
            return {};
        if (png.size() <= GifCompat::kMaxCompositeBytes)
            return png;
        if (image.width() <= 64 || image.height() <= 64)
            return {};
        image = image.scaled(qMax(1, image.width() * 3 / 4), qMax(1, image.height() * 3 / 4),
                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return {};
}
} // namespace

namespace GifCompat {

Composite compose(const QByteArray &gifBytes)
{
    Composite result;
    if (gifBytes.isEmpty())
        return result;
    QVector<FrameInfo> frames;
    if (!inspectFrames(gifBytes, &frames))
        return result;

    const QVector<int> indexes = selectFrames(frames);
    if (indexes.size() < 2)
        return result;

    const QImage strip = composeImage(gifBytes, frames, indexes);
    if (strip.isNull())
        return result;
    QByteArray png = encodeWithinLimit(strip);
    if (png.isEmpty())
        return result;

    result.png = std::move(png);
    result.frameCount = indexes.size();
    return result;
}

} // namespace GifCompat
