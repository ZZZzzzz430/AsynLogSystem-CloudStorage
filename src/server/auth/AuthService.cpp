#include "AuthService.hpp"
#include "Crypto.hpp"
#include <algorithm>
#include <ctime>
namespace storage::auth {
namespace {
bool validToken(const std::string &token) {
    return token.size()==64 && std::all_of(token.begin(),token.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
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
}
