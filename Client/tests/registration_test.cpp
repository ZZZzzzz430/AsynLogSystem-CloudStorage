#include "ApiClient.hpp"
#include "loginwindow.h"
#include "registerdialog.h"
#include "mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <memory>

// Real asynchronous Qt networking against a local deterministic HTTP peer, no MySQL.
class HttpPeer : public QTcpServer {
public:
    struct Response { int status = 201; QByteArray body; int delayMs = 0; bool drop = false; };
    struct Request { QByteArray line, headers; QJsonObject body; };
    QList<Response> responses;
    QList<Request> requests;

    HttpPeer() {
        if (!listen(QHostAddress::LocalHost, 0)) qFatal("Cannot listen on loopback");
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    auto bytes = socket->property("received").toByteArray() + socket->readAll();
                    socket->setProperty("received", bytes);
                    if (socket->property("handled").toBool()) return;
                    const auto split = bytes.indexOf("\r\n\r\n");
                    if (split < 0) return;
                    const auto headers = bytes.left(split);
                    int length = 0;
                    for (const auto &line : headers.split('\n'))
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                    if (bytes.size() < split + 4 + length) return;
                    socket->setProperty("handled", true);
                    requests.append({headers.split('\n').first().trimmed(), headers,
                                     QJsonDocument::fromJson(bytes.mid(split + 4, length)).object()});
                    const auto response = responses.isEmpty()
                        ? Response{503, "{}"} : responses.takeFirst();
                    if (response.delayMs < 0) return; // Deliberately never finishes.
                    QTimer::singleShot(response.delayMs, socket, [socket, response] {
                        if (response.drop) { socket->abort(); return; }
                        socket->write("HTTP/1.1 " + QByteArray::number(response.status) + " Result\r\n"
                                      "Content-Type: application/json\r\nConnection: close\r\n"
                                      "Retry-After: 3\r\nContent-Length: " + QByteArray::number(response.body.size())
                                      + "\r\n\r\n" + response.body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
    }
    QUrl url() const { return QUrl(QString("http://127.0.0.1:%1").arg(serverPort())); }
    static QByteArray registered() {
        return R"({"user":{"id":"u_0123456789abcdef0123456789abcdef","username":"demo_user"}})";
    }
    static QByteArray authenticated() {
        return QJsonDocument(QJsonObject{
            {"user", QJsonObject{{"id", "u_0123456789abcdef0123456789abcdef"}, {"username", "demo_user"}}},
            {"token", QString(64, 'a')},
            {"expiresAt", QDateTime::currentDateTimeUtc().addSecs(3600).toString(Qt::ISODate)}
        }).toJson(QJsonDocument::Compact);
    }
};

class RegistrationTests : public QObject {
    Q_OBJECT
    template<class T> static T *widget(QObject &parent, const char *name) {
        auto *result = parent.findChild<T *>(QString::fromLatin1(name));
        if (!result) qFatal("Missing widget %s", name);
        return result;
    }
    static void fill(RegisterDialog &dialog, const QString &confirmation = "test-password") {
        widget<QLineEdit>(dialog, "registerUsernameEdit")->setText("demo_user");
        widget<QLineEdit>(dialog, "registerPasswordEdit")->setText("test-password");
        widget<QLineEdit>(dialog, "registerConfirmationEdit")->setText(confirmation);
    }
private slots:
    void initTestCase() {
        QApplication::setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
        // Windows offscreen 平台不自动枚举系统字体；仅为测试截图加载中文字体。
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
#endif
    }

    void registrationWireContractAndDuplicateGuard() {
        HttpPeer peer;
        peer.responses = {{201, HttpPeer::registered(), 40}, {401, "{}"}};
        ApiClient client;
        QSignalSpy result(&client, &ApiClient::registrationFinished);
        client.registerUser(peer.url(), "demo_user", "test-password");
        client.registerUser(peer.url(), "demo_user", "test-password");
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(result[0][0].toBool());
        QCOMPARE(peer.requests.size(), 1);
        QCOMPARE(peer.requests[0].line, QByteArray("POST /api/v1/auth/register HTTP/1.1"));
        QCOMPARE(peer.requests[0].body.size(), 2);
        QCOMPARE(peer.requests[0].body["username"].toString(), QString("demo_user"));
        QCOMPARE(peer.requests[0].body["password"].toString(), QString("test-password"));
        QVERIFY(!peer.requests[0].headers.toLower().contains("authorization:"));
        QSignalSpy verified(&client, &ApiClient::sessionVerified);
        client.verifySession(peer.url());
        QTRY_COMPARE(verified.count(), 1);
        QVERIFY(!peer.requests[1].headers.toLower().contains("authorization:"));
    }

    void errorMessages_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("expected");
        QTest::newRow("input") << 400 << QByteArray("{}") << QString("输入");
        QTest::newRow("duplicate") << 409 << QByteArray("{}") << QString("账号已存在");
        QTest::newRow("throttle") << 429 << QByteArray("{}") << QString("3 秒");
        QTest::newRow("unavailable") << 503 << QByteArray("{}") << QString("服务暂不可用");
        QTest::newRow("oversized") << 413 << QByteArray("{}") << QString("过长");
        QTest::newRow("invalid-json") << 201 << QByteArray("<html>broken</html>") << QString("响应异常");
        QTest::newRow("wrong-user") << 201 << QByteArray(R"({"user":{"id":"u_x","username":"another"}})") << QString("响应异常");
        QTest::newRow("redirect") << 302 << QByteArray("{}") << QString("响应异常");
    }
    void errorMessages() {
        QFETCH(int, status); QFETCH(QByteArray, body); QFETCH(QString, expected);
        HttpPeer peer;
        peer.responses = {{status, body}};
        RegisterDialog dialog(peer.url());
        fill(dialog);
        auto *button = widget<QPushButton>(dialog, "registerSubmitButton");
        button->click();
        QVERIFY(!button->isEnabled());
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(widget<QLabel>(dialog, "registerStatusLabel")->text().contains(expected));
        QCOMPARE(widget<QLineEdit>(dialog, "registerPasswordEdit")->text(), QString("test-password"));
        QVERIFY(widget<QLineEdit>(dialog, "registerUsernameEdit")->isEnabled());
        QVERIFY(widget<QLineEdit>(dialog, "registerConfirmationEdit")->isEnabled());
    }

    void clientValidationMakesNoRequests() {
        HttpPeer peer;
        RegisterDialog dialog(peer.url());
        fill(dialog, "different-password");
        auto *status = widget<QLabel>(dialog, "registerStatusLabel");
        QVERIFY(status->text().contains("不一致")); // Immediate feedback, before submit.
        widget<QPushButton>(dialog, "registerSubmitButton")->click();
        QTest::qWait(20);
        QCOMPARE(peer.requests.size(), 0);
        fill(dialog);
        widget<QLineEdit>(dialog, "registerUsernameEdit")->setText("bad name");
        widget<QPushButton>(dialog, "registerSubmitButton")->click();
        QVERIFY(status->text().contains("用户名"));
        fill(dialog);
        widget<QLineEdit>(dialog, "registerPasswordEdit")->setText("1234567");
        widget<QPushButton>(dialog, "registerSubmitButton")->click();
        QVERIFY(status->text().contains("8～1024"));
        QTest::qWait(20);
        QCOMPARE(peer.requests.size(), 0);
    }

    void timeoutRestoresControlsAndCanRetry() {
        HttpPeer peer;
        peer.responses = {{201, {}, -1}, {201, HttpPeer::registered()}};
        RegisterDialog dialog(peer.url(), nullptr, 120);
        dialog.show();
        fill(dialog);
        auto *button = widget<QPushButton>(dialog, "registerSubmitButton");
        QSignalSpy created(&dialog, &RegisterDialog::registrationSucceeded);
        bool heartbeat = false;
        button->click();
        button->click();
        QTimer::singleShot(10, &dialog, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QVERIFY(widget<QPushButton>(dialog, "registerBackButton")->isEnabled());
        QTRY_VERIFY(button->isEnabled());
        QCOMPARE(peer.requests.size(), 1);
        QVERIFY(widget<QLabel>(dialog, "registerStatusLabel")->text().contains("超时"));
        button->click();
        QTRY_COMPARE(created.count(), 1);
        QCOMPARE(peer.requests.size(), 2);
        QVERIFY(widget<QLineEdit>(dialog, "registerPasswordEdit")->text().isEmpty());
        QVERIFY(widget<QLineEdit>(dialog, "registerConfirmationEdit")->text().isEmpty());
    }

    void networkFailureCanRetry() {
        HttpPeer peer;
        const auto address = peer.url();
        const auto port = peer.serverPort();
        peer.close();
        RegisterDialog dialog(address);
        fill(dialog);
        auto *button = widget<QPushButton>(dialog, "registerSubmitButton");
        button->click();
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(widget<QLabel>(dialog, "registerStatusLabel")->text().contains("网络连接失败"));
        QVERIFY(peer.listen(QHostAddress::LocalHost, port));
        peer.responses = {{201, HttpPeer::registered()}};
        QSignalSpy created(&dialog, &RegisterDialog::registrationSucceeded);
        button->click();
        QTRY_COMPARE(created.count(), 1);
    }

    void registrationReturnsToLoginThenAuthenticates() {
        HttpPeer peer;
        peer.responses = {{201, HttpPeer::registered()}, {200, HttpPeer::authenticated()},
                          {200, HttpPeer::registered()}}; // MainWindow's session verification.
        LoginWindow login;
        login.show();
        widget<QComboBox>(login, "serverCombo")->setCurrentText(peer.url().toString());
        widget<QLineEdit>(login, "passwordEdit")->setText("old-login-password");
        QSignalSpy authenticated(&login, &LoginWindow::authenticationSucceeded);
        std::unique_ptr<MainWindow> main;
        connect(&login, &LoginWindow::authenticationSucceeded, &login, [&](const QUrl &url) {
            main = std::make_unique<MainWindow>(login.apiClient());
            main->setServerUrl(url); main->show(); login.hide();
        });
        widget<QPushButton>(login, "registerButton")->click();
        QPointer<RegisterDialog> dialog = login.findChild<RegisterDialog *>();
        QVERIFY(dialog);
        fill(*dialog);
        widget<QPushButton>(*dialog, "registerSubmitButton")->click();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(authenticated.count(), 0);
        QVERIFY(!main);
        QVERIFY(login.isVisible());
        QCOMPARE(widget<QLineEdit>(login, "accountEdit")->text(), QString("demo_user"));
        QVERIFY(widget<QLineEdit>(login, "passwordEdit")->text().isEmpty());
        widget<QLineEdit>(login, "passwordEdit")->setText("test-password");
        widget<QPushButton>(login, "loginButton")->click();
        QTRY_COMPARE(authenticated.count(), 1);
        QVERIFY(main && main->isVisible());
        QVERIFY(!login.isVisible());
    }

    void healthAndMalformedLoginNeverAuthenticate() {
        HttpPeer peer;
        peer.responses = {{200, "{\"ok\":true}"}, {200, "{}"}, {401, "{}"}};
        LoginWindow login;
        login.show();
        widget<QComboBox>(login, "serverCombo")->setCurrentText(peer.url().toString());
        QSignalSpy authenticated(&login, &LoginWindow::authenticationSucceeded);
        auto *test = widget<QPushButton>(login, "testConnectionButton");
        test->click();
        QTRY_VERIFY(test->isEnabled());
        QCOMPARE(authenticated.count(), 0);
        widget<QLineEdit>(login, "accountEdit")->setText("demo_user");
        auto *button = widget<QPushButton>(login, "loginButton");
        for (int i = 0; i < 2; ++i) {
            widget<QLineEdit>(login, "passwordEdit")->setText("test-password");
            button->click(); button->click();
            QTRY_VERIFY(button->isEnabled());
            QCOMPARE(authenticated.count(), 0);
        }
        QCOMPARE(peer.requests.size(), 3);
        QVERIFY(widget<QLabel>(login, "loginStatusLabel")->text().contains("账号或密码错误"));
    }

    void loginTimeoutAndServiceErrorsRecover() {
        HttpPeer peer;
        peer.responses = {{200, {}, -1}, {429, "{}"}, {503, "{}"}, {200, HttpPeer::authenticated()}};
        LoginWindow login(nullptr, 120);
        widget<QComboBox>(login, "serverCombo")->setCurrentText(peer.url().toString());
        widget<QLineEdit>(login, "accountEdit")->setText("demo_user");
        QSignalSpy authenticated(&login, &LoginWindow::authenticationSucceeded);
        const QStringList expected{"超时", "请求过多", "服务暂不可用"};
        for (const auto &text : expected) {
            widget<QLineEdit>(login, "passwordEdit")->setText("test-password");
            auto *button = widget<QPushButton>(login, "loginButton");
            button->click();
            QVERIFY(!widget<QPushButton>(login, "registerButton")->isEnabled());
            QTRY_VERIFY(button->isEnabled());
            QVERIFY(widget<QPushButton>(login, "registerButton")->isEnabled());
            QVERIFY(widget<QLabel>(login, "loginStatusLabel")->text().contains(text));
            QCOMPARE(authenticated.count(), 0);
        }
        widget<QLineEdit>(login, "passwordEdit")->setText("test-password");
        widget<QPushButton>(login, "loginButton")->click();
        QTRY_COMPARE(authenticated.count(), 1);
    }

    void closingRegistrationDiscardsLateResult() {
        HttpPeer peer;
        peer.responses = {{201, HttpPeer::registered(), 180}, {409, "{}"}};
        LoginWindow login;
        login.show();
        widget<QComboBox>(login, "serverCombo")->setCurrentText(peer.url().toString());
        widget<QLineEdit>(login, "accountEdit")->setText("original");
        auto *open = widget<QPushButton>(login, "registerButton");
        open->click();
        QPointer<RegisterDialog> first = login.findChild<RegisterDialog *>();
        QVERIFY(first);
        fill(*first);
        widget<QPushButton>(*first, "registerSubmitButton")->click();
        QTRY_COMPARE(peer.requests.size(), 1);
        widget<QPushButton>(*first, "registerBackButton")->click();
        QTRY_VERIFY(first.isNull());
        open->click();
        auto *second = login.findChild<RegisterDialog *>();
        QVERIFY(second);
        fill(*second);
        widget<QPushButton>(*second, "registerSubmitButton")->click();
        QTRY_VERIFY(widget<QPushButton>(*second, "registerSubmitButton")->isEnabled());
        QTest::qWait(220);
        QCOMPARE(widget<QLineEdit>(login, "accountEdit")->text(), QString("original"));
        QVERIFY(second->isVisible());
        QVERIFY(widget<QLabel>(*second, "registerStatusLabel")->text().contains("账号已存在"));
    }

    void closingAndDestroyingLoginDiscardsLateResult() {
        HttpPeer peer;
        peer.responses = {{200, HttpPeer::authenticated(), 120}, {200, HttpPeer::authenticated(), 120}};
        auto login = std::make_unique<LoginWindow>();
        login->show();
        widget<QComboBox>(*login, "serverCombo")->setCurrentText(peer.url().toString());
        widget<QLineEdit>(*login, "accountEdit")->setText("demo_user");
        widget<QLineEdit>(*login, "passwordEdit")->setText("test-password");
        QSignalSpy authenticated(login.get(), &LoginWindow::authenticationSucceeded);
        widget<QPushButton>(*login, "loginButton")->click();
        QTRY_COMPARE(peer.requests.size(), 1);
        login->close();
        QTest::qWait(160);
        QCOMPARE(authenticated.count(), 0);
        login->show();
        widget<QLineEdit>(*login, "passwordEdit")->setText("test-password");
        widget<QPushButton>(*login, "loginButton")->click();
        QTRY_COMPARE(peer.requests.size(), 2);
        login.reset();
        QTest::qWait(160);
        QCOMPARE(authenticated.count(), 0);
    }

    void registrationPreview() {
        RegisterDialog dialog(QUrl("http://127.0.0.1:8081"));
        dialog.show();
        fill(dialog, "mismatch");
        QTest::qWait(30);
        QVERIFY(dialog.grab().save("registration-preview.png"));
    }
};

QTEST_MAIN(RegistrationTests)
#include "registration_test.moc"
