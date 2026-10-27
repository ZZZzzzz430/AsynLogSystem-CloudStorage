#include "loginwindow.h"

#include "ApiClient.hpp"
#include "clienttheme.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// 辅助函数只组织标签与输入框，所有对象仍归卡片所有，不在这里判断账号真伪。
void addField(QVBoxLayout *layout, QWidget *card, const QString &title, QWidget *input)
{
    auto *label = new QLabel(title, card);
    label->setObjectName(QStringLiteral("fieldLabel"));
    label->setBuddy(input);
    layout->addWidget(label);
    layout->addWidget(input);
}

} // namespace

// 构造时只创建对象并接线；不主动登录、不读取或保存密码。
LoginWindow::LoginWindow(QWidget *parent)
    : QDialog(parent)
    , client_(new ApiClient(this))
{
    setupUi();
    setupConnections();
}

// 代码构建界面，保持普通系统标题栏；内容布局参考用户提供的云盘登录截图。
void LoginWindow::setupUi()
{
    setObjectName(QStringLiteral("loginWindow"));
    setWindowTitle(tr("云盘 · 登录"));
    setWindowIcon(ClientTheme::icon(ClientTheme::Symbol::Cloud));
    setStyleSheet(ClientTheme::styleSheet());
    resize(960, 740);
    setMinimumSize(760, 720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 品牌栏属于窗口内容，系统负责拖动/缩放/最小化/关闭，无需伪造窗口按钮。
    auto *brandBar = new QWidget(this);
    brandBar->setObjectName(QStringLiteral("brandBar"));
    brandBar->setFixedHeight(44);
    auto *brandLayout = new QHBoxLayout(brandBar);
    brandLayout->setContentsMargins(18, 6, 18, 6);
    auto *smallLogo = new QLabel(brandBar);
    smallLogo->setPixmap(ClientTheme::icon(ClientTheme::Symbol::Cloud).pixmap(30, 30));
    auto *brandName = new QLabel(tr("云盘"), brandBar);
    brandName->setObjectName(QStringLiteral("brandName"));
    brandLayout->addWidget(smallLogo);
    brandLayout->addWidget(brandName);
    brandLayout->addStretch();
    root->addWidget(brandBar);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(24, 18, 24, 24);
    root->addLayout(body, 1);
    body->addStretch();

    // 卡片宽度固定，背景随窗口伸缩；内容使用布局，不硬编码每个控件的坐标。
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setFixedWidth(424);
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(42);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(80, 137, 218, 35));
    card->setGraphicsEffect(shadow); // card 接管特效对象，无需手工 delete。
    auto *form = new QVBoxLayout(card);
    form->setContentsMargins(32, 22, 32, 20);
    form->setSpacing(7);

    auto *logo = new QLabel(card);
    logo->setPixmap(ClientTheme::icon(ClientTheme::Symbol::Cloud).pixmap(64, 64));
    logo->setAlignment(Qt::AlignCenter);
    form->addWidget(logo);
    auto *brand = new QLabel(tr("云盘"), card);
    brand->setObjectName(QStringLiteral("loginBrand"));
    brand->setAlignment(Qt::AlignCenter);
    form->addWidget(brand);
    auto *title = new QLabel(tr("登录云盘客户端"), card);
    title->setObjectName(QStringLiteral("loginTitle"));
    title->setAlignment(Qt::AlignCenter);
    form->addWidget(title);
    form->addSpacing(9);

    serverCombo_ = new QComboBox(card);
    serverCombo_->setObjectName(QStringLiteral("serverCombo"));
    serverCombo_->setEditable(true);
    serverCombo_->setInsertPolicy(QComboBox::NoInsert);
    serverCombo_->addItem(ClientTheme::icon(ClientTheme::Symbol::Server),
                          QStringLiteral("http://127.0.0.1:8081"));
    serverCombo_->lineEdit()->setObjectName(QStringLiteral("serverUrlEdit"));
    serverCombo_->setToolTip(tr("填写 http 或 https 服务器根地址，不包含业务路径"));
    addField(form, card, tr("服务器地址"), serverCombo_);
    form->addSpacing(5);

    accountEdit_ = new QLineEdit(card);
    accountEdit_->setObjectName(QStringLiteral("accountEdit"));
    accountEdit_->setPlaceholderText(tr("请输入账号或邮箱"));
    accountEdit_->addAction(ClientTheme::icon(ClientTheme::Symbol::Account), QLineEdit::LeadingPosition);
    addField(form, card, tr("账号 / 邮箱"), accountEdit_);
    form->addSpacing(5);

    passwordEdit_ = new QLineEdit(card);
    passwordEdit_->setObjectName(QStringLiteral("passwordEdit"));
    passwordEdit_->setPlaceholderText(tr("请输入密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->addAction(ClientTheme::icon(ClientTheme::Symbol::Lock), QLineEdit::LeadingPosition);
    passwordVisibilityAction_ = passwordEdit_->addAction(
        ClientTheme::icon(ClientTheme::Symbol::Eye), QLineEdit::TrailingPosition);
    passwordVisibilityAction_->setObjectName(QStringLiteral("togglePasswordAction"));
    passwordVisibilityAction_->setCheckable(true);
    passwordVisibilityAction_->setToolTip(tr("显示密码"));
    addField(form, card, tr("密码"), passwordEdit_);

    // TODO E03：接真实会话存储后再启用。当前不宣称能记住身份或自动登录。
    auto *preferences = new QHBoxLayout;
    auto *remember = new QCheckBox(tr("记住登录状态"), card);
    remember->setObjectName(QStringLiteral("rememberSessionCheck"));
    remember->setEnabled(false);
    remember->setToolTip(tr("会话保存功能暂不可用"));
    auto *automatic = new QCheckBox(tr("自动登录"), card);
    automatic->setObjectName(QStringLiteral("autoLoginCheck"));
    automatic->setEnabled(false);
    automatic->setToolTip(tr("自动登录功能暂不可用"));
    preferences->addWidget(remember);
    preferences->addStretch();
    preferences->addWidget(automatic);
    form->addLayout(preferences);
    form->addSpacing(3);

    loginButton_ = new QPushButton(tr("登录"), card);
    loginButton_->setObjectName(QStringLiteral("loginButton"));
    loginButton_->setProperty("primary", true);
    loginButton_->setIcon(ClientTheme::icon(ClientTheme::Symbol::ArrowRight, Qt::white));
    loginButton_->setLayoutDirection(Qt::RightToLeft); // 图标放在文字右侧，对齐参考图。
    loginButton_->setMinimumHeight(42);
    loginButton_->setDefault(true);
    form->addWidget(loginButton_);

    testConnectionButton_ = new QPushButton(tr("测试连接"), card);
    testConnectionButton_->setObjectName(QStringLiteral("testConnectionButton"));
    testConnectionButton_->setIcon(ClientTheme::icon(ClientTheme::Symbol::Link, QColor("#1979ff")));
    // 其他按钮禁止抢默认按钮，保证回车走登录流程。
    testConnectionButton_->setAutoDefault(false);
    form->addWidget(testConnectionButton_);

    statusLabel_ = new QLabel(tr("欢迎回来，请填写服务器地址与账号信息。"), card);
    statusLabel_->setObjectName(QStringLiteral("loginStatusLabel"));
    statusLabel_->setTextFormat(Qt::PlainText); // 错误提示作为文本显示，不解析远端 HTML。
    statusLabel_->setWordWrap(true);
    statusLabel_->setMinimumHeight(32);
    form->addWidget(statusLabel_);

    auto *separator = new QFrame(card);
    separator->setObjectName(QStringLiteral("separator"));
    separator->setFixedHeight(1);
    form->addWidget(separator);
    auto *links = new QHBoxLayout;
    auto *registerButton = new QPushButton(tr("注册账号"), card);
    auto *forgotButton = new QPushButton(tr("忘记密码？"), card);
    for (auto *button : {registerButton, forgotButton}) {
        button->setObjectName(QStringLiteral("textButton"));
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
    }
    links->addWidget(registerButton);
    links->addStretch();
    links->addWidget(forgotButton);
    form->addLayout(links);
    // TODO E03：接注册/找回页面；当前不打开虚构的远端地址。
    connect(registerButton, &QPushButton::clicked, this, [this] {
        showStatus(tr("注册服务暂不可用，请联系管理员。"));
    });
    connect(forgotButton, &QPushButton::clicked, this, [this] {
        showStatus(tr("请联系管理员重置密码。"));
    });

    body->addWidget(card, 0, Qt::AlignHCenter);
    body->addStretch();
    accountEdit_->setFocus();
}

// 各通道各自收口；connect 指明 context，窗口销毁时自动断开，避免悬空回调。
void LoginWindow::setupConnections()
{
    connect(loginButton_, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(testConnectionButton_, &QPushButton::clicked, this, &LoginWindow::onTestConnectionClicked);
    connect(client_, &ApiClient::connectionFinished, this, &LoginWindow::onConnectionFinished);
    connect(this, &LoginWindow::loginRequested, client_, &ApiClient::login);
    connect(client_, &ApiClient::loginFinished, this, &LoginWindow::handleLoginResult);
    connect(passwordVisibilityAction_, &QAction::toggled, this, [this](bool visible) {
        passwordEdit_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        passwordVisibilityAction_->setToolTip(visible ? tr("隐藏密码") : tr("显示密码"));
    });
}

// 与原主页面采用相同的根地址约束，不偷偷补协议或接受携带账号密码的 URL。
bool LoginWindow::readServerUrl(QUrl &url)
{
    url = QUrl(serverCombo_->currentText().trimmed());
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")
        || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery()
        || url.hasFragment() || (!url.path().isEmpty() && url.path() != "/")) {
        showStatus(tr("请输入有效的 http(s) 服务器根地址。"), "error");
        return false;
    }
    url.setPath(QStringLiteral("/"));
    return true;
}

// 只发送现有的 GET /，不提交凭据，也不会发认证成功信号。
void LoginWindow::onTestConnectionClicked()
{
    if (connectionPending_ || loginPending_) return;
    QUrl url;
    if (!readServerUrl(url)) return;
    connectionPending_ = true;
    testConnectionButton_->setEnabled(false);
    loginButton_->setEnabled(false);
    serverCombo_->setEnabled(false);
    showStatus(tr("正在连接服务器……"));
    client_->testConnection(url);
}

// reply 清理由 ApiClient 完成，此处只恢复界面，不改变任何登录身份。
void LoginWindow::onConnectionFinished(bool ok, int httpStatus, const QString &message)
{
    connectionPending_ = false;
    testConnectionButton_->setEnabled(!loginPending_);
    loginButton_->setEnabled(!loginPending_);
    serverCombo_->setEnabled(!loginPending_);
    showStatus(ok ? tr("服务器可达，HTTP %1。请继续登录。").arg(httpStatus)
                  : tr("连接失败：%1").arg(message), ok ? "success" : "error");
}

// 地址和非空仅是预检，凭据是否有效交给真实认证模块。
void LoginWindow::onLoginClicked()
{
    if (loginPending_ || connectionPending_) return;
    QUrl url;
    if (!readServerUrl(url)) return;
    const QString account = accountEdit_->text().trimmed();
    const QString password = passwordEdit_->text(); // 不改变密码中合法的空格。
    if (account.isEmpty() || password.isEmpty()) {
        showStatus(tr("请输入账号和密码。"), "error");
        return;
    }
    requestedServerUrl_ = url;
    setLoginPending(true);
    showStatus(tr("正在验证身份……"));
    passwordEdit_->clear(); // 不是安全内存擦除，只缩短输入框保存密码的时间。
    passwordVisibilityAction_->setChecked(false);
    // 先置等待状态再 emit，支持当前 TODO 同步回传失败；emit 后不覆盖结果。
    emit loginRequested(url, account, password);
}

// 唯一认证结果入口；verified 必须来自认证模块，不能来自测试连接或非空预检。
void LoginWindow::handleLoginResult(bool verified, const QString &message)
{
    if (!loginPending_) return;
    setLoginPending(false);
    if (!verified) {
        showStatus(message.isEmpty() ? tr("登录失败，请重试。") : message, "error");
        passwordEdit_->setFocus();
        return;
    }
    showStatus(tr("验证通过，正在打开文件页面……"), "success");
    // main 负责先 show 主窗口，再 hide 本窗口；栈上的 LoginWindow 不能 deleteLater。
    emit authenticationSucceeded(requestedServerUrl_);
}

// 等待标记与控件一起更新，避免重复提交；用户仍可随时关闭窗口。
void LoginWindow::setLoginPending(bool pending)
{
    loginPending_ = pending;
    serverCombo_->setEnabled(!pending && !connectionPending_);
    accountEdit_->setEnabled(!pending);
    passwordEdit_->setEnabled(!pending);
    loginButton_->setEnabled(!pending && !connectionPending_);
    testConnectionButton_->setEnabled(!pending && !connectionPending_);
}

// 动态属性仅改变提示颜色，必须重新应用样式；不发任何业务信号。
void LoginWindow::showStatus(const QString &message, const char *state)
{
    statusLabel_->setText(message);
    statusLabel_->setProperty("state", QString::fromLatin1(state));
    statusLabel_->style()->unpolish(statusLabel_);
    statusLabel_->style()->polish(statusLabel_);
}

// 背景由渐变和曲线构成；没有将截图当整张图片贴到窗口上。
void LoginWindow::paintEvent(QPaintEvent *event)
{
    QDialog::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, width(), height());
    gradient.setColorAt(0, QColor("#e0efff"));
    gradient.setColorAt(0.5, QColor("#edf5ff"));
    gradient.setColorAt(1, QColor("#f8fbff"));
    painter.fillRect(rect(), gradient);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 110));
    QPainterPath wave;
    wave.moveTo(0, height() * 0.70);
    wave.cubicTo(width() * 0.30, height() * 0.51,
                 width() * 0.55, height() * 0.57, width(), height() * 0.84);
    wave.lineTo(width(), height());
    wave.lineTo(0, height());
    wave.closeSubpath();
    painter.drawPath(wave);
}
