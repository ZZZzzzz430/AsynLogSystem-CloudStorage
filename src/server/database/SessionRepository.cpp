#include "SessionRepository.hpp"
#include "Statement.hpp"

#include <memory>
#include <mysql.h>
namespace storage::db {
    SessionRepository::SessionRepository(MysqlConnection &connection) noexcept
        : connection_(connection) 
        {

        }

DbStatus SessionRepository::create(const std::string &hash, const std::string &id, std::int64_t expires) {
    return sql(connection_, "INSERT INTO sessions(token_hash,user_id,created_at,expires_at) VALUES(?,?,UTC_TIMESTAMP(),FROM_UNIXTIME(?))",
               {hash,id,std::to_string(expires)});
}
DbStatus SessionRepository::find(const std::string &hash, UserRecord &out) {
    out = {};
    std::vector<std::vector<std::string>> rows;
    auto s = sql(connection_, "SELECT u.id,u.username FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token_hash=? AND s.expires_at>UTC_TIMESTAMP()", {hash}, &rows);
    if (!s.ok()) return s;
    if (rows.empty()) return {DbCode::NotFound, "Session unavailable"};
    out.id = rows[0][0]; out.username = rows[0][1];
    return {DbCode::Ok, {}};
}
DbStatus SessionRepository::revoke(const std::string &hash) {
    return sql(connection_, "DELETE FROM sessions WHERE token_hash=?", {hash});
}

DbStatus SessionRepository::insert(const SessionRepository &record) {
    if (!connection_.isOpen()) {
        return {
            DbCode::NotConnected,
            "MySQL connection is not open"
        };
    }

    // 定义 固定 SQL，用户数据不参与拼接
    constexpr char sql[] = 
        "INSERT INTO sesstion "
        "(token_digest, user_id, expires_at) "
        "VALUES(?, ?, ?)";
    //  把 SQL 交给 MySQL 准备，
    std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> stmt(
            mysql_stmt_init(connection_.nativeHandle()),
            &mysql_stmt_close
            );

    if (!stmt) {
        return {
            DbCode::DatabaseError,
            "mysql_stmt_prepare failed, error code: " + std::tostring(mysql_stmt_error(stmt.get()))
        };
    }
    (void)record;  // chaned the type of record
    return {
        DbCode:NotImplementd,
        "Session isert is not implemented"
    };
}
}
#include "SessionRepository.hpp"

#include <mysql.h>

#include <memory>
#include <string>

namespace storage::db {
namespace {

// 检查从 begin 开始的所有字符是否都是小写十六进制字符。
// 固定一种表示方式，避免同一摘要出现大小写不同的文本形式。
bool isLowerHex(const std::string &text, std::size_t begin)
{
    for (std::size_t i = begin; i < text.size(); ++i) {
        const char ch = text[i];

        const bool isDigit = ch >= '0' && ch <= '9';
        const bool isLowerLetter = ch >= 'a' && ch <= 'f';

        if (!isDigit && !isLowerLetter) {
            return false;
        }
    }

    return true;
}

// 读取固定位置的一段十进制数字。
//
// 调用方先保证字符串总长度正确，因此这里不会越界。
// 不接受正负号、空格、字母等内容。
bool readDigits(
    const std::string &text,
    std::size_t begin,
    std::size_t count,
    unsigned int &out)
{
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

// 将严格格式的 UTC 文本转换成 MYSQL_TIME。
//
// 输入示例：2026-10-29T08:30:00Z
//
// 不使用 mktime，因为 mktime 会按本地时区解释输入。
// DATETIME 本身不保存时区；本仓储约定其中所有字段均属于 UTC。
bool parseUtc(const std::string &text, MYSQL_TIME &out)
{
    out = MYSQL_TIME{};

    // 第一步：检查总长度和固定分隔符。
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

    // 第二步：分别提取年月日时分秒。
    if (!readDigits(text, 0, 4, value.year) ||
        !readDigits(text, 5, 2, value.month) ||
        !readDigits(text, 8, 2, value.day) ||
        !readDigits(text, 11, 2, value.hour) ||
        !readDigits(text, 14, 2, value.minute) ||
        !readDigits(text, 17, 2, value.second)) {
        return false;
    }

    // 第三步：检查基本范围。
    // 使用 DATETIME 的有效年份范围，不接受零日期和闰秒。
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

    // 闰年：能被 4 整除，但整百年份还需要能被 400 整除。
    const bool leap =
        value.year % 4 == 0 &&
        (value.year % 100 != 0 || value.year % 400 == 0);

    if (value.month == 2 && leap) {
        ++maxDay;
    }

    // 拦截 2 月 30 日、4 月 31 日等不合法日期。
    if (value.day < 1 || value.day > maxDay) {
        return false;
    }

    // {} 已经将微秒、负号等字段清零。
    // 这里明确告诉 MySQL：这是完整日期时间。
    value.time_type = MYSQL_TIMESTAMP_DATETIME;

    out = value;
    return true;
}

// 统一生成语句错误结果。
//
// 只返回操作名和数字错误码，不附带用户输入、摘要或完整 SQL。
// 不直接返回 mysql_stmt_error() 的原始文本，减少内部信息泄露。
DbStatus statementError(MYSQL_STMT *stmt, const char *operation)
{
    return {
        DbCode::DatabaseError,
        std::string(operation)
            + " failed, MySQL error code: "
            + std::to_string(mysql_stmt_errno(stmt))
    };
}

} // namespace

SessionRepository::SessionRepository(
    MySqlConnection &connection) noexcept
    : connection_(connection)
{
    // 仅绑定引用，不在构造函数里建立连接或执行 SQL。
}

DbStatus SessionRepository::insert(const SessionRecord &record)
{
    // 1. 检查连接是否已经成功打开。
    // isOpen 不保证服务器此刻仍然可达，后续 API 仍须检查错误。
    if (!connection_.isOpen()) {
        return {
            DbCode::NotConnected,
            "MySQL connection is not open"
        };
    }

    // 2. 校验摘要。
    //
    // 沿用现有 DbCode，本例将无效记录归入 InvalidConfig，
    // 因此不需要修改 DbStatus.hpp。
    //
    // 固定长度也保证后面的 unsigned long 转换不会溢出。
    if (record.tokenDigest.size() != 64 ||
        !isLowerHex(record.tokenDigest, 0)) {
        return {
            DbCode::InvalidConfig,
            "tokenDigest must contain 64 lowercase hex characters"
        };
    }

    // 3. 校验用户 ID。
    if (record.userId.size() != 34 ||
        record.userId.compare(0, 2, "u_") != 0 ||
        !isLowerHex(record.userId, 2)) {
        return {
            DbCode::InvalidConfig,
            "userId must be u_ followed by 32 lowercase hex characters"
        };
    }

    // 4. 校验并转换到期时间。
    MYSQL_TIME expiresAt{};

    if (!parseUtc(record.expiresAtUtc, expiresAt)) {
        return {
            DbCode::InvalidConfig,
            "expiresAtUtc must be a valid YYYY-MM-DDTHH:MM:SSZ datetime"
        };
    }

    // 会话有效期由 AuthService 计算。
    // 本仓储检查日期是否合法，不负责决定有效期或判断是否已经过期。

    // 5. 准备输入缓冲区。
    //
    // record 是 const 引用；复制到局部字符串后，
    // C++17 的 data() 可以提供 C API 所需的 char*，
    // 不需要 const_cast，也不会修改调用方的数据。
    std::string digest = record.tokenDigest;
    std::string userId = record.userId;

    unsigned long digestLength =
        static_cast<unsigned long>(digest.size());

    unsigned long userIdLength =
        static_cast<unsigned long>(userId.size());

    // 6. SQL 结构固定，三个输入全部使用参数绑定。
    // 不使用字符串拼接，不使用 INSERT IGNORE 掩盖失败。
    constexpr char sql[] =
        "INSERT INTO sessions "
        "(token_digest, user_id, expires_at) "
        "VALUES (?, ?, ?)";

    // unique_ptr 管理 MYSQL_STMT。
    // 无论在哪个分支 return，都会自动调用 mysql_stmt_close。
    using StatementPtr =
        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)>;

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

    // 7. 准备 SQL。
    // sizeof(sql) 包含末尾 '\0'，实际 SQL 长度需要减 1。
    if (mysql_stmt_prepare(
            stmt.get(), sql, sizeof(sql) - 1) != 0) {
        return statementError(stmt.get(), "mysql_stmt_prepare");
    }

    // 防止未来修改 SQL 时，忘记同步修改绑定数组。
    if (mysql_stmt_param_count(stmt.get()) != 3) {
        return {
            DbCode::DatabaseError,
            "Unexpected session INSERT parameter count"
        };
    }

    // 8. 描述三个参数。
    //
    // {} 清零所有成员。
    // is_null 为 nullptr，表示这里提供的参数不是 SQL NULL。
    MYSQL_BIND params[3]{};

    // 第一个 ?：token 摘要。
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = digest.data();
    params[0].buffer_length = digestLength;
    params[0].length = &digestLength;

    // 第二个 ?：用户 ID。
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = userId.data();
    params[1].buffer_length = userIdLength;
    params[1].length = &userIdLength;

    // 第三个 ?：到期时间。
    //
    // 使用 MYSQL_TIME 绑定完整日期时间，
    // 不依赖数据库隐式解析带 T、Z 的字符串。
    // 固定类型的时间参数无需设置字符串 length 指针。
    params[2].buffer_type = MYSQL_TYPE_DATETIME;
    params[2].buffer = &expiresAt;
    params[2].buffer_length = sizeof(expiresAt);

    // 9. 绑定全部参数。
    // 绑定成功还不等于插入成功。
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        return statementError(stmt.get(), "mysql_stmt_bind_param");
    }

    // 10. 真正执行 INSERT。
    //
    // digest、userId、长度变量及 expiresAt，
    // 都在本函数作用域内，能够存活到同步执行结束。
    if (mysql_stmt_execute(stmt.get()) != 0) {
        // 1062：唯一键冲突。
        // 本表以摘要为主键，重复摘要会由数据库最终拦截。
        if (mysql_stmt_errno(stmt.get()) == 1062) {
            return {
                DbCode::Conflict,
                "Session token digest already exists"
            };
        }

        // 外键错误、连接中断、权限不足等保持为数据库故障，
        // 不混同为“密码错误”或“查不到用户”。
        return statementError(stmt.get(), "mysql_stmt_execute");
    }

    // 11. 普通单行 INSERT 应影响一行。
    if (mysql_stmt_affected_rows(stmt.get()) != 1) {
        return {
            DbCode::DatabaseError,
            "Session INSERT did not affect exactly one row"
        };
    }

    // 12. 操作成功。
    //
    // 不在仓储中私自提交调用方的事务，不自动重试写入。
    // 网络故障可能导致提交结果未知，业务层不能据此放行登录。
    return {DbCode::Ok, {}};

    // 离开作用域后自动释放 stmt；
    // connection_ 仍由调用方拥有和管理。
}

} // namespace storage::db
