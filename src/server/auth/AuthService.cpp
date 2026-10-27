#include "AuthService.hpp"

namespace storage::auth {

// 只保存引用，不执行数据库查询或密码运算，不启动额外线程。
AuthService::AuthService(db::UserRepository &users) noexcept
    : users_(users)
{
}

// TODO DB05（挖空练习，当前不代写）：
// ① 限制输入长度；② UserRepository::findByAccount，区分故障与无记录；
// ③ 用已确定的密码哈希库验证编码 hash，不使用明文比较或普通 SHA256；
// ④ 创建安全随机 token，保存摘要/用户/过期时间，明确会话仓储/事务边界；
// ⑤ 成功后只返回公开用户字段、真实 token 与过期时间。
// 不存在用户和密码错误对外使用统一凭据错误；日志不记录密码、hash、token。
LoginResult AuthService::login(const std::string &account, const std::string &password)
{
    (void)account;
    (void)password;
    LoginResult result;
    result.message = "TODO DB05: verify credentials and create a real session";
    return result;
}

} // namespace storage::auth
