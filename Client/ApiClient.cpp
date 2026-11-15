
#include "ApiClient.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory> // std::make_shared 的直接依赖，不依赖 Qt 头文件间接包含。

namespace {
void timeoutReply(QNetworkReply *reply, int milliseconds = 10000) {
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply] {
        reply->setProperty("requestTimedOut", true);
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, timer, &QTimer::stop);
    timer->start(milliseconds);
}
bool validServer(const QUrl &url) {
    return url.isValid() && (url.scheme() == "http" || url.scheme() == "https")
        && !url.host().isEmpty() && url.userInfo().isEmpty()
        && !url.hasQuery() && !url.hasFragment()
        && (url.path().isEmpty() || url.path() == "/");
}
QString authenticationFailure(QNetworkReply *reply, int status, bool registration) {
    if (reply->property("requestTimedOut").toBool()
        || reply->error() == QNetworkReply::TimeoutError)
        return registration
            ? QObject::tr("注册请求超时，结果尚未确认。可重试；若提示账号已存在，请尝试登录。")
            : QObject::tr("登录请求超时，请重试。");
    // HTTP 4xx/5xx 也会令 QNetworkReply::error() 非零，先解释明确的 HTTP 状态。
    if (status == 400 || status == 422)
        return QObject::tr("输入不符合要求，请检查用户名和密码。");
    if (status == 413) return QObject::tr("提交内容过长，请缩短输入后重试。");
    if (status == 409) return QObject::tr("该账号已存在，请更换用户名或返回登录。");
    if (status == 429) {
        bool valid = false;
        const int seconds = reply->rawHeader("Retry-After").toInt(&valid);
        return valid && seconds > 0
            ? QObject::tr("请求过多，请在 %1 秒后重试。").arg(seconds)
            : QObject::tr("请求过多，请稍后重试。");
    }
    if (status >= 500) return QObject::tr("服务暂不可用，请稍后重试。");
    if (status == 401 && !registration) return QObject::tr("账号或密码错误。");
    if (reply->error() != QNetworkReply::NoError)
        return registration
            ? QObject::tr("网络连接失败，注册结果尚未确认。请检查网络后重试；若账号已存在，请尝试登录。")
            : QObject::tr("网络连接失败，请检查网络和服务器地址后重试。");
    return QObject::tr("服务器响应异常，无法确认结果，请重试。");
}
bool sameOrigin(const QUrl &a, const QUrl &b) {
    return a.scheme() == b.scheme() && a.host() == b.host()
        && a.port(a.scheme() == "https" ? 443 : 80) == b.port(b.scheme() == "https" ? 443 : 80);
}
}
void ApiClient::clearSession() {
    token_.clear(); expiresAt_ = {}; sessionServer_ = QUrl();
}
QNetworkRequest ApiClient::request(const QUrl &serverUrl, const QString &path, bool authenticated) const {
    QUrl url = serverUrl;
    url.setPath(path); url.setQuery(QString()); url.setFragment(QString());
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (authenticated && sameOrigin(serverUrl, sessionServer_) && expiresAt_ > QDateTime::currentDateTimeUtc())
        req.setRawHeader("Authorization", "Bearer " + token_);
    return req;
}
void ApiClient::login(const QUrl &serverUrl, const QString &account, const QString &password) {
    if (authenticationReply_) return;
    clearSession();
    if (!validServer(serverUrl)) {
        emit loginFinished(false, tr("服务器地址无效。")); return;
    }
    QJsonObject body; body["account"] = account; body["password"] = password;
    auto *reply = manager_->post(request(serverUrl, "/api/v1/auth/login", false), QJsonDocument(body).toJson(QJsonDocument::Compact));
    authenticationReply_ = reply;
    timeoutReply(reply, authenticationTimeoutMs_);
    connect(reply, &QNetworkReply::finished, this, [this, reply, serverUrl] {
        authenticationReply_.clear();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(reply->isReadable() ? reply->readAll() : QByteArray(), &error);
        const auto body = doc.object();
        QByteArray token = body["token"].toString().toLatin1();
        auto expires = QDateTime::fromString(body["expiresAt"].toString(), Qt::ISODate);
        bool tokenValid = token.size() == 64;
        for (char c : token) tokenValid = tokenValid && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        bool ok = reply->error() == QNetworkReply::NoError && status == 200
            && error.error == QJsonParseError::NoError && tokenValid
            && expires.isValid() && expires > QDateTime::currentDateTimeUtc()
            && !body["user"].toObject()["id"].toString().isEmpty()
            && !body["user"].toObject()["username"].toString().isEmpty();
        if (ok) { token_ = token; expiresAt_ = expires; sessionServer_ = serverUrl; }
        else clearSession();
        const QString message = ok ? tr("登录成功。") : authenticationFailure(reply, status, false);
        reply->deleteLater();
        emit loginFinished(ok, message);
    });
}
void ApiClient::registerUser(const QUrl &serverUrl, const QString &username, const QString &password) {
    if (authenticationReply_) return;
    if (!validServer(serverUrl)) {
        emit registrationFinished(false, tr("服务器地址无效，请返回登录页修改。"));
        return;
    }
    const QJsonObject body{{"username", username}, {"password", password}};
    auto *reply = manager_->post(request(serverUrl, "/api/v1/auth/register", false),
                                QJsonDocument(body).toJson(QJsonDocument::Compact));
    authenticationReply_ = reply;
    timeoutReply(reply, authenticationTimeoutMs_);
    connect(reply, &QNetworkReply::finished, this, [this, reply, username] {
        authenticationReply_.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(reply->isReadable() ? reply->readAll() : QByteArray(), &error);
        const auto user = doc.object()["user"].toObject();
        const bool created = reply->error() == QNetworkReply::NoError && status == 201
            && error.error == QJsonParseError::NoError && doc.isObject()
            && !user["id"].toString().isEmpty() && user["username"].toString() == username;
        const QString message = created ? tr("注册成功，请登录。")
                                        : authenticationFailure(reply, status, true);
        reply->deleteLater();
        emit registrationFinished(created, message);
    });
}
void ApiClient::cancelAuthentication() {
    if (!authenticationReply_) return;
    auto *reply = authenticationReply_.data();
    authenticationReply_.clear();
    // abort 会触发 finished：先断开结果回调，避免关闭窗口后仍进行页面切换。
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}
void ApiClient::verifySession(const QUrl &serverUrl) {
    auto *reply = manager_->get(request(serverUrl, "/api/v1/auth/me", true));
    timeoutReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto body = QJsonDocument::fromJson(reply->readAll()).object();
        bool ok = reply->error() == QNetworkReply::NoError && status == 200
            && !body["user"].toObject()["id"].toString().isEmpty();
        if (status == 401) clearSession();
        reply->deleteLater();
        emit sessionVerified(ok, ok ? tr("会话验证成功。") : tr("会话验证失败，请重新登录。"));
    });
}
void ApiClient::logout(const QUrl &serverUrl) {
    auto *reply = manager_->post(request(serverUrl, "/api/v1/auth/logout", true), QByteArray());
    timeoutReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = reply->error() == QNetworkReply::NoError && status == 200;
        // If the network failed, retain the token so logout can be retried.
        if (ok || status == 401) clearSession();
        reply->deleteLater();
        emit logoutFinished(ok || status == 401, ok ? tr("已注销。") : tr("注销未确认，请重试。"));
    });
}

// TODO E04（用户练习）：实现旧协议 low 上传；FileName 使用文件名 UTF-8 的 Base64，
// 将 QFile 保留到 finished，等待服务端最终确认后再发送成功信号。
void ApiClient::uploadFile(const QUrl &serverUrl, const QString &localPath)
{
    Q_UNUSED(serverUrl)
    Q_UNUSED(localPath)
    emit uploadFinished(false, tr("上传服务暂不可用。"));
}

// TODO E05（用户练习）：按已知远端文件名编码 URL，流式写入临时输出；
// 检查状态、写入错误和完整性，全部成功才 commit，失败不留下正式目标文件。
void ApiClient::downloadFile(const QUrl &serverUrl, const QString &remoteName, const QString &targetPath)
{
    Q_UNUSED(serverUrl)
    Q_UNUSED(remoteName)
    Q_UNUSED(targetPath)
    emit downloadFinished(false, tr("下载服务暂不可用。"));
}

// 构造函数
ApiClient::ApiClient(QObject* parent, int authenticationTimeoutMs)
    : QObject(parent)
    , authenticationTimeoutMs_(qMax(1, authenticationTimeoutMs))
{
    // 创建网络管理器
    // parent 为 this，ApiClient 销毁时会自动销毁 manager_
    manager_ = new QNetworkAccessManager(this);
}
ApiClient::~ApiClient() {
    cancelAuthentication();
}

// 测试服务器连接
void ApiClient::testConnection(const QUrl& serverUrl)
{
    // 1. 创建 HTTP 请求
    QNetworkRequest request = this->request(serverUrl, "/health", false);

    // 2. 发送 GET 请求（异步执行）
    QNetworkReply* reply = manager_->get(request);

    qDebug() << "Sending GET request to:" << serverUrl;

    // 3. 设置超时标记
    // shared_ptr 用于让两个 Lambda 共享同一个标记
    auto timedOut = std::make_shared<bool>(false);

    // 4. 创建单次定时器
    QTimer* timer = new QTimer(reply);
    timer->setSingleShot(true);

    // 5. 超过 5 秒后取消请求
    connect(timer, &QTimer::timeout,
            reply, [reply, timedOut]() {
        *timedOut = true;
        reply->abort();
    });

    timer->start(5000);

    // 6. 请求完成时处理结果
    connect(reply, &QNetworkReply::finished,
            this, [this, reply, timer, timedOut]() {

        timer->stop();

        // 获取 HTTP 状态码
        int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute
        ).toInt();

        bool ok = false;
        QString message;

        // 优先判断超时
        if (*timedOut) {
            message = "请求超时（5秒）";
        }
        // 判断网络错误
        else if (reply->error() != QNetworkReply::NoError) {
            message = reply->errorString();
        }
        // 判断 HTTP 状态码
        else if (status >= 200 && status < 300) {
            ok = true;
            message = "服务器连接成功";
        }
        else {
            message = QString("HTTP 错误：%1").arg(status);
        }

        qDebug() << "Status:" << status
                 << "Success:" << ok
                 << "Message:" << message;

        // 7. 将结果通知 MainWindow
        emit connectionFinished(ok, status, message);

        // 8. 延迟释放 reply
        reply->deleteLater();
    });
}

