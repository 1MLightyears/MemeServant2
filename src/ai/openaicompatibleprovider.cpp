// Endpoint必须是完整chat/completions URL；密钥只进入Authorization头，绝不写日志。
#include "ai/openaicompatibleprovider.h"

#include "core/appstrings.h"
#include "storage/logservice.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {
constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;
constexpr int kMaxCompletionTokens = 1024;
constexpr int kMaxNicknameExamples = 50;
constexpr auto kNicknameExamplesMarker = "{{NICKNAME_EXAMPLES}}";

// 只保留适合单行日志的服务端诊断信息，并兜底遮盖常见密钥格式。
QString safeLogText(QString text, int maxLength = 800)
{
    text.replace(QRegularExpression(QStringLiteral("Bearer\\s+\\S+"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("Bearer <redacted>"));
    text.replace(QRegularExpression(QStringLiteral("sk-[A-Za-z0-9_-]{8,}")),
                 QStringLiteral("<redacted-key>"));
    text = text.simplified();
    if (text.size() > maxLength)
        text = text.left(maxLength) + QStringLiteral("…");
    return text;
}

QString responseText(const QJsonValue &content)
{
    if (content.isString())
        return content.toString();
    if (!content.isArray())
        return {};

    QStringList parts;
    for (const QJsonValue &part : content.toArray()) {
        if (part.isString()) {
            parts.append(part.toString());
            continue;
        }
        const QJsonObject object = part.toObject();
        const QJsonValue text = object.value(QStringLiteral("text"));
        if (text.isString())
            parts.append(text.toString());
        else if (text.isObject() && text.toObject().value(QStringLiteral("value")).isString())
            parts.append(text.toObject().value(QStringLiteral("value")).toString());
    }
    return parts.join(QLatin1Char(' '));
}

QString jsonScalar(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble());
    return {};
}

QString serverErrorDetail(const QJsonObject &root)
{
    const QJsonObject error = root.value(QStringLiteral("error")).toObject();
    QStringList parts;
    const QString code = jsonScalar(error.value(QStringLiteral("code")));
    const QString message = error.value(QStringLiteral("message")).toString();
    const QString raw = error.value(QStringLiteral("metadata")).toObject()
                            .value(QStringLiteral("raw")).toString();
    if (!code.isEmpty())
        parts.append(QStringLiteral("code=%1").arg(code));
    if (!message.isEmpty())
        parts.append(QStringLiteral("message=%1").arg(message));
    if (!raw.isEmpty() && raw != message)
        parts.append(QStringLiteral("raw=%1").arg(raw));
    return safeLogText(parts.join(QStringLiteral("; ")));
}

// 每次请求重新洗牌候选集合，只取配置数量且不修改控制器持有的原列表。
QStringList randomNicknameExamples(QStringList candidates, int requestedCount)
{
    const int count = qBound(0, requestedCount, qMin(candidates.size(), kMaxNicknameExamples));
    for (int index = 0; index < count; ++index) {
        const int selected = index + QRandomGenerator::global()->bounded(candidates.size() - index);
        candidates.swapItemsAt(index, selected);
    }
    return candidates.mid(0, count);
}

// 从嵌入资源读取基础提示词，并将随机 nickname 示例填入唯一占位符。
QString nicknamePrompt(const QStringList &examples)
{
    QFile file(QStringLiteral(":/prompts/vision_nickname.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QString prompt = QString::fromUtf8(file.readAll());
    if (!prompt.contains(QLatin1String(kNicknameExamplesMarker)))
        return {};

    QStringList exampleLines;
    for (QString example : examples) {
        example = example.simplified().left(80);
        if (!example.isEmpty())
            exampleLines.append(QStringLiteral("- %1").arg(example));
    }
    const QString section = exampleLines.isEmpty()
                                ? QStringLiteral("（本次未提供nickname示例）")
                                : exampleLines.join(QLatin1Char('\n'));
    return prompt.replace(QLatin1String(kNicknameExamplesMarker), section).trimmed();
}

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
    const QString requestTag = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    emit stageChanged(RequestStage::Preparing);
    // 先拒绝不可请求的 URL、模型、密钥和超限图片，避免发出无效网络请求。
    const QUrl endpoint(settings.endpoint.trimmed());
    if (!endpoint.isValid() || !settings.endpoint.contains(QLatin1String("/chat/completions"), Qt::CaseInsensitive)) {
        LogService::instance().warning(QStringLiteral("AI[%1] Endpoint校验失败").arg(requestTag));
        emit failed(AppStrings::endpointInvalidMessage());
        return;
    }
    if (settings.model.trimmed().isEmpty()) {
        LogService::instance().warning(QStringLiteral("AI[%1] Model为空").arg(requestTag));
        emit failed(AppStrings::modelRequiredMessage());
        return;
    }
    if (settings.apiKey.trimmed().isEmpty()) {
        LogService::instance().warning(QStringLiteral("AI[%1] API Key为空 source=%2")
                                       .arg(requestTag, settings.apiKeySource));
        emit failed(AppStrings::apiKeyMissingMessage());
        return;
    }
    const QByteArray bytes = visionBytes(rawBytes, format);
    if (bytes.isEmpty() || bytes.size() > kMaxImageBytes) {
        LogService::instance().warning(QStringLiteral("AI[%1] 图片校验失败 bytes=%2 format=%3")
                                       .arg(requestTag).arg(bytes.size()).arg(format));
        emit failed(AppStrings::aiImageInvalidMessage());
        return;
    }
    const QStringList nicknameExamples = randomNicknameExamples(
        settings.nicknameCandidates, settings.nicknameExampleCount);
    const QString prompt = nicknamePrompt(nicknameExamples);
    if (prompt.isEmpty()) {
        LogService::instance().error(QStringLiteral("AI[%1] 提示词资源读取或渲染失败").arg(requestTag));
        emit failed(AppStrings::aiPromptUnavailableMessage());
        return;
    }
    const QString mimeType = bytes.startsWith(QByteArray("\x89PNG")) ? QStringLiteral("image/png")
                              : format.compare(QLatin1String("jpg"), Qt::CaseInsensitive) == 0 ? QStringLiteral("image/jpeg")
                              : QStringLiteral("image/") + format.toLower();
    // 使用 OpenAI Vision 的 text + image_url content 数组构造请求体。
    QJsonObject userContent{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("text"), prompt}};
    QJsonObject imageContent{{QStringLiteral("type"), QStringLiteral("image_url")}};
    imageContent.insert(QStringLiteral("image_url"), QJsonObject{
        {QStringLiteral("url"), QStringLiteral("data:%1;base64,").arg(mimeType) + QString::fromLatin1(bytes.toBase64())}});
    QJsonArray content{userContent, imageContent};
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}};
    QJsonObject body{{QStringLiteral("model"), settings.model.trimmed()},
                     {QStringLiteral("messages"), QJsonArray{message}},
                     {QStringLiteral("max_tokens"), kMaxCompletionTokens},
                     {QStringLiteral("temperature"), 0.4}};
    // OpenRouter 的推理 token 计入输出上限；昵称任务使用最小推理，为最终文本留足额度。
    if (endpoint.host().compare(QLatin1String("openrouter.ai"), Qt::CaseInsensitive) == 0) {
        body.insert(QStringLiteral("reasoning"), QJsonObject{
            {QStringLiteral("effort"), QStringLiteral("minimal")},
            {QStringLiteral("exclude"), true}});
    }
    const QString normalizedKey = settings.apiKey.trimmed();
    LogService::instance().info(QStringLiteral(
        "AI[%1] 请求开始 endpoint=%2://%3%4 model=%5 format=%6 rawBytes=%7 requestBytes=%8 "
        "maxTokens=%9 nicknameCandidates=%10 nicknameExamples=%11 keySource=%12 keyTrimmed=%13")
        .arg(requestTag, endpoint.scheme(), endpoint.host(), endpoint.path(), settings.model.trimmed(),
             format, QString::number(rawBytes.size()), QString::number(bytes.size()),
             QString::number(kMaxCompletionTokens), QString::number(settings.nicknameCandidates.size()),
             QString::number(nicknameExamples.size()), settings.apiKeySource,
             settings.apiKey == normalizedKey ? QStringLiteral("false") : QStringLiteral("true")));
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + normalizedKey.toUtf8());
    m_network = new QNetworkAccessManager(this);
    emit stageChanged(RequestStage::Sending);
    LogService::instance().info(QStringLiteral("AI[%1] 正在发送请求").arg(requestTag));
    m_reply = m_network->post(request, QJsonDocument(body).toJson());
    const auto recognizingReported = QSharedPointer<bool>::create(false);
    connect(m_reply, &QNetworkReply::uploadProgress, this,
            [this, requestTag, recognizingReported](qint64 sent, qint64 total) {
        if (!*recognizingReported && total > 0 && sent >= total) {
            *recognizingReported = true;
            emit stageChanged(RequestStage::Recognizing);
            LogService::instance().info(QStringLiteral("AI[%1] 请求发送完成，等待识图结果 bytes=%2")
                                        .arg(requestTag).arg(total));
        }
    });
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    m_timedOut = false;
    connect(timer, &QTimer::timeout, this, [this, requestTag, startedAt]() {
        if (m_reply) {
            m_timedOut = true;
            LogService::instance().error(QStringLiteral("AI[%1] 请求超时 elapsedMs=%2")
                                         .arg(requestTag).arg(QDateTime::currentMSecsSinceEpoch() - startedAt));
            m_reply->abort();
            emit failed(AppStrings::aiTimeoutMessage());
        }
    });
    connect(timer, &QTimer::timeout, timer, &QTimer::deleteLater);
    timer->start(settings.timeoutSeconds * 1000);
    // finished 回调统一处理 HTTP 错误、JSON 结果和 nickname 清洗。
    connect(m_reply, &QNetworkReply::finished, this,
            [this, timer, requestTag, startedAt, recognizingReported]() {
        auto *reply = qobject_cast<QNetworkReply *>(sender());
        timer->stop();
        timer->deleteLater();
        reply->deleteLater();
        m_reply = nullptr;
        const QByteArray responseBytes = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startedAt;
        if (m_timedOut)
            return;

        emit stageChanged(RequestStage::Processing);
        QJsonParseError parseError;
        const QJsonDocument response = QJsonDocument::fromJson(responseBytes, &parseError);
        const QJsonObject object = response.object();
        QString remoteRequestId = QString::fromLatin1(reply->rawHeader("x-request-id"));
        if (remoteRequestId.isEmpty())
            remoteRequestId = QString::fromLatin1(reply->rawHeader("x-openrouter-request-id"));
        LogService::instance().info(QStringLiteral(
            "AI[%1] 收到响应 http=%2 networkError=%3 elapsedMs=%4 responseBytes=%5 "
            "remoteRequestId=%6 jsonError=%7")
            .arg(requestTag).arg(status).arg(int(reply->error())).arg(elapsed).arg(responseBytes.size())
            .arg(safeLogText(remoteRequestId, 128), safeLogText(parseError.errorString(), 160)));
        if (reply->error() != QNetworkReply::NoError) {
            const QString detail = serverErrorDetail(object);
            LogService::instance().error(QStringLiteral("AI[%1] 请求失败 qt=%2 server=%3")
                                         .arg(requestTag, safeLogText(reply->errorString()), detail));
            emit failed(AppStrings::aiHttpFailedMessage(status));
            return;
        }
        if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
            LogService::instance().error(QStringLiteral("AI[%1] 响应不是有效JSON对象").arg(requestTag));
            emit failed(AppStrings::aiNicknameEmptyMessage());
            return;
        }
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            LogService::instance().error(QStringLiteral("AI[%1] 响应choices为空 server=%2")
                                         .arg(requestTag, serverErrorDetail(object)));
            emit failed(AppStrings::aiNicknameEmptyMessage());
            return;
        }
        const QJsonObject choice = choices.at(0).toObject();
        const QJsonObject responseMessage = choice.value(QStringLiteral("message")).toObject();
        const QJsonValue contentValue = responseMessage.value(QStringLiteral("content"));
        QString nickname = responseText(contentValue).simplified();
        if (nickname.isEmpty())
            nickname = responseText(choice.value(QStringLiteral("text"))).simplified();
        nickname.remove(QLatin1Char('\n'));
        const QString reasoning = responseMessage.value(QStringLiteral("reasoning")).toString();
        const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
        const int reasoningTokens = usage.value(QStringLiteral("completion_tokens_details")).toObject()
                                        .value(QStringLiteral("reasoning_tokens")).toInt(-1);
        LogService::instance().info(QStringLiteral(
            "AI[%1] 响应解析 finishReason=%2 contentType=%3 contentChars=%4 reasoningChars=%5 "
            "promptTokens=%6 completionTokens=%7 reasoningTokens=%8")
            .arg(requestTag, safeLogText(choice.value(QStringLiteral("finish_reason")).toString(), 80),
                 contentValue.isString() ? QStringLiteral("string")
                                         : contentValue.isArray() ? QStringLiteral("array")
                                                                  : QStringLiteral("other"))
            .arg(nickname.size()).arg(reasoning.size())
            .arg(usage.value(QStringLiteral("prompt_tokens")).toInt(-1))
            .arg(usage.value(QStringLiteral("completion_tokens")).toInt(-1))
            .arg(reasoningTokens));
        if (nickname.isEmpty()) {
            LogService::instance().error(QStringLiteral("AI[%1] 未返回可用nickname").arg(requestTag));
            emit failed(AppStrings::aiNicknameEmptyMessage());
        } else {
            emit succeeded(nickname);
        }
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
