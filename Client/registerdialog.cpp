#include "registerdialog.h"
#include "ApiClient.hpp"
#include "clienttheme.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QRegularExpression>
#include <QStyle>
#include <QVBoxLayout>

RegisterDialog::RegisterDialog(const QUrl &serverUrl, QWidget *parent, int authenticationTimeoutMs)
    : QDialog(parent), client_(new ApiClient(this, authenticationTimeoutMs)), serverUrl_(serverUrl)
{
    setObjectName("registerDialog");
    setWindowTitle(tr("云盘 · 注册账号"));
    setWindowIcon(ClientTheme::icon(ClientTheme::Symbol::Cloud));
    setStyleSheet(ClientTheme::styleSheet());
    setMinimumWidth(420);
    resize(460, 510);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(10);
    auto *title = new QLabel(tr("创建云盘账号"), this);
    title->setObjectName("loginTitle");
    layout->addWidget(title);
    auto *server = new QLabel(tr("服务器：%1").arg(serverUrl_.toString()), this);
    server->setTextFormat(Qt::PlainText);
    server->setWordWrap(true);
    layout->addWidget(server);

    auto field = [this, layout](const QString &title, const char *name, bool password) {
        auto *label = new QLabel(title, this);
        label->setObjectName("fieldLabel");
        auto *edit = new QLineEdit(this);
        edit->setObjectName(QString::fromLatin1(name));
        if (password) edit->setEchoMode(QLineEdit::Password);
        label->setBuddy(edit);
        layout->addWidget(label);
        layout->addWidget(edit);
        return edit;
    };
    usernameEdit_ = field(tr("用户名"), "registerUsernameEdit", false);
    usernameEdit_->setPlaceholderText(tr("4～16 位字母、数字、下划线或减号"));
    passwordEdit_ = field(tr("密码"), "registerPasswordEdit", true);
    passwordEdit_->setPlaceholderText(tr("8～1024 字节，不自动去除空格"));
    confirmationEdit_ = field(tr("确认密码"), "registerConfirmationEdit", true);
    confirmationEdit_->setPlaceholderText(tr("再次输入密码"));
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("registerStatusLabel");
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setWordWrap(true);
    statusLabel_->setMinimumHeight(54);
    layout->addWidget(statusLabel_);
    submitButton_ = new QPushButton(tr("注册"), this);
    submitButton_->setObjectName("registerSubmitButton");
    submitButton_->setProperty("primary", true);
    submitButton_->setDefault(true);
    layout->addWidget(submitButton_);
    backButton_ = new QPushButton(tr("返回登录"), this);
    backButton_->setObjectName("registerBackButton");
    backButton_->setAutoDefault(false);
    layout->addWidget(backButton_);

    for (auto *edit : {usernameEdit_, passwordEdit_, confirmationEdit_})
        connect(edit, &QLineEdit::textChanged, this, &RegisterDialog::updateHint);
    connect(submitButton_, &QPushButton::clicked, this, &RegisterDialog::submit);
    connect(backButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(client_, &ApiClient::registrationFinished, this, [this](bool created, const QString &message) {
        if (!pending_) return;
        setPending(false);
        if (!created) {
            showStatus(message, "error"); // 保留输入供修正或重试。
            return;
        }
        const QString username = submittedUsername_;
        passwordEdit_->clear();
        confirmationEdit_->clear();
        // 先完成对话框自身操作，再通知外部，避免外部槽销毁窗口后继续访问 this。
        QPointer<RegisterDialog> guard(this);
        accept();
        if (guard) emit registrationSucceeded(username);
    });
    updateHint();
    usernameEdit_->setFocus();
}

// 	用户名、密码、确认密码是否合法
QString RegisterDialog::validationError(bool submitting) const
{
    const QString username = usernameEdit_->text();
    static const QRegularExpression pattern(QStringLiteral("\\A[a-zA-Z0-9_-]{4,16}\\z"));
    if ((submitting || !username.isEmpty()) && !pattern.match(username).hasMatch())
        return tr("用户名须为 4～16 位字母、数字、下划线或减号。");
    const auto size = passwordEdit_->text().toUtf8().size();
    if ((submitting || size > 0) && (size < 8 || size > 1024))
        return tr("密码长度须为 8～1024 字节。");
    if ((submitting || !confirmationEdit_->text().isEmpty())
        && confirmationEdit_->text() != passwordEdit_->text())
        return tr("两次输入的密码不一致，请重新确认。");
    return {};
}

void RegisterDialog::updateHint()
{
    // 上一个请求还没结束，不重复提交
    if (pending_) return;
    const QString error = validationError(false);
    showStatus(error.isEmpty() ? tr("注册成功后将返回登录页。") : error,
               error.isEmpty() ? "normal" : "error");
}

void RegisterDialog::submit()
{
    if (pending_) return;
    const QString error = validationError(true);
    if (!error.isEmpty()) {
        showStatus(error, "error");
        return;
    }
    submittedUsername_ = usernameEdit_->text();
    //	开始等待结果，同时调整控件状态
    setPending(true);
    showStatus(tr("正在注册……可返回登录取消等待；取消不会撤销已完成的注册。"));
    client_->registerUser(serverUrl_, submittedUsername_, passwordEdit_->text());
}

void RegisterDialog::setPending(bool pending)
{
    pending_ = pending;
    usernameEdit_->setEnabled(!pending);
    passwordEdit_->setEnabled(!pending);
    confirmationEdit_->setEnabled(!pending);
    submitButton_->setEnabled(!pending);
    submitButton_->setText(pending ? tr("注册中……") : tr("注册"));
    // 返回按钮和系统关闭按钮保持可用，不阻塞 GUI 事件循环。
}

void RegisterDialog::done(int result)
{
    client_->cancelAuthentication();
    setPending(false);
    passwordEdit_->clear();
    confirmationEdit_->clear();
    QDialog::done(result);
}

void RegisterDialog::showStatus(const QString &message, const char *state)
{
    statusLabel_->setText(message);
    statusLabel_->setProperty("state", QString::fromLatin1(state));
    statusLabel_->style()->unpolish(statusLabel_);
    statusLabel_->style()->polish(statusLabel_);
}
