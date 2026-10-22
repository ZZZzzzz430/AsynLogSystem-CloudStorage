#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

// Reference adapter for the CURRENT anonymous service. Not the proposed v1 API.
// Keep this object in its creating thread. The caller owns task scheduling.
class LegacyTransfer final : public QObject {
    Q_OBJECT
public:
    explicit LegacyTransfer(QObject* parent = nullptr);

    // Returns nullptr on local validation failure and fills error.
    // On success, reply belongs to this object and is deleted after finished.
    // Keep only a QPointer<QNetworkReply> if the UI offers cancellation.
    QNetworkReply* upload(const QUrl& base, const QString& localPath,
                         QString* error = nullptr);
    QNetworkReply* download(const QUrl& base, const QString& remoteName,
                           const QString& targetPath, QString* error = nullptr);

signals:
    void progress(qint64 done, qint64 total);
    void finished(bool ok, int httpStatus, const QString& message);

private:
    QNetworkAccessManager manager_;
};
