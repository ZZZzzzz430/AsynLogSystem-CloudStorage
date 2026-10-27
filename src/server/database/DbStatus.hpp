#pragma once

#include <string>

namespace storage::db {

// 数据库“不可用”和“查询不到”含义不同；不能都变成 false 后让上层猜测。
// NotImplemented 是练习占位，不代表已连接、已建表或不存在此用户。
enum class DbCode {
    Ok,
    NotImplemented,
    InvalidConfig,
    InvalidState,
    NotConnected,
    NotFound,
    Conflict,
    DatabaseError
};

struct DbStatus {
    // 默认失败，避免漏写返回状态时意外通过。
    DbCode code = DbCode::NotImplemented;
    // 给服务端诊断使用；不能拼入密码、完整连接串或直接将内部错误返回客户端。
    std::string message;

    // 仅判断明确的 Ok；枚举的其他值都不是成功。
    [[nodiscard]] bool ok() const noexcept { return code == DbCode::Ok; }
};

} // namespace storage::db
