#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QDateTime>
#include <QNetworkRequest>
#include <QNetworkAccessManager>

class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject* parent = nullptr);

    void testConnection(const QUrl& serverUrl);

    // Login and subsequent authenticated requests share one in-memory session.
    // 不记录密码、不保存凭据到成员、不把网络可达作为身份有效。
    void login(const QUrl &serverUrl, const QString &account, const QString &password);

    // TODO E04：实现上传请求，并保证 QFile 的寿命覆盖 reply 的异步读取。
    void uploadFile(const QUrl &serverUrl, const QString &localPath);

    // TODO E05：实现下载请求，通过 QSaveFile 在全部检查完成后提交目标。
    void downloadFile(const QUrl &serverUrl, const QString &remoteName, const QString &targetPath);

    void verifySession(const QUrl &serverUrl);
    void logout(const QUrl &serverUrl);

signals:
    void sessionVerified(bool valid, const QString &message);
    void logoutFinished(bool completed, const QString &message);
    // 认证结果与连接测试结果分属两个通道。
    void loginFinished(bool verified, const QString &message);

    // completed=true 必须代表最终确认完成；占位传输永远返回 false。
    void uploadFinished(bool completed, const QString &message);
    void downloadFinished(bool completed, const QString &message);

    // TODO E04/E05：转发真实请求的进度；进度达到 100% 不等于操作成功。
    void transferProgress(qint64 transferred, qint64 total);

    void connectionFinished(
        bool ok,
        int httpStatus,
        const QString& message
    );

private:
    QNetworkRequest request(const QUrl &serverUrl, const QString &path, bool authenticated) const;
    void clearSession();
    QNetworkAccessManager* manager_;
    QByteArray token_;
    QDateTime expiresAt_;
    QUrl sessionServer_;
    bool loginPending_ = false;
};
