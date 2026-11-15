#pragma once

#include <QDialog>
#include <QUrl>

class ApiClient;
class QLabel;
class QLineEdit;
class QPushButton;

class RegisterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RegisterDialog(const QUrl &serverUrl, QWidget *parent = nullptr,
                            int authenticationTimeoutMs = 10000);

signals:
    void registrationSucceeded(const QString &username);

public slots:
    void done(int result) override;

private:
    QString validationError(bool submitting) const;
    void updateHint();
    void submit();
    void setPending(bool pending);
    void showStatus(const QString &message, const char *state = "normal");

    ApiClient *client_;
    QUrl serverUrl_;
    QLineEdit *usernameEdit_;
    QLineEdit *passwordEdit_;
    QLineEdit *confirmationEdit_;
    QPushButton *submitButton_;
    QPushButton *backButton_;
    QLabel *statusLabel_;
    QString submittedUsername_;
    bool pending_ = false;
};
