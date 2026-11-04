#include "loginwindow.h"
#include "mainwindow.h"
#include "ApiClient.hpp"
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QMenuBar>
#include <QAction>
#include <iostream>
#include <memory>
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    if(argc!=2)return 2;
    LoginWindow login;
    std::unique_ptr<MainWindow> main;
    auto *client=login.apiClient();
    auto *account=login.findChild<QLineEdit *>("accountEdit");
    auto *password=login.findChild<QLineEdit *>("passwordEdit");
    auto *server=login.findChild<QComboBox *>("serverCombo");
    auto *button=login.findChild<QPushButton *>("loginButton");
    if(!account||!password||!server||!button)return 2;
    server->setCurrentText(QString::fromLocal8Bit(argv[1]));
    account->setText(qEnvironmentVariable("STORAGE_TEST_ACCOUNT"));
    int phase=0;
    auto fail=[&]{std::cerr<<"FAIL Qt UI phase "<<phase<<'\n';app.exit(1);};
    QObject::connect(&login,&LoginWindow::authenticationSucceeded,&login,[&](const QUrl &url){
        if(phase!=1){fail();return;}
        main=std::make_unique<MainWindow>(client);
        main->setServerUrl(url);main->show();login.hide();phase=2;
    });
    QObject::connect(client,&ApiClient::loginFinished,&login,[&](bool ok,const QString &){
        if(phase==0&&!ok&&login.isVisible()&&!main&&password->text().isEmpty()){
            std::cout<<"PASS Qt login widget rejects wrong credentials and clears password\n";
            phase=1;password->setText(qEnvironmentVariable("STORAGE_TEST_PASSWORD"));button->click();
        }else if(phase!=2){fail();}
    });
    QObject::connect(client,&ApiClient::sessionVerified,&login,[&](bool ok,const QString &){
        if(phase==2&&ok&&main&&main->isVisible()&&!login.isVisible()){
            std::cout<<"PASS Qt window transition shares authenticated ApiClient\n";
            phase=3;
            // Exercise the actual MainWindow logout menu action.
            main->menuBar()->actions().back()->trigger();
        }else fail();
    });
    QObject::connect(client,&ApiClient::logoutFinished,&login,[&](bool ok,const QString &){
        if(phase==3&&ok){std::cout<<"PASS Qt logout menu revokes session\n";app.exit(0);}else fail();
    });
    login.show();
    QTimer::singleShot(0,&login,[&]{password->setText("wrong-password");button->click();});
    QTimer::singleShot(60000,&login,[&]{fail();});
    return app.exec();
}
