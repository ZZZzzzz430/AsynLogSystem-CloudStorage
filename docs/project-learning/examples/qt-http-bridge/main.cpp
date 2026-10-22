#include "LegacyTransfer.hpp"
#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv) {
    // On Windows, default Qt logging may go only to the debugger when piped.
    // This console exercise always reports results to stderr as UTF-8.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        const QByteArray bytes = message.toUtf8();
        std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    });
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 4 ||
        (args[1] != "upload" && args[1] != "download") ||
        (args[1] == "upload" && args.size() != 4) ||
        (args[1] == "download" && args.size() != 5)) {
        qCritical().noquote() << "bridge-demo upload <origin> <local-file>\n"
                                "bridge-demo download <origin> <remote-name> <local-target>\n"
                                "Optional: BRIDGE_CANCEL_AFTER_MS=100 to exercise cancellation";
        return 2;
    }
    LegacyTransfer transfer;
    QObject::connect(&transfer, &LegacyTransfer::progress, &app,
                     [](qint64 done, qint64 total) { qInfo() << "bytes:" << done << '/' << total; });
    QObject::connect(&transfer, &LegacyTransfer::finished, &app,
                     [&app](bool ok, int status, const QString& message) {
        qInfo().noquote() << "HTTP" << status << message;
        app.exit(ok ? 0 : 1);
    });
    // Start after the event loop begins, so even a fast failure can exit it.
    QTimer::singleShot(0, &app, [&] {
        QString error;
        QPointer<QNetworkReply> reply = args[1] == "upload"
            ? transfer.upload(QUrl(args[2]), args[3], &error)
            : transfer.download(QUrl(args[2]), args[3], args[4], &error);
        if (!reply) {
            qCritical().noquote() << error;
            app.exit(2);
            return;
        }
        bool valid = false;
        const int delay = qEnvironmentVariableIntValue("BRIDGE_CANCEL_AFTER_MS", &valid);
        if (valid && delay >= 0)
            QTimer::singleShot(delay, &app, [reply] { if (reply) reply->abort(); });
    });
    return app.exec();
}
