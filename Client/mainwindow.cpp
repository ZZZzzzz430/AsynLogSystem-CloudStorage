#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "clienttheme.h"

#include <QUrl>
#include <QDebug>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QProgressBar>
#include <QStackedWidget>
#include <QTableWidget>
#include "ApiClient.hpp"

// 保留原主页面及连接测试接线；仅由入口在验证成功后创建并显示。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , client_(new ApiClient(this))
{
    ui->setupUi(this);
    setupWorkspace();

    // 页面只采集输入和展示状态，真实传输仍交给 ApiClient 中留给用户的接口。
    connect(ui->selectFileButton, &QPushButton::clicked, this, &MainWindow::onSelectFileClicked);
    connect(ui->emptySelectButton, &QPushButton::clicked, this, &MainWindow::onSelectFileClicked);
    connect(ui->uploadButton, &QPushButton::clicked, this, &MainWindow::onUploadClicked);
    connect(ui->downloadButton, &QPushButton::clicked, this, &MainWindow::onDownloadClicked);
    connect(client_, &ApiClient::transferProgress, this, &MainWindow::onTransferProgress);
    connect(client_, &ApiClient::uploadFinished, this, &MainWindow::onTransferFinished);
    connect(client_, &ApiClient::downloadFinished, this, &MainWindow::onTransferFinished);
    connect(ui->refreshButton, &QPushButton::clicked, this, [this] {
        // TODO E06：确认真实列表 API 后再请求，不从 HTML 页面猜测用户文件列表。
        ui->transferStatusLabel->setText(tr("文件列表刷新服务暂不可用。"));
    });

    // 1. 连接测试按钮的 clicked 信号；this 为接收对象，销毁时自动断连。
    connect(ui->testConnectionButton,
            &QPushButton::clicked,
            this,
            &MainWindow::onTestConnectionClicked);

    // 2. 连接 ApiClient 的结果信号；结果只更新主页面，不触发登录成功。
    connect(client_,
            &ApiClient::connectionFinished,
            this,
            &MainWindow::onConnectionFinished);
}

// 只搭建视觉与空态；原连接测试控件仍保留相同 objectName，方便继续调试。
void MainWindow::setupWorkspace()
{
    setWindowTitle(tr("云盘 · 我的文件"));
    setWindowIcon(ClientTheme::icon(ClientTheme::Symbol::Cloud));
    ui->sidebarLogo->setPixmap(ClientTheme::icon(ClientTheme::Symbol::Cloud).pixmap(38, 38));
    ui->emptyIcon->setPixmap(ClientTheme::icon(ClientTheme::Symbol::Folder, QColor("#86b6fa")).pixmap(72, 72));
    ui->filesButton->setIcon(ClientTheme::icon(ClientTheme::Symbol::Folder, QColor("#1979ff")));
    ui->selectFileButton->setIcon(ClientTheme::icon(ClientTheme::Symbol::File));
    ui->uploadButton->setIcon(ClientTheme::icon(ClientTheme::Symbol::Upload, Qt::white));
    ui->uploadButton->setProperty("primary", true);
    ui->uploadButton->setEnabled(false); // 未选文件前不能开始上传。
    ui->downloadButton->setIcon(ClientTheme::icon(ClientTheme::Symbol::Download));
    ui->refreshButton->setIcon(ClientTheme::icon(ClientTheme::Symbol::Refresh));
    ui->testConnectionButton->setIcon(ClientTheme::icon(ClientTheme::Symbol::Link));
    ui->filesStack->setCurrentIndex(0);
    ui->fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->fileTable->verticalHeader()->hide();
    ui->fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->fileTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->fileTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->fileTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->transferProgressBar->setTextVisible(false);
    ui->transferProgressBar->setValue(0);
    ui->transferStatusLabel->setTextFormat(Qt::PlainText);
    ui->connectionStatusLabel->setTextFormat(Qt::PlainText);
    // 属性设置后再应用样式，确保主按钮第一次显示时就使用蓝色。
    setStyleSheet(ClientTheme::styleSheet());
}

// 入口只在真实认证通过后调用；这里不自动发 GET、列表或上传请求。
void MainWindow::setServerUrl(const QUrl &serverUrl)
{
    ui->serverUrlEdit->setText(serverUrl.toString());
    ui->serverHintLabel->setText(serverUrl.host());
    ui->serverHintLabel->setToolTip(serverUrl.toString());
}

// 这个函数仅完成“选择和展示”，不是 D02 已通过，更不是远端上传已成功。
void MainWindow::onSelectFileClicked()
{
    if (transferPending_) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("选择待上传文件"));
    if (path.isEmpty()) return; // 用户取消时保留上一次选择，不清空、不发网络请求。

    const QFileInfo file(path);
    selectedFilePath_ = file.absoluteFilePath();
    selectedFileSize_ = file.size();
    ui->fileTable->setRowCount(1);
    auto *name = new QTableWidgetItem(ClientTheme::icon(ClientTheme::Symbol::File), file.fileName());
    name->setToolTip(selectedFilePath_);
    ui->fileTable->setItem(0, 0, name); // QTableWidget 接管 item 的所有权。
    ui->fileTable->setItem(0, 1, new QTableWidgetItem(tr("%1 字节").arg(selectedFileSize_)));
    ui->fileTable->setItem(0, 2, new QTableWidgetItem(tr("待检查")));
    ui->filesStack->setCurrentIndex(1);
    ui->uploadButton->setEnabled(true);
    ui->transferProgressBar->setRange(0, 100);
    ui->transferProgressBar->setValue(0);
    ui->transferStatusLabel->setText(tr("已选择本地文件，尚未上传。"));
}

// TODO E02（用户练习）：使用 QFileInfo/QFile 重新检查文件仍存在、是普通文件、
// 可读、大小非零且不超过 1 MiB；不要只相信选择时保存的 size，因为文件可能变化。
// 成功时清空 reason 并返回 true；失败时填写原因。当前保守返回 false，禁止误上传。
bool MainWindow::validateSelectedFile(QString &reason) const
{
    reason = tr("本地文件检查暂不可用，尚未开始上传。");
    return false;
}

// 页面层只决定是否可以发起操作，HTTP 请求体和协议头都交由网络层实现。
void MainWindow::onUploadClicked()
{
    if (transferPending_ || selectedFilePath_.isEmpty()) return;
    QUrl url;
    if (!readServerUrl(url)) return;
    QString reason;
    if (!validateSelectedFile(reason)) {
        ui->transferStatusLabel->setText(reason);
        return;
    }
    beginTransfer(true);
    // TODO E04 的占位会同步回传失败；状态必须先切到“传输中”，再调用接口。
    client_->uploadFile(url, selectedFilePath_);
}

// 旧协议支持按已知远端名下载；先采集目标，不抓 HTML 伪造服务端文件列表。
void MainWindow::onDownloadClicked()
{
    if (transferPending_) return;
    QUrl url;
    if (!readServerUrl(url)) return;
    const QString remoteName = ui->remoteNameEdit->text();
    if (remoteName.trimmed().isEmpty()) {
        ui->transferStatusLabel->setText(tr("请输入要下载的远端文件名。"));
        return;
    }
    const QString target = QFileDialog::getSaveFileName(
        this, tr("保存下载文件"), QFileInfo(remoteName).fileName());
    if (target.isEmpty()) return; // 取消不会请求服务端，也不会创建磁盘文件。
    beginTransfer(false);
    client_->downloadFile(url, remoteName, target);
}

// 当前只接一个传输任务，后续并发队列属于另一个练习，不能复用一个 bool 表示多任务。
void MainWindow::beginTransfer(bool uploading)
{
    transferPending_ = true;
    uploading_ = uploading;
    ui->selectFileButton->setEnabled(false);
    ui->uploadButton->setEnabled(false);
    ui->downloadButton->setEnabled(false);
    ui->refreshButton->setEnabled(false);
    ui->serverUrlEdit->setEnabled(false);
    ui->transferProgressBar->setRange(0, 0); // 总大小未知时使用忙碌指示，不制造百分比。
    ui->transferStatusLabel->setText(uploading ? tr("正在上传……") : tr("正在下载……"));
}

// total 可能未知或为零；百分比使用浮点运算避免 qint64 先乘 100 造成溢出。
void MainWindow::onTransferProgress(qint64 transferred, qint64 total)
{
    if (!transferPending_) return;
    if (total <= 0) {
        ui->transferProgressBar->setRange(0, 0);
        return;
    }
    const double ratio = qBound(0.0, static_cast<double>(transferred) / total, 1.0);
    ui->transferProgressBar->setRange(0, 100);
    ui->transferProgressBar->setValue(static_cast<int>(ratio * 100));
    if (transferred >= total) ui->transferStatusLabel->setText(tr("数据已传输，等待最终确认……"));
}

// 所有成功/失败路径恢复同一组控件；只有 completed=true 才标记最终完成。
void MainWindow::onTransferFinished(bool completed, const QString &message)
{
    if (!transferPending_) return;
    transferPending_ = false;
    ui->selectFileButton->setEnabled(true);
    ui->uploadButton->setEnabled(!selectedFilePath_.isEmpty());
    ui->downloadButton->setEnabled(true);
    ui->refreshButton->setEnabled(true);
    ui->serverUrlEdit->setEnabled(true);
    ui->transferProgressBar->setRange(0, 100);
    ui->transferProgressBar->setValue(completed ? 100 : 0);
    ui->transferStatusLabel->setText(message.isEmpty()
        ? (completed ? tr("传输完成。") : tr("传输失败。")) : message);
    if (uploading_ && ui->fileTable->rowCount() > 0) {
        ui->fileTable->item(0, 2)->setText(completed ? tr("已上传") : tr("上传失败"));
    }
}

// 保留原连接测试的根地址限制；上传/下载也经过同样的预检。
bool MainWindow::readServerUrl(QUrl &url)
{
    url = QUrl(ui->serverUrlEdit->text().trimmed());
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")
        || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery()
        || url.hasFragment() || (!url.path().isEmpty() && url.path() != "/")) {
        ui->connectionStatusLabel->setText(tr("服务器地址格式错误"));
        return false;
    }
    url.setPath(QStringLiteral("/"));
    return true;
}

MainWindow::~MainWindow()
{
    // UI 包装对象不是 QObject，需手动删除；控件及 client_ 不在这里重复 delete。
    delete ui;
}

// 用户点击测试连接
void MainWindow::onTestConnectionClicked()
{
    // 共用根地址预检，结果与旧连接测试一致；不改变任何登录或传输状态。
    QUrl url;
    if (!readServerUrl(url)) return;

    // 禁止重复点击
    ui->testConnectionButton->setEnabled(false);

    // 更新状态
    ui->connectionStatusLabel->setText("连接中...");

    // 发起异步 GET 请求
    client_->testConnection(url);
}

// 接收 ApiClient 请求结果
void MainWindow::onConnectionFinished(
    bool ok,
    int httpStatus,
    const QString& message)
{
    // 恢复按钮
    ui->testConnectionButton->setEnabled(true);

    if (ok) {
        ui->connectionStatusLabel->setText(
            QString("服务器可达，HTTP %1").arg(httpStatus)
        );
    }
    else {
        ui->connectionStatusLabel->setText(
            QString("连接失败：%1").arg(message)
        );
    }

    qDebug() << "HTTP:" << httpStatus << message;
}

