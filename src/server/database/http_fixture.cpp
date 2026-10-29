#include "Statement.hpp"
#include "UserRepository.hpp"
#include "../auth/Crypto.hpp"
#include <mysql.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace storage;
std::string env(const char *name) {auto *p=std::getenv(name);return p?p:"";}
int main(int argc,char **argv) {
    if(argc!=2||mysql_library_init(0,nullptr,nullptr))return 2;
    int result=1;
    try {
        db::MySqlConfig config;auto s=db::loadMySqlConfigFromEnvironment(config);
        if(!s.ok()||config.database!="asynlog_storage_test")throw std::runtime_error("Isolated test database required");
        db::MySqlConnection c;s=c.open(config);if(!s.ok())throw std::runtime_error(s.message);
        auto account=env("STORAGE_TEST_ACCOUNT"),password=env("STORAGE_TEST_PASSWORD");
        if(account.compare(0,10,"http_test_")||account.size()>64)throw std::runtime_error("Test account prefix required");
        std::string command=argv[1];
        if(command=="seed") {
            db::UserRepository users(c);
            s=users.insert({"u_"+auth::randomHex(16),account,"",auth::hashPassword(password)});
        } else if(command=="expire") {
            s=db::sql(c,"UPDATE sessions SET expires_at=UTC_TIMESTAMP()-INTERVAL 1 SECOND WHERE token_hash=? AND user_id=(SELECT id FROM users WHERE username=?)",
                      {auth::tokenHash(env("STORAGE_TEST_TOKEN")),account});
        } else if(command=="cleanup") {
            s=db::sql(c,"DELETE FROM users WHERE username=?",{account});
        } else throw std::runtime_error("Unknown fixture operation");
        if(!s.ok())throw std::runtime_error(s.message);result=0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';}
    mysql_library_end();return result;
}
