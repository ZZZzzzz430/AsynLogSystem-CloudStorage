#include "loginwindow.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

// 构造时仅搭建界面：不发认证请求，不设置任何默认登录身份。
LoginWindow::LoginWindow(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
}

// 用代码构建界面，无需额外 .ui 文件；原 mainwindow.ui 保持原样。
void LoginWindow::setupUi()
{
    setObjectName(QStringLiteral("loginWindow"));
    setWindowTitle(tr("云存储客户端 - 登录"));
    resize(400, 240);

    // 布局安装到 this；表单控件显式指定 this 为父对象，窗口结束时统一清理。
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    accountEdit_ = new QLineEdit(this);
    accountEdit_->setObjectName(QStringLiteral("accountEdit"));
    accountEdit_->setPlaceholderText(tr("请输入账号"));
    form->addRow(tr("账号："), accountEdit_);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setObjectName(QStringLiteral("passwordEdit"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(tr("请输入密码"));
    form->addRow(tr("密码："), passwordEdit_);

    statusLabel_ = new QLabel(tr("登录验证尚未实现，请先完成 TODO 接口。"), this);
    statusLabel_->setObjectName(QStringLiteral("loginStatusLabel"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    loginButton_ = new QPushButton(tr("登录"), this);
    loginButton_->setObjectName(QStringLiteral("loginButton"));
    loginButton_->setDefault(true);
    layout->addWidget(loginButton_);

    // 使用 this 作为接收对象；窗口销毁时 Qt 自动断开连接，不留悬空回调。
    // QDialog 的默认按钮支持回车提交，与鼠标点击共用同一处理函数。
    connect(loginButton_, &QPushButton::clicked,
            this, &LoginWindow::onLoginClicked);
    accountEdit_->setFocus();
}

// 本地预检仅拦截空输入；不要在这里写死账号密码或调用 accept() 放行。
void LoginWindow::onLoginClicked()
{
    if (loginPending_) {
        return;
    }

    const QString account = accountEdit_->text().trimmed();
    // 密码不做 trimmed()，避免改变用户输入的合法空格。
    const QString password = passwordEdit_->text();
    if (account.isEmpty() || password.isEmpty()) {
        statusLabel_->setText(tr("请输入账号和密码；填写完整并不代表验证通过。"));
        return;
    }

    setLoginPending(true);
    statusLabel_->setText(tr("等待登录验证结果……"));
    // 清空输入框只是缩短界面保存密码的时间，不保证 QString 内存被安全擦除。
    passwordEdit_->clear();

    // 先设置等待状态，再发信号：当前 TODO 接收方可能在同一调用栈立即返回失败。
    // 不在 emit 后覆盖状态，避免把已经返回的结果重新改成“等待”。
    emit loginRequested(account, password);
}

// 由认证模块回传结果；当前项目尚无该模块，main() 明确回传“未实现”。
void LoginWindow::handleLoginResult(bool verified, const QString &message)
{
    if (!loginPending_) {
        return;
    }

    setLoginPending(false);
    if (!verified) {
        statusLabel_->setText(message.isEmpty() ? tr("登录验证失败。") : message);
        passwordEdit_->setFocus();
        return;
    }

    // 仅作窗口切换通知；会话凭据保存、权限校验仍由未来认证模块实现。
    // 此处不 close()/accept()，由 main() 先显示主窗口再隐藏登录窗口。
    statusLabel_->setText(tr("验证通过，正在打开主界面……"));
    emit authenticationSucceeded();
}

// 控件状态与等待标记一起更新；关闭窗口始终可用，未实现认证不会困住用户。
void LoginWindow::setLoginPending(bool pending)
{
    loginPending_ = pending;
    accountEdit_->setEnabled(!pending);
    passwordEdit_->setEnabled(!pending);
    loginButton_->setEnabled(!pending);
}
