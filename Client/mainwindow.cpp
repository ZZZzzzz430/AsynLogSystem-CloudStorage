#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QUrl>
#include <QDebug>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include "ApiClient.hpp"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , client_(new ApiClient(this))
{
    ui->setupUi(this);

    // 1. 连接测试按钮的 clicked 信号
    connect(ui->testConnectionButton,
            &QPushButton::clicked,
            this,
            &MainWindow::onTestConnectionClicked);

    // 2. 连接 ApiClient 的结果信号
    connect(client_,
            &ApiClient::connectionFinished,
            this,
            &MainWindow::onConnectionFinished);
}

MainWindow::~MainWindow()
{
    delete ui;
}

// 用户点击测试连接
void MainWindow::onTestConnectionClicked()
{
    // 获取输入框内容
    QString urlText = ui->serverUrlEdit->text().trimmed();

    QUrl url(urlText);

    // 检查协议和主机
    if (!url.isValid() ||
        (url.scheme() != "http" && url.scheme() != "https") ||
        url.host().isEmpty() ||
        !url.userInfo().isEmpty() ||
        !url.query().isEmpty() ||
        !url.fragment().isEmpty() ||
        (url.path() != "" && url.path() != "/")) {

        ui->connectionStatusLabel->setText("服务器地址格式错误");
        return;
    }

    // 统一测试根路径 /
    url.setPath("/");

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

