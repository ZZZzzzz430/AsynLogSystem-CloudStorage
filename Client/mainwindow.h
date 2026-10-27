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

    // 登录成功后由 main 注入同一次提交使用的服务器地址，不在构造时自动发请求。
    void setServerUrl(const QUrl &serverUrl);

private:
    // 设置共用视觉、表头和空态；不填充虚假的服务端文件记录。
    void setupWorkspace();

    // 选择文件只展示本地候选，不读取文件正文、不自动开始上传。
    void onSelectFileClicked();

    // TODO E02：文件预检练习入口，当前明确返回失败，供用户补齐而不默认通过。
    bool validateSelectedFile(QString &reason) const;

    // 校验地址及本地文件后调用 ApiClient::uploadFile；协议细节留在网络层。
    void onUploadClicked();

    // 采集远端文件名和保存位置；不因选择保存位置就创建正式目标文件。
    void onDownloadClicked();

    // 同时只允许一个传输任务，集中管理按钮和进度控件状态。
    void beginTransfer(bool uploading);

    // 进度仅展示已传输字节；100% 时仍须等待最终 finished 结果。
    void onTransferProgress(qint64 transferred, qint64 total);

    // 上传与下载共用完成收口，不从按钮状态或进度条推导成功。
    void onTransferFinished(bool completed, const QString &message);

    // 与连接测试复用同一服务器根地址约束，非法地址不会发出网络请求。
    bool readServerUrl(QUrl &url);

    // setupUi() 创建的控件归 MainWindow 所有，ui 只是访问这些控件的包装对象。
    Ui::MainWindow *ui;
    // 父对象是 this，随主窗口销毁；登录窗口不复用此连接测试对象进行认证。
    ApiClient* client_ = nullptr;

    // 只保存用户主动选中的本地候选文件；不把它当作已经上传的远端文件。
    QString selectedFilePath_;
    qint64 selectedFileSize_ = -1;
    bool transferPending_ = false;
    bool uploading_ = false;

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
