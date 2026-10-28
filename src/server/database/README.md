# 服务端 MySQL 连接

2026-10-28：已实现 DB01 配置读取和 DB03 连接/探活。用户查询、插入、认证、会话及 HTTP 路由仍为 TODO，不能把连接成功当作用户登录成功。

## 实现入口

- `MySqlConnection.cpp`：环境变量严格解析、配置校验、超时、TCP、utf8mb4、TLS、真实连接及探活；临时句柄使用 RAII，失败不会留下已连接状态。
- `MySqlConnection.hpp`：配置和连接接口；一个连接只能在其所属服务线程使用。
- `connection_probe.cpp`：可独立运行的完整调用示例，读取配置 → 初始化客户端库 → open → ping → 释放连接 → 清理客户端库。不执行 SQL。
- `connection_tests.cpp`：配置边界、凭据清理、未连接状态和可选的关闭端口测试。
- `mysql.env.example`：环境变量说明，不自动加载，不填写真实密码。

连接错误只返回操作名和 MySQL 数字错误码，不返回可能包含用户输入的原始错误文本。常见错误：1045 是账号/密码或账号来源不匹配，1049 是数据库不存在，1044 是数据库权限不足，2003 是地址/端口/服务不可达，2026 是 TLS 问题。

## Windows 构建

在仓库根目录用 PowerShell 执行。以下为本机已存在的工具和 SDK 路径，换机器时按实际安装位置调整。

```powershell
$env:PATH = 'E:\Qt\Tools\mingw1310_64\bin;E:\Qt\Tools\Ninja;C:\Program Files\MySQL\MySQL Server 8.0\lib;C:\Program Files\MySQL\MySQL Server 8.0\bin;' + $env:PATH

& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' `
  -S src/server/database -B build/mysql-scaffold -G Ninja `
  '-DCMAKE_BUILD_TYPE=Debug' '-DBUILD_TESTING=ON' `
  '-DMYSQL_ROOT=C:/Program Files/MySQL/MySQL Server 8.0' `
  '-DCMAKE_CXX_COMPILER=E:/Qt/Tools/mingw1310_64/bin/g++.exe' `
  '-DCMAKE_MAKE_PROGRAM=E:/Qt/Tools/Ninja/ninja.exe'

& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/mysql-scaffold --parallel 4
& 'E:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build/mysql-scaffold --output-on-failure
```

CTest 默认只测试配置和状态，不连接数据库。`mysql_sdk_probe.exe` 也只检查 SDK；真实连接请运行下面的 `mysql_connection_probe.exe`。

## 本机连接验证

在上述终端中执行，库名和专用账号改成你在 Workbench 中创建的实际值。此例仅用于服务端程序与 MySQL 同在 Windows 的本机练习。

```powershell
$env:STORAGE_DB_HOST = '127.0.0.1'
$env:STORAGE_DB_PORT = '3306'
$env:STORAGE_DB_NAME = 'cloud_storage'
$env:STORAGE_DB_USER = 'storage_app'
$env:STORAGE_DB_CONNECT_TIMEOUT = '3'
$env:STORAGE_DB_READ_TIMEOUT = '3'
$env:STORAGE_DB_WRITE_TIMEOUT = '3'

# 本机练习显式选择：必须 TLS 加密，但不校验服务器身份。
$env:STORAGE_DB_REQUIRE_VERIFIED_TLS = '0'
Remove-Item Env:\STORAGE_DB_TLS_CA_FILE -ErrorAction SilentlyContinue

# 密码在提示中输入，不写入代码、脚本或终端命令历史。
$dbPasswordInput = Read-Host '请输入 storage_app 的 MySQL 密码' -AsSecureString
try {
    $env:STORAGE_DB_PASSWORD = [System.Net.NetworkCredential]::new('', $dbPasswordInput).Password
    & .\build\mysql-scaffold\mysql_connection_probe.exe
} finally {
    Remove-Item Env:\STORAGE_DB_PASSWORD -ErrorAction SilentlyContinue
    $dbPasswordInput.Dispose()
}
```

成功输出：

```text
MySQL connection and ping succeeded.
```

MySQL 8.0.42 可能同时报告 `MYSQL_OPT_RECONNECT is deprecated`：本实现显式禁止自动重连，以避免事务丢失被隐藏，该提示不等于连接失败。

跨机器连接应使用默认的 `STORAGE_DB_REQUIRE_VERIFIED_TLS=1`，并设置 `STORAGE_DB_TLS_CA_FILE` 为可信 CA 的 PEM 文件路径，HOST 必须匹配证书中的服务器身份。`0` 仍强制 TLS，服务端不支持 TLS 时失败，不降级到明文。超时范围为 1～3600 秒，TLS 标志仅接受 `0`/`1`；未设置则默认 `1`。

## 接进现有服务端时

独立示例已经可运行；原 `Test.cpp` 服务启动、Makefile 和 Qt 客户端尚未接入此模块。

1. 在 `main()` 启动工作线程前调用 `mysql_library_init`，失败则退出。
2. 在 `service_module()` 所在线程创建配置和连接，调用 `open`，检查失败后再启动业务服务。`mysql_init` 会初始化调用线程所需的客户端线程状态。
3. 连接必须活到仓储和业务服务之后，同一连接不得跨线程共享；工作线程释放连接后调用 `mysql_thread_end`。
4. 主线程等待所有 MySQL 工作线程结束后，调用 `mysql_library_end`。
5. Linux 服务目标需编译数据库源文件并链接 Linux 的 MySQL 客户端库；Windows `.lib/.dll` 不能用于 Linux。服务端在 WSL/虚拟机内运行时，`127.0.0.1` 指向其自身环境，需确认连接地址和网络访问权限。

Qt 仍只通过 HTTP 请求服务端，不链接本模块、不保存数据库账号。下一步为 `UserRepository::findByAccount()` / `insert()` 的参数化 SQL，之后才是密码哈希验证、真实会话及 HTTP 接线。

原练习背景见 [18：MySQL 接入框架与挖空练习](../../../docs/project-learning/18-MySQL接入框架与挖空练习.md)。其中 DB01/DB03 的“未实现”描述为 2026-10-27 的历史状态，以当前源码为准。`schema.sql.template` 仍是挖空模板，不能直接导入；现有文件元数据不迁移。

参考：[MySQL 连接选项](https://dev.mysql.com/doc/c-api/8.0/en/mysql-options.html)、[mysql_real_connect](https://dev.mysql.com/doc/c-api/8.0/en/mysql-real-connect.html)、[线程生命周期](https://dev.mysql.com/doc/c-api/8.0/en/c-api-threaded-clients.html)。
