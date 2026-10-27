#include "UserRepository.hpp"

namespace storage::db {

// 仅保存借用关系，不主动连库，不在构造函数里执行查询或建表。
UserRepository::UserRepository(MySqlConnection &connection) noexcept
    : connection_(connection)
{
}

// TODO DB04（挖空练习）：
// ① 固定 SELECT 语句；② MYSQL_STMT 生命周期；③ bind 输入，不能拼接 account；
// ④ execute/bind_result/fetch，检查 NULL、长度、截断和无结果；
// ⑤ 全部字段检查成功才将临时 UserRecord 交给 out；所有路径释放 stmt/result。
// 建议先只支持用户名；若开放邮箱登录，要先解决用户名/邮箱跨列歧义。
DbStatus UserRepository::findByAccount(const std::string &account, UserRecord &out)
{
    out = UserRecord{};
    if (!connection_.isOpen()) return {DbCode::NotConnected, "MySQL connection is not open"};
    (void)account;
    return {DbCode::NotImplemented, "TODO DB04: bind parameters and fetch user record"};
}

// TODO DB04（挖空练习）：固定 INSERT、绑定字段、检查影响行数和唯一键错误。
// 不在此处接收明文密码、计算密码哈希、自动创建数据库或回退到文件存储。
DbStatus UserRepository::insert(const UserRecord &record)
{
    if (!connection_.isOpen()) return {DbCode::NotConnected, "MySQL connection is not open"};
    (void)record;
    return {DbCode::NotImplemented, "TODO DB04: insert a user with bound parameters"};
}

} // namespace storage::db
