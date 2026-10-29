#pragma once
#include "../database/UserRepository.hpp"
#include "../database/SessionRepository.hpp"
#include <string>
namespace storage::auth {
enum class AuthCode { Authenticated, InvalidCredentials, Unavailable, NotImplemented };
struct LoginResult {
    AuthCode code = AuthCode::NotImplemented;
    std::string userId, username, token, expiresAtUtc, message;
};
class AuthService {
public:
    AuthService(db::UserRepository &users, db::SessionRepository &sessions) noexcept
        : users_(users), sessions_(sessions) {}
    LoginResult login(const std::string &account, const std::string &password);
    AuthCode authenticate(const std::string &token, db::UserRecord &out);
    AuthCode logout(const std::string &token);
private:
    db::UserRepository &users_;
    db::SessionRepository &sessions_;
};
}
