#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "ApiClient.hpp"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 验证成功后由 main() 延迟创建；窗口及 ApiClient 都在 GUI 线程工作。
    MainWindow(QWidget *parent = nullptr);
    // 只手动释放 UI 包装对象；子控件和 client_ 由 QObject 父子关系释放。
    ~MainWindow();

private:
    // setupUi() 创建的控件归 MainWindow 所有，ui 只是访问这些控件的包装对象。
    Ui::MainWindow *ui;
    // 父对象是 this，随主窗口销毁；登录窗口不复用此连接测试对象进行认证。
    ApiClient* client_ = nullptr;

    // 校验服务器根地址并发起原有异步 GET 测试，不改变任何登录状态。
    void onTestConnectionClicked();
    // 展示连接测试结果并恢复按钮；HTTP 可达只代表连接测试结果。
    void onConnectionFinished(
            bool ok,
            int httpStatus,
            const QString& message
            );
};
#endif // MAINWINDOW_H
