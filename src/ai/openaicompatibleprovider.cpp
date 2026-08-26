// Endpoint必须是完整chat/completions URL；密钥只进入Authorization头，绝不写日志。
#include "ai/openaicompatibleprovider.h"

#include "core/appstrings.h"

#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;

// AI Vision 不稳定支持 GIF，因此只取第一帧并转换为 PNG。
QByteArray visionBytes(const QByteArray &bytes, const QString &format)
{
    if (!format.contains(QLatin1String("gif"), Qt::CaseInsensitive))
        return bytes;
    QBuffer buffer(const_cast<QByteArray *>(&bytes));
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "GIF");
    const QImage frame = reader.read();
    QByteArray png;
    if (!frame.isNull()) {
        QBuffer pngBuffer(&png);
        pngBuffer.open(QIODevice::WriteOnly);
        frame.save(&pngBuffer, "PNG");
    }
    return png.isEmpty() ? bytes : png;
}
}

// 网络对象由 provider 所有，便于取消时统一释放。
OpenAiCompatibleProvider::OpenAiCompatibleProvider(QObject *parent)
    : QObject(parent)
{
}

// 校验请求参数，构造 OpenAI 兼容 JSON，并在 30 秒内异步返回结果。
void OpenAiCompatibleProvider::requestNickname(const QByteArray &rawBytes, const QString &format,
                                               const AiSettings &settings)
{
    cancel();
    // 先拒绝不可请求的 URL、模型、密钥和超限图片，避免发出无效网络请求。
    const QUrl endpoint(settings.endpoint.trimmed());
    if (!endpoint.isValid() || !settings.endpoint.contains(QLatin1String("/chat/completions"), Qt::CaseInsensitive)) {
        emit failed(AppStrings::endpointInvalidMessage());
        return;
    }
    if (settings.model.trimmed().isEmpty()) {
        emit failed(AppStrings::modelRequiredMessage());
        return;
    }
    if (settings.apiKey.trimmed().isEmpty()) {
        emit failed(AppStrings::apiKeyMissingMessage());
        return;
    }
    const QByteArray bytes = visionBytes(rawBytes, format);
    if (bytes.isEmpty() || bytes.size() > kMaxImageBytes) {
        emit failed(AppStrings::aiImageInvalidMessage());
        return;
    }
    const QString mimeType = bytes.startsWith(QByteArray("\x89PNG")) ? QStringLiteral("image/png")
                              : format.compare(QLatin1String("jpg"), Qt::CaseInsensitive) == 0 ? QStringLiteral("image/jpeg")
                              : QStringLiteral("image/") + format.toLower();
    // 使用 OpenAI Vision 的 text + image_url content 数组构造请求体。
    QJsonObject userContent{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"),
        QStringLiteral("给图片生成简短、自然、便于以后检索的中文表情包nickname。重点描述情绪、动作、反应或梗意，不要长篇客观描述画面内容。只返回一个nickname。")}};
    QJsonObject imageContent{{QStringLiteral("type"), QStringLiteral("image_url")}};
    imageContent.insert(QStringLiteral("image_url"), QJsonObject{
        {QStringLiteral("url"), QStringLiteral("data:%1;base64,").arg(mimeType) + QString::fromLatin1(bytes.toBase64())}});
    QJsonArray content{userContent, imageContent};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}};
    QJsonObject body{{QStringLiteral("model"), settings.model.trimmed()},
                     {QStringLiteral("messages"), QJsonArray{message}},
                     {QStringLiteral("max_tokens"), 32},
                     {QStringLiteral("temperature"), 0.4}};
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + settings.apiKey.toUtf8());
    m_network = new QNetworkAccessManager(this);
    m_reply = m_network->post(request, QJsonDocument(body).toJson());
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (m_reply) {
            m_reply->abort();
            emit failed(AppStrings::aiTimeoutMessage());
        }
    });
    connect(timer, &QTimer::timeout, timer, &QTimer::deleteLater);
    timer->start(settings.timeoutSeconds * 1000);
    // finished 回调统一处理 HTTP 错误、JSON 结果和 nickname 清洗。
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        auto *reply = qobject_cast<QNetworkReply *>(sender());
        reply->deleteLater();
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(AppStrings::aiHttpFailedMessage(
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            emit failed(AppStrings::aiNicknameEmptyMessage());
            return;
        }
        QString nickname = choices.at(0).toObject().value(QStringLiteral("message")).toObject()
                           .value(QStringLiteral("content")).toString().simplified();
        nickname.remove(QLatin1Char('\n'));
        if (nickname.isEmpty())
            emit failed(AppStrings::aiNicknameEmptyMessage());
        else
            emit succeeded(nickname);
    });
}

// 断开 finished 信号后中止并延迟销毁当前 reply 和 network manager。
void OpenAiCompatibleProvider::cancel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_network) {
        m_network->deleteLater();
        m_network = nullptr;
    }
}
