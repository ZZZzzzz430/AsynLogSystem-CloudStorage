#include <mysql.h>

#include <iostream>

// 只检查“头文件能编译、C API 能链接、动态库能加载”，不连接任何数据库。
// 这不是 mysql_ping，也不能证明密码正确、数据库存在或 SQL 已实现。
int main()
{
    std::cout << "MySQL client SDK: " << mysql_get_client_info() << '\n';
    std::cout << "SDK only; no database connection or SQL was executed.\n";
    return 0;
}
