// 声明 GIF 兼容模式的独立处理路线：把动画帧按帧序号均匀采样后拼接成一张静态长图。
#ifndef AI_GIFCOMPAT_H
#define AI_GIFCOMPAT_H

#include <QByteArray>

// 该路线只服务于 AI 识图：视觉模型只接受静态图片时，用长图向它表达 GIF 的动作过程。
namespace GifCompat {

/// 拼接结果允许发送的最大字节数（8MB = 8388608 bytes）；帧数由每帧编码后大小和该上限反推。
constexpr qint64 kMaxCompositeBytes = 8 * 1024 * 1024;

struct Composite
{
    /// 拼接后的 PNG；为空表示无法在限制内拼接，调用方应回退到首帧方案。
    QByteArray png;
    /// 实际包含的帧数（含首尾）。
    int frameCount = 0;
};

/// 从原始 GIF 字节生成“多帧纵向拼接长图”；失败时返回空 Composite。
Composite compose(const QByteArray &gifBytes);

} // namespace GifCompat

#endif
