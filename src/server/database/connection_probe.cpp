#include "MySqlConnection.hpp"

#include <mysql.h>

#include <iostream>

// 独立连接验证程序。只连接和 ping，不执行 SQL，也不查询或打印用户信息。
// 单线程示例：库初始化 → 连接对象 → 对象析构 → 库清理，顺序不可颠倒。
int main()
{
    storage::db::MySqlConfig config;
    auto status = storage::db::loadMySqlConfigFromEnvironment(config);
    if (!status.ok()) {
        std::cerr << "Configuration: " << status.message << '\n';
        return 1;
    }
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        std::cerr << "mysql_library_init failed\n";
        return 1;
    }

    int exitCode = 1;
    {
        storage::db::MySqlConnection connection;
        status = connection.open(config);
        // 不需要再使用密码；clear 不承诺对底层内存进行安全擦除。
        config.password.clear();
        if (!status.ok()) {
            std::cerr << "Connection: " << status.message << '\n';
        } else {
            status = connection.ping();
            if (!status.ok()) std::cerr << "Ping: " << status.message << '\n';
            else {
                std::cout << "MySQL connection and ping succeeded.\n";
                exitCode = 0;
            }
        }
    } // 先析构 connection，释放 MYSQL 句柄。
    mysql_library_end();
    return exitCode;
}
