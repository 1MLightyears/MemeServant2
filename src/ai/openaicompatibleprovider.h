// 声明基于 OpenAI Chat Completions 协议的图片识别提供器。
#ifndef AI_OPENAICOMPATIBLEPROVIDER_H
#define AI_OPENAICOMPATIBLEPROVIDER_H

// 实现首版唯一的 OpenAI Compatible Vision Provider。
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

struct AiSettings
{
    QString endpoint;
    QString model;
    QString apiKey;
    QString apiKeySource = QStringLiteral("none");
    QStringList nicknameCandidates;
    int nicknameExampleCount = 0;
    int timeoutSeconds = 30;
};

class OpenAiCompatibleProvider : public QObject
{
    Q_OBJECT
public:
    enum class RequestStage {
        Preparing,
        Sending,
        Recognizing,
        Processing
    };
    Q_ENUM(RequestStage)

    explicit OpenAiCompatibleProvider(QObject *parent = nullptr);
    /// 校验 AI 配置、编码图片并异步请求一个可检索的 nickname。
    void requestNickname(const QByteArray &imageBytes, const QString &imageFormat, const AiSettings &settings);
    /// 取消当前网络请求并释放请求对象。
    void cancel();

signals:
    /// 报告当前请求阶段，供捕获浮窗显示就地状态。
    void stageChanged(RequestStage stage);
    void succeeded(const QString &nickname);
    void failed(const QString &reason);

private:
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_timedOut = false;
};

#endif
