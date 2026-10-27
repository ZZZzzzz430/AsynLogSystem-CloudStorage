# 18：MySQL 接入框架与挖空练习

日期：2026-10-27。用户已选择 MySQL，并要求延续“详细中文注释、只给框架、核心自己补写”。本篇取代旧方案中的 SQLite 技术选型，不把数据库安装或 SDK 编译通过写成业务接入完成。

## 1. 当前接入边界

```mermaid
flowchart LR
    A[Qt 登录页] --> B[Qt Network / HTTP]
    B -. DB06 待接 .-> C[libevent 认证接口]
    C -. DB06 待接 .-> D[AuthService]
    D --> E[UserRepository]
    E --> F[MySqlConnection]
    F -. DB03 待实现 .-> G[MySQL 8]
```

数据库属于服务端。Qt 不直接连接 MySQL，不链接 QtSql 或 libmysql，也不保存数据库账号密码。云盘用户密码与服务端连数据库的密码是两类数据。

本次只添加可编译的连接/仓储/认证骨架、SQL 挖空模板、配置变量约定和可选构建目标。没有连接数据库，没有执行 SQL、建库建表、创建账号、授权、插入测试用户、修改认证插件或访问已保存密码。原文件元数据仍使用 `storage.data`，不自动迁移或删除。

真实 `/api/v1/auth/login` **尚未挂载**；Service/Test 只添加未来接线位置的注释，Qt 登录保持失败占位。仅安装 MySQL 不会使登录按钮放行。

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

## 3. 已建立的文件

| 文件 | 责任及留白 |
| --- | --- |
| [DbStatus.hpp](../../src/server/database/DbStatus.hpp) | 区分未实现、未连接、无记录、唯一键冲突和数据库故障，默认失败 |
| [MySqlConnection.hpp](../../src/server/database/MySqlConnection.hpp) / [.cpp](../../src/server/database/MySqlConnection.cpp) | 配置对象、连接所有权、open/ping 挖空；close/析构是已给出的生命周期接线 |
| [UserRepository.hpp](../../src/server/database/UserRepository.hpp) / [.cpp](../../src/server/database/UserRepository.cpp) | 用户记录、参数化查询/插入接口；未填 SQL 与绑定实现 |
| [AuthService.hpp](../../src/server/auth/AuthService.hpp) / [.cpp](../../src/server/auth/AuthService.cpp) | 认证结果、仓储引用、登录步骤提示；不验证密码、不生成假 token |
| [mysql.env.example](../../src/server/database/mysql.env.example) | 服务端环境变量约定，真实账号/密码留空，不自动读取 |
| [schema.sql.template](../../src/server/database/schema.sql.template) | users/sessions 建表挖空，故意含 `{{...}}`，不能直接导入 |
| [database/CMakeLists.txt](../../src/server/database/CMakeLists.txt) | 独立 `storage_mysql_scaffold` 库及 SDK probe，不改变 Qt 依赖 |
| [sdk_probe.cpp](../../src/server/database/sdk_probe.cpp) | 仅打印 C API 客户端库版本，不连库 |
| [Makefile](../../src/server/Makefile) | 新增可选 `mysql-scaffold` 目标，原 test/gdb_test 依赖不变 |
| [Test.cpp](../../src/server/Test.cpp)、[Service.hpp](../../src/server/Service.hpp) | DB06 生命周期与 HTTP 路由接线注释，当前不执行 |
| [Client/ApiClient.cpp](../../Client/ApiClient.cpp) | 标注未来仅通过 HTTP 访问认证，不在客户端接数据库 |

数据库目录的 `.gitignore` 排除 `.env`、`mysql.local.env` 和 `*.local.env`。示例文件不填秘密；当前没有 dotenv 自动加载器，复制文件不会让程序自动连库。

## 4. 先理解对象寿命

未来在服务端同一执行线程内：

```text
配置验证完成
  ↓
MySqlConnection open 成功
  ↓
UserRepository 借用 connection
  ↓
AuthService 借用 repository
  ↓
Service / 请求上下文借用 AuthService
  ↓
事件循环结束
  ↓
逆序析构，最后 mysql_close
```

- 连接禁止复制/移动，避免双重关闭及仓储引用悬空。`nativeHandle()` 只借用，不转移所有权。
- `handle_` 只能为 null，或完全连接成功后发布的句柄；mysql_init 成功但 real_connect 失败的临时句柄不能留在成员中。
- 构造函数不主动连库。析构只释放资源，不抛异常、不自动重连。
- UserRepository 不解析 HTTP、验证密码或生成会话；AuthService 不调用 Qt 控件或直接操作 evhttp_request。
- 现有服务在 `service_module()` 的线程中运行。不要在 main 创建一个连接后任意跨线程共享；按 C API 文档明确进程初始化、线程使用与清理。
- 同步 MySQL 查询与密码哈希可能阻塞 libevent 线程。本阶段先学习正确性，不能把日志有后台线程描述成数据库已经异步；后续异步化需要连接归属、有界执行器及回事件线程回复等设计。

[MySQL 官方线程说明](https://dev.mysql.com/doc/c-api/8.0/en/c-api-threaded-clients.html)说明多线程客户端的初始化与连接使用约束；不能简单给同一个 MYSQL 指针加一处锁就假定所有结果读取也安全。

## 5. 六项挖空任务

### DB01：配置读取——建议这次先完成此项

入口：`loadMySqlConfigFromEnvironment(MySqlConfig &out)`。

你要补写：

1. 按 `mysql.env.example` 的名字读取环境变量，不自动寻找其他应用保存的配置或密码。
2. 校验数据库名和专用账号；端口严格在 1～65535，超时为合理正整数，拒绝多余字符与溢出。
3. 明确 `REQUIRE_VERIFIED_TLS` 的取值与 CA 策略，不能遇到连接失败就自动关闭验证。仅本机练习若选择不同传输策略，也须显式配置并解释。
4. 在局部临时配置中完成全部校验，成功才更新 out 并返回 Ok；失败清空输出凭据，返回 InvalidConfig。
5. 错误信息只说哪个配置不合格，不输出 password 或完整连接串。

当前框架只有函数签名、输出清空和 NotImplemented 返回；没有给完整解析代码。

正常验收：对专用练习配置能正确取到 host/port/database/user，**不打印 password**。故障验收：缺必填项、端口 0/65536/abc/3306xyz、超时溢出、无效 TLS 值均失败。此项不需要连库。

### DB02：补齐 SQL 模板

入口：`schema.sql.template`，先另存为完成后的 SQL，再自行在专用开发库验收；模板本身不能直接运行。

明确 users 的主键、用户名、可选邮箱、密码哈希和创建时间；sessions 的 token 摘要、用户外键、过期时间及索引。`UserRecord::id` 约定为 `u_` 加 32 位 hex；库表类型、字符集和外键列比较规则必须一致。不要一边使用数值自增主键，一边仍对 API 返回 u_ 字符串却没有映射规则。

决定用户名大小写、邮箱规范化和唯一性策略。建议先只支持用户名登录；若未来允许“用户名或邮箱”，须避免某用户的用户名等于另一用户的邮箱，不能用 `OR ... LIMIT 1` 随机选中一个身份。

使用完整 UTF-8 字符集与事务型引擎，数据库唯一约束负责最终冲突判定。不修改系统 `mysql.user`，不存明文密码，不用 MySQL 的普通摘要函数代替口令哈希。业务进程不以 root 自动建表；专用应用账号和迁移权限由你在本机明确管理，本次没有执行授权。

正常验收：表、唯一键和外键符合你的模型。故障验收：重复用户名/邮箱和不存在的会话用户被约束拦截；确认外键两侧的类型和排序规则一致。

### DB03：真正连接和探活

入口：`MySqlConnection::open()`、`ping()`；`close()`/析构已经接好。

补写顺序提示：配置检查 → mysql_init 的局部句柄 → timeout/TLS/字符集选项 → mysql_real_connect → 检查所有返回值 → 发布成员句柄。失败应关闭临时句柄，成员保持 null。不要在类成员保存连接密码，不把连接成功当成用户认证成功，不自动重连掩盖事务丢失。

参考 [mysql_real_connect](https://dev.mysql.com/doc/c-api/8.0/en/mysql-real-connect.html) 与 [mysql_close](https://dev.mysql.com/doc/c-api/8.0/en/mysql-close.html)。当前代码中没有这套连接实现，只给了步骤和失败返回。

正常验收：使用你自己的专用开发账号连接并完成真实探活，再关闭两次仍安全。故障验收：错密码、错误端口、库不存在、权限不足、TLS 不符合要求和服务停止，都不能留下“已连接”成员或泄露凭据。未连接时 ping 返回 NotConnected。

### DB04：参数化用户查询和插入

入口：`UserRepository::findByAccount()`、`insert()`。

只补 SQL、MYSQL_STMT 生命周期、输入绑定、执行、输出绑定、fetch、长度/NULL/截断检查与错误分类。`findByAccount` 在失败前清空 out，不能使用上次成功查询遗留的身份。查询不到是 NotFound，库不可用是 DatabaseError/NotConnected，不能混为一谈。插入重复键返回 Conflict。

依据 [mysql_stmt_prepare](https://dev.mysql.com/doc/c-api/8.0/en/mysql-stmt-prepare.html) 和 [MYSQL_BIND 数据结构](https://dev.mysql.com/doc/c-api/8.0/en/c-api-prepared-statement-data-structures.html)，输入值用参数绑定，不拼接账号字符串；缓冲区长度、输出截断与资源释放也必须处理。

验收：存在/不存在用户、中文或受支持账号、超长字段、带引号输入、重复注册、数据库停服。包含引号的输入不能改变查询结构；不能只凭“占位函数没有执行 SQL”就声称已通过 SQL 注入测试。

### DB05：密码验证与真实会话

入口：`AuthService::login()`。补查用户 → 区分故障与无记录 → 验证口令哈希 → 生成安全随机 token → 持久化会话摘要与到期时间 → 返回公开身份和真实会话。

密码哈希采用既有计划的专用密码哈希库方案；[libsodium 密码存储接口](https://doc.libsodium.org/password_hashing/default_phf)提供生成与验证编码哈希的 API。当前没有安装或链接该库，更没有代写哈希与会话实现。会话仓储仍需你自行建立，AuthService 当前只保存 UserRepository 引用。

用户存在不代表密码正确；密码正确但创建会话失败也不能返回 Authenticated。错误账号与错密码对外用统一凭据错误；数据库故障返回服务不可用，不能伪装成错误密码。token、密码、hash 不写日志。

验收：有效账号密码且会话创建成功才能成功；错误账号/密码、数据库故障、哈希失败、会话插入失败都不能产生可用登录身份。默认 LoginResult 没有 userId/token。

### DB06：接进 libevent 与 Qt

入口注释：`Test.cpp::service_module()`、`Service.hpp::GenHandler()`、`Client/ApiClient.cpp::login()`。

你要完成对象组合、服务上下文寿命、精确路由、受限 JSON 解析和 HTTP/业务状态映射，再实现 Qt HTTP 请求与结果解析。拟用 `POST /api/v1/auth/login`，当前还不是已实现接口。不要在 GenHandler 的空 arg 上直接强转，也不要为演示回复假的 HTTP 200/token。

账户认证后还需传递会话、保护文件路由和验证文件归属；旧 `/upload`、`/download/...` 仍然无身份隔离，不能声称“加了登录就完成用户文件隔离”。这些属于之后的单独验收。

Qt 侧保持原有 gate：只有真实认证结果才发 `loginFinished(true, ...)`，触发已有 MainWindow 切换；数据库配置永远不进入客户端。

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

## 8. 这次交作业只需要什么

先完成 DB01，一次只补一个学习块：

1. 提交配置读取函数的差异，不提交真实环境变量或密码。
2. 给出合法/缺失/端口异常/超时异常的测试现象，只显示非秘密字段。
3. 解释：“MySQL80 运行、SDK probe 成功、open 成功、用户登录成功，这四件事有什么不同？”

后续 DB02～DB06 已给接口与验收方向，当前不要求一次写完，也不因框架能编译就标记核心练习完成。
