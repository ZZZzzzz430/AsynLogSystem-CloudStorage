#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QNetworkAccessManager>

class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject* parent = nullptr);

    void testConnection(const QUrl& serverUrl);

signals:
    void connectionFinished(
        bool ok,
        int httpStatus,
        const QString& message
    );

private:
    QNetworkAccessManager* manager_;
};
