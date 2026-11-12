
#include "SessionRepository.hpp"

#include <mysql.h>

#include <cstddef>
#include <memory>
#include <string>

namespace storage::db {

namespace {

// 检查指定位置之后的字符是否全部为小写十六进制字符。
bool isLowerHex(const std::string& text, std::size_t begin)
{
    for (std::size_t i = begin; i < text.size(); ++i) {
        const char ch = text[i];

        const bool isDigit = (ch >= '0' && ch <= '9');
        const bool isLowerLetter = (ch >= 'a' && ch <= 'f');

        if (!isDigit && !isLowerLetter) {
            return false;
        }
    }

    return true;
}

// 读取指定位置的十进制数字。
bool readDigits(
    const std::string& text,
    std::size_t begin,
    std::size_t count,
    unsigned int& out)
{
    if (begin > text.size() ||
        count > text.size() - begin) {
        return false;
    }

    unsigned int value = 0;

    for (std::size_t i = begin; i < begin + count; ++i) {
        const char ch = text[i];

        if (ch < '0' || ch > '9') {
            return false;
        }

        value = value * 10
              + static_cast<unsigned int>(ch - '0');
    }

    out = value;
    return true;
}

// 将严格格式的 UTC 时间字符串转换为 MYSQL_TIME。
// 格式：YYYY-MM-DDTHH:MM:SSZ
bool parseUtc(const std::string& text, MYSQL_TIME& out)
{
    out = MYSQL_TIME{};

    if (text.size() != 20 ||
        text[4] != '-' ||
        text[7] != '-' ||
        text[10] != 'T' ||
        text[13] != ':' ||
        text[16] != ':' ||
        text[19] != 'Z') {
        return false;
    }

    MYSQL_TIME value{};

    if (!readDigits(text, 0, 4, value.year) ||
        !readDigits(text, 5, 2, value.month) ||
        !readDigits(text, 8, 2, value.day) ||
        !readDigits(text, 11, 2, value.hour) ||
        !readDigits(text, 14, 2, value.minute) ||
        !readDigits(text, 17, 2, value.second)) {
        return false;
    }

    if (value.year < 1000 || value.year > 9999 ||
        value.month < 1 || value.month > 12 ||
        value.hour > 23 ||
        value.minute > 59 ||
        value.second > 59) {
        return false;
    }

    constexpr unsigned int daysPerMonth[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    unsigned int maxDay = daysPerMonth[value.month - 1];

    const bool isLeapYear =
        (value.year % 4 == 0) &&
        (value.year % 100 != 0 || value.year % 400 == 0);

    if (value.month == 2 && isLeapYear) {
        ++maxDay;
    }

    if (value.day < 1 || value.day > maxDay) {
        return false;
    }

    value.time_type = MYSQL_TIMESTAMP_DATETIME;

    out = value;
    return true;
}

// 统一处理 MySQL 预处理语句错误。
// 不暴露 SQL 内容、用户数据或 token。
DbStatus statementError(
    MYSQL_STMT* stmt,
    const char* operation)
{
    return {
        DbCode::DatabaseError,
        std::string(operation)
            + " failed, MySQL error code: "
            + std::to_string(mysql_stmt_errno(stmt))
    };
}

} // namespace

// 构造函数。
// connection_ 是外部连接的引用，不负责连接的生命周期。
SessionRepository::SessionRepository(
    MySqlConnection& connection) noexcept
    : connection_(connection)
{
}

// 插入 Session 记录。
DbStatus SessionRepository::insert(
    const SessionRecord& record)
{
    // 1. 检查数据库连接。
    if (!connection_.isOpen()) {
        return {
            DbCode::NotConnected,
            "MySQL connection is not open"
        };
    }

    // 2. 校验 token 摘要。
    // 要求 64 位小写十六进制字符。
    if (record.tokenDigest.size() != 64 ||
        !isLowerHex(record.tokenDigest, 0)) {
        return {
            DbCode::InvalidConfig,
            "tokenDigest must contain "
            "64 lowercase hex characters"
        };
    }

    // 3. 校验用户 ID。
    // 格式：u_ + 32 位小写十六进制字符。
    if (record.userId.size() != 34 ||
        record.userId.compare(0, 2, "u_") != 0 ||
        !isLowerHex(record.userId, 2)) {
        return {
            DbCode::InvalidConfig,
            "userId must be u_ followed by "
            "32 lowercase hex characters"
        };
    }

    // 4. 校验 UTC 到期时间。
    MYSQL_TIME expiresAt{};

    if (!parseUtc(record.expiresAtUtc, expiresAt)) {
        return {
            DbCode::InvalidConfig,
            "expiresAtUtc must be a valid "
            "YYYY-MM-DDTHH:MM:SSZ datetime"
        };
    }

    // 5. 准备参数缓冲区。
    std::string digest = record.tokenDigest;
    std::string userId = record.userId;

    unsigned long digestLength =
        static_cast<unsigned long>(digest.size());

    unsigned long userIdLength =
        static_cast<unsigned long>(userId.size());

    // 6. 固定 SQL，所有用户数据通过参数绑定。
    constexpr char sql[] =
        "INSERT INTO sessions "
        "(token_digest, user_id, expires_at) "
        "VALUES (?, ?, ?)";

    // 使用 RAII 自动释放 MYSQL_STMT。
    using StatementPtr =
        std::unique_ptr<
            MYSQL_STMT,
            decltype(&mysql_stmt_close)
        >;

    StatementPtr stmt(
        mysql_stmt_init(connection_.nativeHandle()),
        &mysql_stmt_close
    );

    if (!stmt) {
        return {
            DbCode::DatabaseError,
            "mysql_stmt_init failed"
        };
    }

    // 7. 准备预处理语句。
    if (mysql_stmt_prepare(
            stmt.get(),
            sql,
            static_cast<unsigned long>(sizeof(sql) - 1)
        ) != 0) {
        return statementError(
            stmt.get(),
            "mysql_stmt_prepare"
        );
    }

    // 检查参数数量是否符合预期。
    if (mysql_stmt_param_count(stmt.get()) != 3) {
        return {
            DbCode::DatabaseError,
            "Unexpected session INSERT parameter count"
        };
    }

    // 8. 绑定 SQL 参数。
    MYSQL_BIND params[3]{};

    // 参数 1：token_digest
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = digest.data();
    params[0].buffer_length = digestLength;
    params[0].length = &digestLength;

    // 参数 2：user_id
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = userId.data();
    params[1].buffer_length = userIdLength;
    params[1].length = &userIdLength;

    // 参数 3：expires_at
    params[2].buffer_type = MYSQL_TYPE_DATETIME;
    params[2].buffer = &expiresAt;
    params[2].buffer_length =
        static_cast<unsigned long>(sizeof(expiresAt));

    // 9. 提交参数绑定。
    if (mysql_stmt_bind_param(
            stmt.get(),
            params
        ) != 0) {
        return statementError(
            stmt.get(),
            "mysql_stmt_bind_param"
        );
    }

    // 10. 执行 INSERT。
    if (mysql_stmt_execute(stmt.get()) != 0) {

        // MySQL 错误码 1062：唯一键冲突。
        if (mysql_stmt_errno(stmt.get()) == 1062) {
            return {
                DbCode::Conflict,
                "Session token digest already exists"
            };
        }

        return statementError(
            stmt.get(),
            "mysql_stmt_execute"
        );
    }

    // 11. 确认实际插入一行。
    if (mysql_stmt_affected_rows(stmt.get()) != 1) {
        return {
            DbCode::DatabaseError,
            "Session INSERT did not affect exactly one row"
        };
    }

    // 12. 插入成功。
    // 不擅自提交事务，事务由调用方管理。
    return {
        DbCode::Ok,
        {}
    };
}

} // namespace storage::db
