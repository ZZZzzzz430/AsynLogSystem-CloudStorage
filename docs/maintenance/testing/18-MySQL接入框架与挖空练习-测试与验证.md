# 18-MySQL接入框架与挖空练习：测试与验证

对应学习笔记：[18-MySQL接入框架与挖空练习](../../project-learning/18-MySQL接入框架与挖空练习.md)。

以下内容从原笔记按章节迁移，章节编号和历史状态保留。命令按正文指定的工作目录执行；未注明时以仓库根目录为准。整理目录不代表重新执行测试。

## 2. 本机检查记录

| 项目 | 实际结果 |
| --- | --- |
| mysql.exe | `C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe`，报告 8.0.42 |
| Windows 服务 | `MySQL80` 正在运行 |
| 开发头文件 | 安装目录 `include/mysql.h` 存在 |
| 开发库 | `lib/libmysql.lib` 与 `lib/libmysql.dll` 存在 |
| SDK 编译/链接/动态加载 | Qt 自带 MinGW 13.1 编译、链接本机 import library 并运行通过，probe 报告 8.0.42 |
| 实际连接、用户权限、数据库和表 | 未验证；SDK probe 不发连接或 SQL |
| 完整 Linux/libevent 服务构建 | 本次未执行；当前 WSL 枚举返回访问不可用，Windows SDK 骨架检查不代表 Linux 服务已通过 |

未询问或读取数据库密码。本次也不需要把 root 密码发送到聊天。

## 6. 本机编译命令

仓库根目录，PowerShell：

```powershell
$env:PATH = 'E:\Qt\Tools\mingw1310_64\bin;E:\Qt\Tools\Ninja;C:\Program Files\MySQL\MySQL Server 8.0\lib;C:\Program Files\MySQL\MySQL Server 8.0\bin;' + $env:PATH

& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' `
  -S src/server/database -B build/mysql-scaffold -G Ninja `
  '-DCMAKE_BUILD_TYPE=Debug' `
  '-DMYSQL_ROOT=C:/Program Files/MySQL/MySQL Server 8.0' `
  '-DCMAKE_CXX_COMPILER=E:/Qt/Tools/mingw1310_64/bin/g++.exe' `
  '-DCMAKE_MAKE_PROGRAM=E:/Qt/Tools/Ninja/ninja.exe'

& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/mysql-scaffold --parallel 4
& .\build\mysql-scaffold\mysql_sdk_probe.exe
```

预期只输出 SDK 8.0.42 和“未执行连接或 SQL”的说明。PATH 中 lib/bin 分别用于加载本机 MySQL DLL 及依赖；不向系统目录复制 DLL，也不持久修改全局 PATH。

Linux 开发环境安装匹配的 MySQL C API 开发依赖后，可从 `src/server` 执行 `make mysql-scaffold`；它只构建独立骨架。头文件/库不在默认路径时通过 `MYSQL_SCAFFOLD_CMAKE_ARGS` 提供 CMake 位置参数。Linux 要用 Linux 的客户端库，不能链接 Windows `.lib`。当前默认 `make test` 不自动链接这个骨架。

[MySQL 官方构建说明](https://dev.mysql.com/doc/c-api/8.0/en/c-api-building-clients.html)说明 C API 的头文件、库和平台链接依赖；本次实际使用本机动态库 import library，而不是把 MSVC 静态库混进 Qt MinGW。

## 7. 本次已验证与未验证

- 数据库独立静态库和 sdk_probe 已编译、链接并加载真实 MySQL 客户端库。
- 额外临时检查确认：未连接/未实现时不认证，失败查询清空旧身份，结果没有 token，连接对象不能复制/移动，空连接重复 close 安全。
- Qt Client 重新编译链接通过，没有给它增加 MySQL/QtSql 依赖。
- 临时检查源/产物在 `build/mysql-scaffold/`，未加入默认业务目标。检查的是占位阶段的失败边界，不是 SQL、认证或真实数据库连接验收。
- 未运行任何 SQL 模板，未读写数据库，没有验证真实密码/权限/表结构/网络连接，没有验证完整 Linux 服务。
- 保留原有用户和前次 UI 修改，不清理已有构建产物或 `.vscode` 设置。

当前完成度：**MySQL SDK 接线和练习框架完成，DB01～DB06 的核心实现与真实验收仍待用户完成。**
