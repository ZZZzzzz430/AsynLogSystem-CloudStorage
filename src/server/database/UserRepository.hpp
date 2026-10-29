#pragma once

#include "MySqlConnection.hpp"

#include <string>

namespace storage::db {

// 这是服务端内部记录，passwordHash 绝不能整体序列化成客户端 UserDto。
struct UserRecord {
    std::string id; // 延续既有 API 设计：u_ + 32 位 hex，使用安全随机数生成。
    std::string username;
    std::string email; // 可选；规范化/唯一性规则由 DB02/DB04 明确，不能随意 OR 两列。
    std::string passwordHash; // 密码哈希库的编码结果，禁止保存明文或普通 SHA256 口令。
};

// 仓储只处理 SQL 与数据映射，不解析 HTTP、不显示 Qt 控件、不生成登录 token。
class UserRepository {
public:
    // 引用不拥有连接：connection 必须先创建、后销毁，且与仓储在同一线程使用。
    explicit UserRepository(MySqlConnection &connection) noexcept;

    // 只支持大小写敏感的用户名登录；区分 NotFound 与数据库故障。
    // 失败必须清空 out，避免调用者误用上一次查询留下的用户身份。
    [[nodiscard]] DbStatus findByAccount(const std::string &account, UserRecord &out);

    // 仅供用户初始化工具使用，只接收已哈希的记录。
    // 唯一键冲突返回 Conflict，不用“先查不存在再插入”替代数据库唯一约束。
    [[nodiscard]] DbStatus insert(const UserRecord &record);

private:
    MySqlConnection &connection_;
};

} // namespace storage::db
