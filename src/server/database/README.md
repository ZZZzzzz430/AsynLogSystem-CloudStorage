# Linux MySQL 与真实认证

当前服务端使用 **MySQL C API**，不是 Connector/C++。`libmysqlcppconn-dev` 无法替代 C API SDK；CMake 直接找头文件/库，不依赖 `mysql_config`。Qt 只通过 HTTP 连接服务端，不接触数据库凭据。

## 构建与初始化

在仓库根目录：

```bash
sudo apt install -y libmysqlclient-dev libssl-dev
cmake -S src/server/database -B build/mysql -DBUILD_TESTING=ON
cmake --build build/mysql --parallel 4
ctest --test-dir build/mysql --output-on-failure
make -C src/server test
cmake -S Client -B build/client -DBUILD_TESTING=ON
cmake --build build/client --parallel 4
```

`mysql_configuration_and_state` 与 `password_hash_and_token_crypto` 不连接数据库。真实连接及 SQL 验证必须执行后面的命令。

在**本机交互终端**执行：

```bash
python3 src/server/database/setup_local.py
```

隐藏输入 MySQL 管理员密码和新的云盘用户密码（至少 12 字节）。脚本应用 [001_auth.sql](migrations/001_auth.sql)，创建独立的 `asynlog_storage` 与 `asynlog_storage_test`；不访问 `second_hand_platform`。不创建默认密码。初始用户通过 C++ `storage_create_user` 工具进行 scrypt 哈希及参数化插入。

账号随机命名并限制来源为 `127.0.0.1`：

- 业务账号：仅 `users SELECT`、`sessions SELECT/INSERT/DELETE`。
- 隔离 HTTP 测试账号：同样最小权限，仅作用于测试库。
- 测试数据账号：仅测试库 SELECT/INSERT/UPDATE/DELETE，不可访问业务库或执行 DDL。
- 管理员只做迁移和一次性用户初始化，不作为 HTTP 服务账号，也不保存管理员密码。

本地运行配置保存在 `build/local-auth/*.json`（目录 0700，文件 0600，Git 已忽略）。不要复制到 Client、提交到 Git 或公开输出文件内容。初始化脚本拒绝覆盖已有配置；若运行部分失败，已经创建的库/账号会保留，需管理员检查后再恢复，不自动删除数据。

现有 `schema.sql.template` 是历史挖空练习，不要导入；正式脚本是 migrations/001_auth.sql。迁移新增会话表是为了保存 token 摘要、用户、创建与过期时间，让重启后验证和注销生效；内存 map 无法满足这一点。`IF NOT EXISTS` 不会修复不同的已有表结构，迁移到已有同名库前应检查结构。

## 实际测试与启动

仓库根目录：

```bash
python3 src/server/database/run_local.py runtime build/mysql/mysql_connection_probe
python3 src/server/database/run_local.py test build/mysql/mysql_integration_tests
python3 src/server/tests/auth_http_test.py
```

第二项必须使用 `asynlog_storage_test`；事务内生成随机用户并测试 CRUD、认证、token 摘要、过期、注销、回滚，以及错误密码、库名和不可达端口。测试不会停止系统 MySQL。

第三项运行**原项目服务端**，用临时工作目录和随机 HTTP 端口，真实登录后终止/重启服务，验证持久化 token，然后验证过期和注销；fixture 只在测试库创建带随机前缀的用户并在 finally 中删除。若已构建 `client_auth_smoke`，同时调用真实 Qt ApiClient，验证登录、自动 Bearer、注销。测试不会替换业务 `Storage.conf`、文件索引和数据目录。

启动日常服务：

```bash
cd src/server
python3 database/run_local.py runtime ./test
```

HTTP 使用 Storage.conf 的 8081；MySQL 保持 `127.0.0.1:3306`。Windows 只需访问 Linux HTTP 端口，不开放 3306。

也可以直接设置 `mysql.env.example` 所示 STORAGE_DB_* 环境变量，不强制使用本地 JSON。`STORAGE_DB_REQUIRE_VERIFIED_TLS=0` 仍要求数据库 TLS；默认值 1 还校验证书身份，需要 CA。专用账号不得用 root 替代。数据库故障只报告操作与数字错误码，不输出原始错误消息或连接串。

## 实现边界

- `MySqlConnection`：C API RAII 连接、超时、utf8mb4、UTC、禁用自动重连；所有仓储和认证在同一个 libevent 线程使用连接。库初始化先于线程启动，连接析构后清理线程状态。
- `Statement.hpp`：预处理 SQL、参数绑定、结果长度/NULL 检查；stmt RAII 释放结果和句柄，不拼接外部参数。
- 用户名登录大小写敏感，不使用邮箱 OR 查询。用户 ID 为安全随机 `u_` + 32 hex。
- scrypt 编码固定为 `scrypt$131072$8$1$<16-byte salt hex>$<32-byte key hex>`；不支持明文、旧普通摘要或任意参数编码。比较使用 CRYPTO_memcmp；不存在用户也执行一次派生。
- token 为 RAND_bytes 生成的 32 字节随机数，以 64 hex 传输；数据库保存其 SHA-256 摘要。有效期 8 小时，UTC，服务端每次认证都查询数据库；注销删除会话。没有把 SHA-256 用来存用户密码。
- Qt token 仅保存在 ApiClient 内存，登录页与主窗口共享；不持久化到本地明文文件。只向同源地址附加 token，不跟随重定向转发凭据；401 清理本地会话，注销网络失败保留 token 供重试。
- 文件列表/上传下载的 Qt TODO 仍在，未声称实现用户文件隔离；现有文件元数据为全局存储。旧服务路由需要登录，不能据此当成多租户云盘上线。
- 当前 scrypt 和 SQL 同步运行在 libevent 回调线程，密码派生会阻塞该事件循环；是当前最小接线的性能边界，后续应配合限流与独立认证工作队列。HTTP 为原项目明文开发服务，跨不可信网络需使用 HTTPS 入口。
- 过期会话查询会拒绝，但不会自动删除；管理员可定期执行 `DELETE FROM sessions WHERE expires_at <= UTC_TIMESTAMP()`。

参考：[MySQL C API 预处理语句](https://dev.mysql.com/doc/c-api/8.0/en/c-api-prepared-statement-interface-usage.html)、[OpenSSL scrypt](https://docs.openssl.org/3.0/man7/EVP_KDF-SCRYPT/)、[RAND_bytes](https://docs.openssl.org/3.0/man3/RAND_bytes/)。
