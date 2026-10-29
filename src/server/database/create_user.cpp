#include "UserRepository.hpp"
#include "../auth/Crypto.hpp"
#include <mysql.h>
#include <iostream>
#include <cstdlib>
// One-off provisioning tool. Use a provisioning account, never the runtime account.
// User password arrives on stdin (use a hidden local terminal prompt).
int main(int argc,char **argv) {
    if(argc!=2){std::cerr<<"Usage: storage_create_user USERNAME (password on stdin)\n";return 2;}
    std::string account=argv[1],password;
    if(account.empty()||account.size()>64||!std::getline(std::cin,password)||password.size()<12||password.size()>1024) {
        std::cerr<<"Username invalid or password must be 12..1024 bytes\n";return 2;
    }
    if(mysql_library_init(0,nullptr,nullptr))return 1;
    int result=1;
    try {
        storage::db::MySqlConfig config;auto s=storage::db::loadMySqlConfigFromEnvironment(config);
        if(!s.ok())throw std::runtime_error(s.message);
        storage::db::MySqlConnection connection;s=connection.open(config);config.password.clear();
        if(!s.ok())throw std::runtime_error(s.message);
        storage::db::UserRepository users(connection);
        s=users.insert({"u_"+storage::auth::randomHex(16),account,"",storage::auth::hashPassword(password)});
        password.clear();if(!s.ok())throw std::runtime_error(s.message);
        std::cout<<"User created (scrypt password hash).\n";result=0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';}
    mysql_library_end();return result;
}
