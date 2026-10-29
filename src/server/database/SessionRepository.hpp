#pragma once
#include "UserRepository.hpp"
#include <cstdint>
#include <ctime>
namespace storage::db {

// 数据结构
struct SessionRecord {
    std::string tokenDigest; // token
    std::string id_;  // id ,用于确认
    std::string exiresAtUtc;  // 业务层来计算到期时间。
    
}


class SessionRepository {
public:
    explicit SessionRepository(MySqlConnection &connection) : connection_(connection) {}
    DbStatus create(const std::string &hash, const std::string &userId, std::int64_t expires);
    DbStatus find(const std::string &hash, UserRecord &out);
    DbStatus revoke(const std::string &hash);

    // 接收会话记录,只读取记录，返回独立操作
    [[nodiscard]] Dbstatus insert(const SessionRecord &record);
private:
    MySqlConnection &connection_;
};
}
