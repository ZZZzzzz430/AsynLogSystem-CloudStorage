# 20：真实 MySQL 与持久化登录验收

日期：2026-10-28。本次由用户明确授权实现和测试，使用本地 Git 最新源码，不依据旧 ZIP。现状、已运行测试与尚待验收分别记录。

## 修改前现状

- 分支 `main...origin/main`，提交 `66aaadf`；唯一未跟踪文件 `a`，原样保留。
- 启动链：`src/server/Test.cpp main → service_module → Service::RunModule → libevent GenHandler`。路由是旧列表/上传/下载。
- Qt：`Client/main.cpp → LoginWindow::onLoginClicked → loginRequested → ApiClient::login → loginFinished → handleLoginResult → authenticationSucceeded → MainWindow`。网络登录是返回失败的占位，主窗口原有单独 ApiClient，尚无 token。
- MySQL：MySqlConnection 已实现 C API TCP/TLS 配置和 ping；UserRepository/AuthService 为 TODO，没有 SessionRepository，没有实际密码哈希方案，schema.sql.template 不可导入。
- 原 Makefile 不编译数据库；database/CMakeLists.txt 是独立练习目标。
- 本机最初缺少 C API SDK，Connector/C++ 1.1.12 已安装但不是当前代码的 API；安装 libmysqlclient-dev 后已能编译。
- 仓库没有 `second_hand_platform` 引用；其 sys_user 结构与预留 users 结构不同，无证据表明属于本项目。选择独立 `asynlog_storage`，不修改二手交易库和项目。

## 这次实现

1. 数据库：新增 [正式迁移](../../src/server/database/migrations/001_auth.sql)，users 与 sessions；UTC 时间，用户外键与到期索引。不删除已有数据。单独测试库 asynlog_storage_test。
2. SQL：新增 Statement.hpp，用 MYSQL_STMT 的 prepare/bind/execute/fetch，所有外部值绑定；NULL/截断检查、异常状态和 RAII；UserRepository 查询/插入实现。
3. 密码：OpenSSL scrypt，N=131072/r=8/p=1，16 字节随机盐、32 字节派生键；带参数编码，CRYPTO_memcmp 比较。不识别明文密码，未迁移其他项目密码。不存在用户也做派生。
4. 会话：32 字节随机 token；仅数据库保存 SHA-256 摘要。8 小时有效期，创建/查询/删除由 SessionRepository 实现，每次验证查库，重启无需恢复内存缓存。
5. HTTP：原 Service 注入 AuthService，复用 libevent；POST login、GET me、POST logout。登录返回 user/token/expiresAt。401 凭据失败，503 数据库或认证不可用，400 JSON 错误，405 方法错误。限制登录 JSON 4096 字节；全局请求体上限沿用上传 1MiB。
6. Qt：ApiClient 发真实 JSON；内存保存 token/期限/服务源地址；同源请求自动 Bearer，禁止重定向转发凭据。登录页与主窗口共享 ApiClient，主窗口创建时验证 me；注销成功清除会话并关闭窗口。界面改为用户名登录，邮箱登录未开放。
7. 启动：MySQL 库初始化先于线程，连接/仓储/认证/HTTP 在同一服务线程构造并逆序析构；MySQL 线程清理在连接释放后。启动失败给出非零退出与脱敏错误。
8. 运维：setup_local.py 只在本机隐藏输入管理员密码，创建专用账号、独立数据库和初始用户；runtime 仅 users SELECT、sessions SELECT/INSERT/DELETE。凭据文件为 build/local-auth/*.json（0600，目录0700，Git忽略）。run_local.py 不输出凭据地加载环境。

## 文件范围

- 服务接线：Service.hpp、Test.cpp、Makefile。
- 数据库：MySqlConnection.cpp、UserRepository.hpp/.cpp、CMakeLists.txt、README.md；新增 Statement.hpp、SessionRepository.hpp/.cpp、migrations/001_auth.sql。
- 认证：AuthService.hpp/.cpp；新增 Crypto.hpp/.cpp、AuthHttp.hpp。
- Qt：ApiClient.hpp/.cpp、loginwindow.h/.cpp、mainwindow.h/.cpp、main.cpp、CMakeLists.txt；新增 tests/auth_smoke.cpp。
- 初始化与测试：create_user.cpp、setup_local.py、run_local.py、integration_tests.cpp、http_fixture.cpp、crypto_tests.cpp、tests/auth_http_test.py。

## 已执行的构建与离线测试

仓库根目录：

```bash
cmake -S src/server/database -B build/mysql -DBUILD_TESTING=ON
cmake --build build/mysql --parallel 4
ctest --test-dir build/mysql --output-on-failure
make -C src/server test
cmake -S Client -B build/client -DBUILD_TESTING=ON
cmake --build build/client --parallel 4
```

实际结果：数据库工具、Linux 原服务端、Qt Client 与 client_auth_smoke 均成功构建。Qt 第一轮的 QUrl 空赋值重载歧义已修复。CTest：

```text
1/2 Test #1: mysql_configuration_and_state .... Passed
2/2 Test #2: password_hash_and_token_crypto ... Passed
100% tests passed, 0 tests failed out of 2
```

密码测试包含用 Python hashlib.scrypt 独立产生的已知向量、错误密码、拒绝明文编码、随机盐与摘要已知向量。

## 真实数据库和 HTTP 测试

初始化尚需用户在本机终端提供管理员凭据。已存在 login-path 不可用，实际返回 1045（未使用密码）。不猜测其他项目的管理员密码。

```bash
python3 src/server/database/setup_local.py
python3 src/server/database/run_local.py runtime build/mysql/mysql_connection_probe
python3 src/server/database/run_local.py test build/mysql/mysql_integration_tests
python3 src/server/tests/auth_http_test.py
```

状态：**等待本机初始化，不算通过**。初始化后更新本节实际输出。

验收脚本范围：C++ TCP/ping、SELECT、参数化 CRUD/唯一键/注入、回滚、密码验证、token 摘要、会话过期/注销、错误密码/库名/端口、失败不崩溃；HTTP 真服务进程重启后验证 token、401/400/405、日志无凭据；Qt Network 使用相同 ApiClient 访问真服务。

## Windows 构建、启动和手工验收（未执行）

1. 将修改后的 Client 源码同步到 Windows。Qt Creator 打开 `Client/CMakeLists.txt`，选择已安装的 Qt6 MinGW 64-bit Kit，配置/构建/运行 Client。Qt 只依赖 Widgets、Network，不安装或链接 MySQL。
2. Linux 项目根目录完成上述初始化/构建，之后：

   ```bash
   cd src/server
   python3 database/run_local.py runtime ./test
   ```

3. Windows 登录页填写 `http://<Linux局域网IP>:8081`，不能填写 Windows 自身的 127.0.0.1。Linux MySQL 保持 127.0.0.1:3306，网络只需要 Windows 能访问服务 HTTP 端口。
4. 点“测试连接”验证 `/health`，应提示可达；此操作不应打开主窗口。
5. 用初始化时自行设置的账号和错误密码登录，应显示账号或密码错误，不打开主窗口；不存在用户同样失败。
6. 用正确账号密码登录，应进入主窗口；状态栏显示“会话验证成功”，对应自动带 Bearer 的 GET /api/v1/auth/me。
7. 保持客户端不退出，重启 Linux 服务端，再次触发会话验证（测试工具或重新调用 verifySession）；token 在原8小时内应有效。客户端进程重启后需要重新登录，本次没有实现“记住登录”。
8. 菜单“注销”使服务端删除会话，再关闭主窗口。可用 Linux 自动 HTTP 测试验证同 token 随后401；不要在终端/截图中公开 token。
9. 通过测试库中的过期 fixture 验证到期401，不修改业务用户数据。Windows 可在相同 Qt Kit 构建 `client_auth_smoke`，从本地环境提供隔离测试账号后运行 `client_auth_smoke.exe http://<LinuxIP>:8081`；不要把测试密码放进命令行参数。

**Windows 编译、界面运行和跨机器端到端尚未执行，不算通过。Linux Qt Network 测试也不能代替 Windows 验收。**

## 线程与边界

现有 HTTP 是一个 libevent 事件循环，所有数据库访问串行，连接不会跨线程共享。密码派生与 SQL 都在回调中同步执行，慢登录会阻塞其他 HTTP 请求；后续应以限流和认证工作队列优化，不能直接让多个工作线程共享当前连接。

现有文件元数据仍为全局 storage.data；本次给旧入口加认证，未实现用户文件所有权。Qt 上传/下载/文件列表 TODO 不属于本次登录目标的完成项。HTTP 是开发环境明文入口，跨不可信网络部署需 HTTPS。

数据库过期会话不会自动清理，但每次使用都会拒绝；可定期按 expires_at 删除。未输出密码、完整 token 或数据库密码；凭据仅本地环境与被忽略的受限权限文件保存。

实现依据：[MySQL C API 预处理语句](https://dev.mysql.com/doc/c-api/8.0/en/c-api-prepared-statement-interface-usage.html)、[OpenSSL scrypt](https://docs.openssl.org/3.0/man7/EVP_KDF-SCRYPT/)、[RAND_bytes](https://docs.openssl.org/3.0/man3/RAND_bytes/)。

## 本机初始化反馈后的复查

用户报告已经执行初始化。实际复查 `build/local-auth/` 目录存在但为空，没有 runtime.json、test.json、http-test.json，因此还不能加载专用账号执行真实测试；“已执行”暂不能记为“初始化成功”。下一步先取得初始化脚本的最后几行输出，定位中途退出原因，再继续验收，不盲目重复建库。

经用户授权检查并删除根目录未跟踪文件 `a`：内容为旧 Git 提交编辑器提示与误输入 `:q`，不属于项目源码或配置。其余工作区修改保留；`git diff --check` 通过。

## 管理员凭据只读验证

用户授权提供管理员凭据后，分别在沙箱外执行 MySQL CLI TCP 检查与项目 `build/mysql/mysql_connection_probe`（临时进程环境传入凭据，不落盘保存）。地址为 127.0.0.1:3306；C++ 探针暂选只读系统元数据库 information_schema，以免猜测业务库名。

实际结果：CLI 返回 `ERROR 1045 (28000): Access denied for user 'root'@'localhost' (using password: YES)`；C++ 返回 `Connection: mysql_real_connect failed (MySQL error 1045)`，退出码均为1。说明已接触到 MySQL 并被认证拒绝，尚未验证业务库/表；不把它当成用户仓储 SQL 错误或缺表。未修改数据库，也未将 root 配为日常账号。

## 用户本机 TCP 登录成功反馈

用户提供 `mysql -h 127.0.0.1 -P 3306 -u root -p` 成功输出，服务器为 MySQL 8.0.46，连接 ID 14。这证明用户本机 TCP 与交互输入的 root 凭据可用；不等于此前助手提供凭据的1045已解决，也不等于C++连接或用户查询已通过。下一步只读查询 information_schema.tables 确认 users 所在数据库，再检查字段；不重复建库、不修改用户表，不要求用户公开密码。
