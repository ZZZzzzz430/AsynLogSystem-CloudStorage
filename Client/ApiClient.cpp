
#include "ApiClient.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QDebug>
#include <memory> // std::make_shared 的直接依赖，不依赖 Qt 头文件间接包含。

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

