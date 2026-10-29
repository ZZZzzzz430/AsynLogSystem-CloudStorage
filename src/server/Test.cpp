/* ************************************************************************
> File Name:     test.cpp
> Created Time:  Thu 07 Sep 2023 06:37:16 PM CST
> Description:
 ************************************************************************/
#define DEBUG_LOG
#include "Service.hpp"
#include <thread>
#include <mysql.h>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <stdexcept>

using namespace std;

storage::DataManager *data_;
ThreadPool* tp=nullptr;
mylog::Util::JsonData* g_conf_data;
void service_module()
{
    storage::db::MySqlConfig config;
    auto status = storage::db::loadMySqlConfigFromEnvironment(config);
    if (!status.ok()) throw std::runtime_error(status.message);
    storage::db::MySqlConnection connection;
    status = connection.open(config);
    config.password.clear();
    if (!status.ok()) throw std::runtime_error(status.message);
    storage::db::UserRepository users(connection);
    storage::db::SessionRepository sessions(connection);
    storage::auth::AuthService auth(users, sessions);
    storage::Service s(auth);
    mylog::GetLogger("asynclogger")->Info("service step in RunModule");
    if (!s.RunModule()) throw std::runtime_error("HTTP service failed");
}

void log_system_module_init()
{
    g_conf_data = mylog::Util::JsonData::GetJsonData();
    tp = new ThreadPool(g_conf_data->thread_count);
    std::shared_ptr<mylog::LoggerBuilder> Glb(new mylog::LoggerBuilder());
    Glb->BuildLoggerName("asynclogger");
    Glb->BuildLoggerFlush<mylog::RollFileFlush>("./logfile/RollFile_log",
                                              1024 * 1024);
    // The LoggerManger has been built and is managed by members of the LoggerManger class
    //The logger is assigned to the managed object, and the caller lands the log by invoking the singleton managed object
    mylog::LoggerManager::GetInstance().AddLogger(Glb->Build());
}

void PrintStartupInfo() {
    std::error_code error;
    std::filesystem::path cwd = std::filesystem::current_path(error);
    if (error) {
      std::cerr << "[startup] 获取工作目录失败: "
        << error.message() << '\n';
      return;
    }
    std::cerr << "[startup] 工作目录"
      << cwd.string() << '\n';
}

bool CheckConfigFile(const std::filesystem::path& path) {
    
    std::error_code error;
    bool exists = std::filesystem::exists(path, error);

    if (error) {
      // 输出： 检查哪个路径失败，以及 error 信息
      std::cerr << "Chenk config file failed: "
        << path 
        << ", error: " 
        << error.message() 
        << std::endl;
      return false;
    }

    if (!exists){
        // 输出： 配置文件不存在，以及 path 路径
      std::cerr << "Config file does not exists"
        << path
        << std::endl;

      return false;
    }

    // 输出： 配置文件存在，以及 path
    std::cerr << "Config file exists : "
      << std::endl;
    return true;
}

int main()
{

    if (mysql_library_init(0, nullptr, nullptr) != 0) return 1;
    struct MySqlLibraryGuard { ~MySqlLibraryGuard() { mysql_library_end(); } } mysqlGuard;
    PrintStartupInfo();

    // 检查日志配置和存储配置，失败就退出。
    if (!CheckConfigFile("../../log_system/logs_code/config.conf")) {return 1;}
    if (!CheckConfigFile("Storage.conf")) {return 1;}

    
    // 设置边界
    try {
     log_system_module_init(); 
    }
    catch (const std::exception& error) {
      std::cerr << "[startup]" << error.what() << '\n' << std::endl;
      return 1;
    }
  

    data_ = new storage::DataManager();

    bool failed = false;
    thread t1([&failed] {
        try { service_module(); }
        catch (const std::exception &error) {
            std::cerr << "[database/service] " << error.what() << '\n'; failed = true;
        }
        catch (...) { std::cerr << "[database/service] startup failed\n"; failed = true; }
        mysql_thread_end();
    });
    t1.join();
    delete(tp);
    delete(data_);
    return failed ? 1 : 0;
}
