# 服务端 MySQL 挖空骨架

本目录不是可用的认证服务。`open`、配置读取、SQL 查询/插入和认证核心都留为 TODO，默认失败，不创建数据库或用户。`schema.sql.template` 含有未填空项，不能直接导入。

按 [18：MySQL 接入框架与挖空练习](../../../docs/project-learning/18-MySQL接入框架与挖空练习.md) 的 DB01～DB06 顺序补写。已有中文注释说明每个函数、状态、对象所有权和错误处理边界。

- `MySqlConnection`：服务端连接生命周期；`close` 与析构接线已提供，连接本身待补。
- `UserRepository`：只处理用户数据，不保存明文密码，不拼接用户输入到 SQL。
- `../auth/AuthService`：认证与会话骨架；默认没有身份和 token。
- `mysql_sdk_probe`：只检查 SDK 的编译、链接和动态库加载，不连库、不执行 SQL。

Qt 客户端通过 HTTP 调用服务端，不链接本目录，也不接触数据库账号。
