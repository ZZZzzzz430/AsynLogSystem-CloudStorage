
#include "ApiClient.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QDebug>
#include <memory> // std::make_shared 的直接依赖，不依赖 Qt 头文件间接包含。

// TODO E01（用户练习）：按实际服务端认证契约补齐这里。
// 1. 创建真实请求；2. 管理 reply/超时/取消；3. 检查 HTTP 和业务身份结果；
// 4. 只在身份确认通过时 emit loginFinished(true, ...)，否则给出失败原因。
// 目前不拼接假 /login，不写死账号，也不使用定时器默认放行。
// TODO DB06：服务端的 MySQL 仓储和 /api/v1/auth/login 真正完成后再实现 HTTP。
// Qt 客户端只调用服务接口，不链接 MySQL、不持有 STORAGE_DB_* 数据库凭据。
void ApiClient::login(const QUrl &serverUrl, const QString &account, const QString &password)
{
    Q_UNUSED(serverUrl)
    Q_UNUSED(account)
    Q_UNUSED(password)
    emit loginFinished(false, tr("登录服务暂不可用，请联系管理员。"));
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
ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
{
    // 创建网络管理器
    // parent 为 this，ApiClient 销毁时会自动销毁 manager_
    manager_ = new QNetworkAccessManager(this);
}

// 测试服务器连接
void ApiClient::testConnection(const QUrl& serverUrl)
{
    // 1. 创建 HTTP 请求
    QNetworkRequest request(serverUrl);

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

