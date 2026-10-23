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
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    ApiClient* client_ = nullptr;

    void onTestConnectionClicked();
    void onConnectionFinished(
            bool ok,
            int httpStatus,
            const QString& message
            );
};
#endif // MAINWINDOW_H
