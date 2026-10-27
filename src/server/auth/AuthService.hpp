#pragma once

#include "../database/UserRepository.hpp"

#include <string>

namespace storage::auth {

enum class AuthCode { Authenticated, InvalidCredentials, Unavailable, NotImplemented };

// 只承载认证对外需要的数据，不携带 passwordHash 或数据库账号密码。
struct LoginResult {
    AuthCode code = AuthCode::NotImplemented; // 默认失败，不从 DbStatus::Ok 推导登录成功。
    std::string userId;
    std::string username;
    std::string token; // TODO DB05：真实安全随机 token；当前必须为空。
    std::string expiresAtUtc;
    std::string message;
};

// 认证业务层不依赖 Qt 或 libevent；HTTP 解析和 JSON 响应属于未来适配层。
class AuthService {
public:
    // 不拥有仓储：数据库 → 仓储 → 认证服务按此顺序构造，逆序析构。
    explicit AuthService(db::UserRepository &users) noexcept;

    // TODO DB05：查用户、验证密码、建立真实会话，全部完成后才返回 Authenticated。
    // 仅“数据库能连接”或“用户存在”都不代表密码正确，更不代表会话已建立。
    [[nodiscard]] LoginResult login(const std::string &account, const std::string &password);

private:
    db::UserRepository &users_;
};

} // namespace storage::auth
