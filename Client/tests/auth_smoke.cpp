#include "ApiClient.hpp"
#include <QCoreApplication>
#include <QTimer>
#include <iostream>
// Real Qt Network requests against the real Linux HTTP server. Never prints credentials.
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2)return 2;
    QUrl url(QString::fromLocal8Bit(argv[1]));
    QString account=qEnvironmentVariable("STORAGE_TEST_ACCOUNT"),password=qEnvironmentVariable("STORAGE_TEST_PASSWORD");
    if(account.isEmpty()||password.isEmpty())return 2;
    ApiClient client;int phase=0;
    auto fail=[&] {std::cerr<<"FAIL Qt auth phase "<<phase<<'\n';app.exit(1);};
    QObject::connect(&client,&ApiClient::loginFinished,&app,[&](bool ok,const QString &) {
        if(phase==0&&!ok){std::cout<<"PASS Qt wrong password\n";phase=1;client.login(url,"missing_"+account,password);}
        else if(phase==1&&!ok){std::cout<<"PASS Qt missing user\n";phase=2;client.login(url,account,password);}
        else if(phase==2&&ok){std::cout<<"PASS Qt login\n";phase=3;client.verifySession(url);}
        else fail();
    });
    QObject::connect(&client,&ApiClient::sessionVerified,&app,[&](bool ok,const QString &) {
        if(phase==3&&ok){std::cout<<"PASS Qt authenticated request\n";phase=4;client.logout(url);}
        else if(phase==5&&!ok){std::cout<<"PASS Qt session cleared after logout\n";app.exit(0);}
        else fail();
    });
    QObject::connect(&client,&ApiClient::logoutFinished,&app,[&](bool ok,const QString &) {
        if(phase==4&&ok){std::cout<<"PASS Qt logout\n";phase=5;client.verifySession(url);}else fail();
    });
    QTimer::singleShot(0,&app,[&]{client.login(url,account,"wrong-password");});
    QTimer::singleShot(60000,&app,[&]{fail();});
    return app.exec();
}
