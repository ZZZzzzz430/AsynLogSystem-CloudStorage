#include "UserRepository.hpp"
#include "Statement.hpp"
namespace storage::db {
UserRepository::UserRepository(MySqlConnection &connection) noexcept : connection_(connection) {}
DbStatus UserRepository::findByAccount(const std::string &account, UserRecord &out) {
    out = {};
    std::vector<std::vector<std::string>> rows;
    auto status = sql(connection_, "SELECT id,username,email,password_hash FROM users WHERE username=?", {account}, &rows);
    if (!status.ok()) return status;
    if (rows.empty()) return {DbCode::NotFound, "User not found"};
    out = {rows[0][0], rows[0][1], rows[0][2], rows[0][3]};
    return {DbCode::Ok, {}};
}
DbStatus UserRepository::insert(const UserRecord &r) {
    return sql(connection_, "INSERT INTO users(id,username,email,password_hash) VALUES(?,?,?,?)",
               {r.id,r.username,r.email,r.passwordHash});
}
}
