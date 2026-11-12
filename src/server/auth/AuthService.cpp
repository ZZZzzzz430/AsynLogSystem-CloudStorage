#include "AuthService.hpp"
#include "Crypto.hpp"
#include <algorithm>
#include <ctime>
#include <regex>
#include <iostream>
namespace storage::auth {
namespace {
bool validToken(const std::string &token) {
    return token.size()==64 && std::all_of(token.begin(),token.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}

/**
 * @brief 校验用户名
 * 规则：4~16位，字母、数字、下划线、减号
 * @param username 待校验用户名
 * @return true 格式正确，false 错误
 */
bool checkUserName(const std::string& username)
{
    static const std::regex reg(R"(^[a-zA-Z0-9_-]{4,16}$)");
    return std::regex_match(username, reg);
}


// 密码：只检查长度（按字节），8~1024。
bool checkPassword(const std::string& pwd)
{
    return pwd.size() >= 8 && pwd.size() <= 1024;
}

// 邮箱：基本结构检查，不追求"完美正则"。
bool checkEmail(const std::string& email)
{
    // 1. 先看长度，再做别的事：超长输入最先被拒绝。
    if (email.size() < 3 || email.size() > 254) return false;

    // 2. 只接受可见 ASCII，拒绝空白和控制字符。
    for (unsigned char c : email)
        if (c <= 0x20 || c >= 0x7f) return false;

    // 3. 恰好一个 '@'，且前面不为空。
    const auto at = email.find('@');
    if (at == std::string::npos || at == 0 || at != email.rfind('@')) return false;

    const std::string local  = email.substr(0, at);
    const std::string domain = email.substr(at + 1);
    if (domain.size() < 3) return false;   // 至少 "a.b"，同时保证 domain 非空

    // 4. 两段都不能以 '.' 开头或结尾，也不能出现 ".."。
    auto badDots = [](const std::string& s) {
        return s.front() == '.' || s.back() == '.' || s.find("..") != std::string::npos;
    };
    if (badDots(local) || badDots(domain)) return false;

    // 5. 域名部分必须含 '.'。
    return domain.find('.') != std::string::npos;
}
}
LoginResult AuthService::login(const std::string &account,const std::string &password) {
    LoginResult result;result.code=AuthCode::InvalidCredentials;
    result.message="Invalid account or password";
    if(account.empty()||account.size()>256||password.empty()||password.size()>1024) return result;
    try {
        db::UserRecord user;
        auto status=users_.findByAccount(account,user);
        if(!status.ok()&&status.code!=db::DbCode::NotFound) {result.code=AuthCode::Unavailable;return result;}
        bool verified=verifyPassword(password,user.passwordHash);
        if(!status.ok()||!verified) return result;
        std::string token=randomHex(32);
        auto expires=std::time(nullptr)+8*60*60;
        status=sessions_.create(tokenHash(token),user.id,expires);
        if(!status.ok()) {result.code=AuthCode::Unavailable;return result;}
        std::tm utc{};gmtime_r(&expires,&utc);char formatted[32];
        std::strftime(formatted,sizeof(formatted),"%Y-%m-%dT%H:%M:%SZ",&utc);
        result.code=AuthCode::Authenticated;result.userId=user.id;result.username=user.username;
        result.token=std::move(token);result.expiresAtUtc=formatted;result.message.clear();
    } catch(...) {result={};result.code=AuthCode::Unavailable;}
    return result;
}
AuthCode AuthService::authenticate(const std::string &token,db::UserRecord &out) {
    out={};if(!validToken(token))return AuthCode::InvalidCredentials;
    try {
        auto s=sessions_.find(tokenHash(token),out);
        return s.ok()?AuthCode::Authenticated:s.code==db::DbCode::NotFound?AuthCode::InvalidCredentials:AuthCode::Unavailable;
    }catch(...){return AuthCode::Unavailable;}
}
AuthCode AuthService::logout(const std::string &token) {
    db::UserRecord user;auto code=authenticate(token,user);
    if(code!=AuthCode::Authenticated)return code;
    try {return sessions_.revoke(tokenHash(token)).ok()?AuthCode::Authenticated:AuthCode::Unavailable;}
    catch(...){return AuthCode::Unavailable;}
}
RegisterResult AuthService::registerUser(const std::string& username, const std::string& password, const std::string& email) {
    try{
        // 先校验
        if (!checkUserName(username) ||
            !checkPassword(password) ||
            (!email.empty() && !checkEmail(email))) {
            return {AuthCode::InvalidInput, {}, {}};
        }

        db::UserRecord user;
        user.id = "u_" + randomHex(16); // 生成用户ID
        user.username = username;
        user.passwordHash = hashPassword(password);
        user.email = email;

        // 提前准备返回字段，减少插入成功后可能抛异常的操作。
        RegisterResult result;
        result.userId = user.id;
        result.username = user.username;

        auto status = users_.insert(user);
        if (status.code == db::DbCode::Conflict) {
            return {AuthCode::Conflict, {}, {}};
        } else if (!status.ok()) {
            return {AuthCode::Unavailable, {}, {}};
        }
        result.code = AuthCode::Registered; // 仅在数据库明确插入成功后返回成功。
        return result;
    }catch (...) {
        return {AuthCode::Unavailable, {}, {}};
    }
}

} // namespace storage::auth
