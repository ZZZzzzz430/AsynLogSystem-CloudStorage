#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

// 登录窗口只收集输入和展示状态，不判断账号真伪，也不创建主窗口。
// 本类和所有控件均在 GUI 线程使用；异步认证结果应通过带 context 的信号槽返回。
class LoginWindow : public QDialog
{
    Q_OBJECT

public:
    // main() 在栈上创建无父窗口的 LoginWindow，使其存活到事件循环结束。
    // 如果传入 parent，则仍遵循 Qt 的父子对象所有权规则。
    explicit LoginWindow(QWidget *parent = nullptr);

public slots:
    // 认证结果的唯一接入口；只处理本窗口正在等待的一次请求。
    // verified 必须来自未来真实认证模块，不能由连接成功、非空输入或定时器推导。
    // message 用于失败提示；当前 main() 的 TODO 占位只传 false。
    void handleLoginResult(bool verified, const QString &message);

signals:
    // 输入非空后发出请求意图；非空检查只是界面预检，不代表认证通过。
    // TODO：未来接认证模块；跨异步边界时由接收方管理凭据寿命，禁止记录密码。
    void loginRequested(const QString &account, const QString &password);

    // 只有 handleLoginResult() 收到真实成功结果时才发出，由 main() 负责切窗口。
    void authenticationSucceeded();

private:
    // 创建界面和信号槽；全部控件交给 Qt 父子对象树管理，无需手动 delete。
    void setupUi();

    // 读取输入、完成最小非空检查、进入等待状态，再发 loginRequested。
    void onLoginClicked();

    // 同步等待标记和控件可用状态，防止一次认证尚未结束时重复提交。
    void setLoginPending(bool pending);

    // 以下指针只用于访问控件，不单独拥有对象；LoginWindow 析构时自动释放控件。
    QLineEdit *accountEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;

    // 仅在 GUI 线程读写，不引入线程、锁或网络请求。
    bool loginPending_ = false;
};

#endif // LOGINWINDOW_H
