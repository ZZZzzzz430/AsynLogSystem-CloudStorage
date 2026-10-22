#include "LegacyTransfer.hpp"

#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <memory>

namespace {
constexpr qint64 kMaxSize = 1024 * 1024;

bool reject(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

bool validBase(const QUrl& base, QString* error) {
    if (!base.isValid() || (base.scheme() != "http" && base.scheme() != "https") ||
        base.host().isEmpty() || !base.userInfo().isEmpty() || base.hasQuery() ||
        base.hasFragment() || (!base.path().isEmpty() && base.path() != "/"))
        return reject(error, QStringLiteral("Use an http(s) origin, e.g. http://127.0.0.1:8081"));
    return true;
}

bool validName(const QString& name, QString* error) {
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/') ||
        name.contains('\\') || name.toUtf8().size() > 255)
        return reject(error, QStringLiteral("Invalid file name"));
    for (const QChar c : name) {
        if (c.unicode() < 32 || c.unicode() == 127)
            return reject(error, QStringLiteral("Control characters in file name"));
    }
    return true;
}

QNetworkRequest requestFor(const QUrl& base, const QString& path) {
    QUrl url(base);
    url.setPath(path); // Qt encodes Unicode, spaces, literal #, ? and %.
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    return request;
}

// Explicit timers distinguish idle/total timeouts from a user's abort().
// Reply owns both timers. Cancellation shares the single finished() path.
void addDeadline(QNetworkReply* reply) {
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply] {
        reply->setProperty("bridgeTimedOut", true);
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, timer, &QTimer::stop);
    timer->start(30000);

    auto* idle = new QTimer(reply);
    idle->setSingleShot(true);
    idle->setInterval(15000);
    QObject::connect(idle, &QTimer::timeout, reply, [reply] {
        reply->setProperty("bridgeIdleTimedOut", true);
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::readyRead, idle, [idle] { idle->start(); });
    QObject::connect(reply, &QNetworkReply::uploadProgress, idle,
                     [idle](qint64, qint64) { idle->start(); });
    QObject::connect(reply, &QNetworkReply::finished, idle, &QTimer::stop);
    idle->start();
}

QString failure(QNetworkReply* reply, int status) {
    if (reply->property("bridgeTimedOut").toBool())
        return QStringLiteral("Total deadline exceeded");
    if (reply->property("bridgeIdleTimedOut").toBool())
        return QStringLiteral("Inactivity deadline exceeded");
    if (reply->error() == QNetworkReply::OperationCanceledError)
        return QStringLiteral("Cancelled; refresh remote list to confirm upload outcome");
    // Qt also reports many HTTP 4xx/5xx responses through error().
    if (status != 0 && status != 200)
        return QStringLiteral("HTTP %1").arg(status);
    return reply->errorString();
}
} // namespace

LegacyTransfer::LegacyTransfer(QObject* parent) : QObject(parent) {}

QNetworkReply* LegacyTransfer::upload(const QUrl& base, const QString& localPath,
                                     QString* error) {
    if (error) error->clear();
    const QString name = QFileInfo(localPath).fileName();
    if (!validBase(base, error) || !validName(name, error)) return nullptr;

    auto file = std::make_unique<QFile>(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        reject(error, file->errorString());
        return nullptr;
    }
    if (file->size() <= 0 || file->size() > kMaxSize) {
        reject(error, QStringLiteral("Current service requires 1..1048576 bytes"));
        return nullptr;
    }
    auto request = requestFor(base, "/upload");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    request.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
    request.setRawHeader("FileName", name.toUtf8().toBase64());
    request.setRawHeader("StorageType", "low");
    auto* reply = manager_.post(request, file.get());
    // QNetworkAccessManager does not take ownership of the upload device.
    file->setParent(reply);
    file.release();
    addDeadline(reply);
    connect(reply, &QNetworkReply::uploadProgress, this, &LegacyTransfer::progress);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = status == 200 && reply->error() == QNetworkReply::NoError;
        const QString message = ok ? QStringLiteral("Server acknowledged upload; verify download bytes")
                                   : failure(reply, status);
        // Schedule cleanup before emitting: a receiver may destroy this adapter.
        reply->deleteLater();
        emit finished(ok, status, message);
    });
    return reply;
}

QNetworkReply* LegacyTransfer::download(const QUrl& base, const QString& remoteName,
                                       const QString& targetPath, QString* error) {
    if (error) error->clear();
    if (!validBase(base, error) || !validName(remoteName, error)) return nullptr;
    if (targetPath.isEmpty()) {
        reject(error, QStringLiteral("Choose a local destination"));
        return nullptr;
    }
    struct State {
        explicit State(const QString& path) : output(path) {}
        QSaveFile output;
        qint64 received = 0;
        QString localError;
    };
    auto state = std::make_shared<State>(targetPath);
    state->output.setDirectWriteFallback(false);
    if (!state->output.open(QIODevice::WriteOnly)) {
        reject(error, state->output.errorString());
        return nullptr;
    }
    auto request = requestFor(base, "/download/" + remoteName);
    request.setRawHeader("Accept-Encoding", "identity");
    auto* reply = manager_.get(request); // Do not send Range or If-Range.
    reply->setReadBufferSize(64 * 1024);
    addDeadline(reply);
    connect(reply, &QNetworkReply::downloadProgress, this, &LegacyTransfer::progress);
    auto drain = [reply, state] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            reply->readAll(); // Discard errors without filling the bounded read buffer.
            return;
        }
        if (!state->localError.isEmpty()) return;
        while (reply->bytesAvailable() > 0) {
            const QByteArray bytes = reply->read(64 * 1024);
            if (bytes.isEmpty()) break;
            if (state->received + bytes.size() > kMaxSize) {
                state->localError = QStringLiteral("Response exceeds reference size limit");
                reply->abort();
                return;
            }
            if (state->output.write(bytes) != bytes.size()) {
                state->localError = state->output.errorString();
                reply->abort();
                return;
            }
            state->received += bytes.size();
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this, [this, reply, state, drain] {
        drain(); // Consume any final buffered bytes before checking/committing.
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = status == 200 && reply->error() == QNetworkReply::NoError &&
                  state->localError.isEmpty();
        if (ok && reply->hasRawHeader("Content-Length")) {
            bool validLength = false;
            const qint64 expected = reply->rawHeader("Content-Length").toLongLong(&validLength);
            if (!validLength || expected < 0 || expected != state->received) {
                ok = false;
                state->localError = QStringLiteral("Response length mismatch");
            }
        }
        QString message;
        if (ok) {
            ok = state->output.commit();
            message = ok ? QStringLiteral("Download committed; compare source and destination hashes")
                         : state->output.errorString();
        } else {
            state->output.cancelWriting();
            message = state->localError.isEmpty() ? failure(reply, status) : state->localError;
        }
        reply->deleteLater();
        emit finished(ok, status, message);
    });
    return reply;
}
