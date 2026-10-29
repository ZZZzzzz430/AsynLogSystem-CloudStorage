#include "Statement.hpp"
#include "SessionRepository.hpp"
#include "../auth/AuthService.hpp"
#include "../auth/Crypto.hpp"
#include <mysql.h>
#include <iostream>
#include <stdexcept>
#include <ctime>
using namespace storage;
void check(bool ok,const char *name) {if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
void require(db::DbStatus status) {if(!status.ok())throw std::runtime_error(status.message);}
int main() {
    if(mysql_library_init(0,nullptr,nullptr))return 1;
    int result=1;
    try {
        db::MySqlConfig config;require(db::loadMySqlConfigFromEnvironment(config));
        check(config.database=="asynlog_storage_test","isolated test database guard");
        db::MySqlConnection c;require(c.open(config));require(c.ping());
        check(c.isOpen(),"C++ TCP connection and ping");
        std::vector<std::vector<std::string>> rows;
        require(db::sql(c,"SELECT 1, @@session.time_zone",{},&rows));check(rows[0][0]=="1"&&rows[0][1]=="+00:00","real SELECT and UTC");
        require(db::sql(c,"START TRANSACTION",{}));
        try {
            db::UserRepository users(c);db::SessionRepository sessions(c);auth::AuthService service(users,sessions);
            auto account="test_"+auth::randomHex(8),password=auth::randomHex(16);
            auto hash=auth::hashPassword(password);
            check(auth::verifyPassword(password,hash)&&!auth::verifyPassword("wrong",hash),"scrypt verification");
            db::UserRecord original{"u_"+auth::randomHex(16),account,"",hash},found;
            require(users.insert(original));require(users.findByAccount(account,found));
            check(found.id==original.id&&found.passwordHash==hash,"parameterized INSERT and SELECT");
            check(users.insert(original).code==db::DbCode::Conflict,"unique constraint conflict");
            require(db::sql(c,"UPDATE users SET email=? WHERE id=?",{"test@example.invalid",original.id}));
            require(users.findByAccount(account,found));check(found.email=="test@example.invalid","UPDATE round trip");
            check(users.findByAccount("' OR 1=1 --",found).code==db::DbCode::NotFound&&found.id.empty(),"SQL injection rejected and output cleared");
            auto login=service.login(account,password);check(login.code==auth::AuthCode::Authenticated&&login.token.size()==64,"correct login and persistent session INSERT");
            check(service.login(account,"wrong").code==auth::AuthCode::InvalidCredentials,"wrong password rejected");
            check(service.login("missing_"+account,password).code==auth::AuthCode::InvalidCredentials,"missing user rejected");
            check(service.authenticate(login.token,found)==auth::AuthCode::Authenticated&&found.id==original.id,"bearer verification");
            check(service.authenticate("",found)==auth::AuthCode::InvalidCredentials,"missing token rejected");
            check(service.authenticate(std::string(64,'0'),found)==auth::AuthCode::InvalidCredentials,"forged token rejected");
            auto expired=auth::randomHex(32);require(sessions.create(auth::tokenHash(expired),original.id,std::time(nullptr)-1));
            check(service.authenticate(expired,found)==auth::AuthCode::InvalidCredentials,"expired token rejected");
            require(db::sql(c,"SELECT token_hash FROM sessions WHERE token_hash=?",{auth::tokenHash(login.token)},&rows));
            check(rows.size()==1&&rows[0][0]!=login.token,"database stores token digest only");
            check(service.logout(login.token)==auth::AuthCode::Authenticated&&service.authenticate(login.token,found)==auth::AuthCode::InvalidCredentials,"logout invalidates token");
            require(db::sql(c,"DELETE FROM users WHERE id=?",{original.id}));
            check(users.findByAccount(account,found).code==db::DbCode::NotFound,"DELETE and foreign key cascade");
            require(users.insert(original));require(db::sql(c,"ROLLBACK",{}));
            check(users.findByAccount(account,found).code==db::DbCode::NotFound,"transaction rollback leaves no test data");
            auto failed=db::sql(c,"SELECT missing_column FROM users",{},&rows);
            check(!failed.ok()&&c.ping().ok(),"query failure handled without crash");
        } catch(...) {auto ignored=db::sql(c,"ROLLBACK",{});(void)ignored;throw;}
        auto bad=config;bad.password=auth::randomHex(24);db::MySqlConnection wrong;
        auto s=wrong.open(bad);check(!s.ok()&&!wrong.isOpen(),"wrong database password rejected");std::cout<<s.message<<'\n';
        bad=config;bad.database="asynlog_missing_"+auth::randomHex(8);s=wrong.open(bad);
        check(!s.ok()&&!wrong.isOpen(),"wrong database name rejected");std::cout<<s.message<<'\n';
        bad=config;bad.port=1;bad.connectTimeoutSeconds=1;s=wrong.open(bad);
        check(!s.ok()&&!wrong.isOpen(),"unreachable service rejected");std::cout<<s.message<<'\n';
        c.close();c.close();check(!c.ping().ok(),"idempotent close and disconnected query handling");
        result=0;
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';}
    mysql_library_end();return result;
}
