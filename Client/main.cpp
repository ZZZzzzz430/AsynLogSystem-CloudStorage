#include "loginwindow.h"
#include "mainwindow.h"

#include <QApplication>
#include <memory>

// 程序入口只组装窗口和信号槽，不在这里实现认证协议或上传下载业务。
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // QApplication 必须先于所有 QWidget 创建，且最后析构。
    // login 是栈对象，整个事件循环期间有效；关闭唯一可见窗口会退出程序。
    LoginWindow login;

    // MainWindow 无 QWidget 父对象，由 unique_ptr 独占管理，首次验证成功才构造。
    // 不设置 WA_DeleteOnClose，避免 Qt 自删与 unique_ptr 重复释放。
    // 事件循环结束后先销毁 mainWindow，再销毁 login，最后销毁 QApplication。
    std::unique_ptr<MainWindow> mainWindow;

    // 认证接线移到 LoginWindow::setupConnections()：loginRequested → ApiClient::login
    // → loginFinished → handleLoginResult。TODO E01 留在 ApiClient，不在入口默认放行。

    // 仅真实认证成功通知能走到这里；输入非空、按钮点击都不会直接打开主窗口。
    // 捕获的两个局部对象都活到 a.exec() 返回；回调在 GUI 线程执行。
    QObject::connect(&login, &LoginWindow::authenticationSucceeded,
                     &login, [&login, &mainWindow](const QUrl &serverUrl) {
        if (mainWindow) {
            return; // 防止重复成功通知创建多个主窗口。
        }
        mainWindow = std::make_unique<MainWindow>();
        // 把提交时的服务器地址传给文件页；未来还需要用户自行接入真实会话凭据。
        mainWindow->setServerUrl(serverUrl);
        // 先 show 再 hide，切换期间始终有可见窗口，避免最后窗口关闭导致退出。
        mainWindow->show();
        login.hide(); // 仅隐藏栈对象，不调用 deleteLater()；关闭主窗口即退出。
    });

    login.show();
    return a.exec();
}
