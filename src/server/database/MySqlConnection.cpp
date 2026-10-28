#include "MySqlConnection.hpp"

#include <mysql.h>

#include <charconv>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

namespace storage::db {

namespace {

constexpr unsigned int maxTimeoutSeconds = 3600;

// 防止 std::string 中的内嵌 NUL 被 C API 当成字符串结束符。
bool validText(const std::string &value)
{
    return !value.empty() && value.find('\0') == std::string::npos;
}

DbStatus invalid(const char *field)
{
    // 只报告变量名，不输出收到的值，尤其不输出密码。
    return {DbCode::InvalidConfig, std::string("Invalid or missing configuration: ") + field};
}

DbStatus validate(const MySqlConfig &config)
{
    if (!validText(config.host)) return invalid("STORAGE_DB_HOST");
    if (!validText(config.database)) return invalid("STORAGE_DB_NAME");
    if (!validText(config.user)) return invalid("STORAGE_DB_USER");
    if (!validText(config.password)) return invalid("STORAGE_DB_PASSWORD");
    if (config.port == 0 || config.port > 65535) return invalid("STORAGE_DB_PORT");
    if (config.connectTimeoutSeconds == 0 || config.connectTimeoutSeconds > maxTimeoutSeconds)
        return invalid("STORAGE_DB_CONNECT_TIMEOUT");
    if (config.readTimeoutSeconds == 0 || config.readTimeoutSeconds > maxTimeoutSeconds)
        return invalid("STORAGE_DB_READ_TIMEOUT");
    if (config.writeTimeoutSeconds == 0 || config.writeTimeoutSeconds > maxTimeoutSeconds)
        return invalid("STORAGE_DB_WRITE_TIMEOUT");
    if (config.tlsCaFile.find('\0') != std::string::npos ||
        (config.requireVerifiedTls && config.tlsCaFile.empty()))
        return invalid("STORAGE_DB_TLS_CA_FILE");
    return {DbCode::Ok, {}};
}

// 缺少可选变量时保留默认值；显式空字符串、符号、尾随字符和溢出均失败。
bool readUnsigned(const char *name, unsigned int &out, unsigned int maximum)
{
    const char *text = std::getenv(name);
    if (text == nullptr) return true;
    const std::string_view value(text);
    unsigned int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed == 0 || parsed > maximum) return false;
    out = parsed;
    return true;
}

DbStatus databaseError(MYSQL *handle, const char *operation)
{
    // 原始 mysql_error 可能带上外部输入。返回操作名和错误码即可定位，避免泄密。
    return {DbCode::DatabaseError,
            std::string(operation) + " failed (MySQL error " +
                std::to_string(mysql_errno(handle)) + ")"};
}

} // namespace

// 在局部配置中读取和校验，全部成功才发布，失败不留下上次的凭据。
DbStatus loadMySqlConfigFromEnvironment(MySqlConfig &out)
{
    out = MySqlConfig{};
    MySqlConfig config;
    const auto readText = [](const char *name, std::string &target) {
        if (const char *value = std::getenv(name)) target = value;
    };
    readText("STORAGE_DB_HOST", config.host);
    readText("STORAGE_DB_NAME", config.database);
    readText("STORAGE_DB_USER", config.user);
    readText("STORAGE_DB_PASSWORD", config.password);
    readText("STORAGE_DB_TLS_CA_FILE", config.tlsCaFile);

    if (!readUnsigned("STORAGE_DB_PORT", config.port, 65535)) return invalid("STORAGE_DB_PORT");
    if (!readUnsigned("STORAGE_DB_CONNECT_TIMEOUT", config.connectTimeoutSeconds, maxTimeoutSeconds))
        return invalid("STORAGE_DB_CONNECT_TIMEOUT");
    if (!readUnsigned("STORAGE_DB_READ_TIMEOUT", config.readTimeoutSeconds, maxTimeoutSeconds))
        return invalid("STORAGE_DB_READ_TIMEOUT");
    if (!readUnsigned("STORAGE_DB_WRITE_TIMEOUT", config.writeTimeoutSeconds, maxTimeoutSeconds))
        return invalid("STORAGE_DB_WRITE_TIMEOUT");
    if (const char *tls = std::getenv("STORAGE_DB_REQUIRE_VERIFIED_TLS")) {
        const std::string_view value(tls);
        if (value == "1") config.requireVerifiedTls = true;
        else if (value == "0") config.requireVerifiedTls = false;
        else return invalid("STORAGE_DB_REQUIRE_VERIFIED_TLS");
    }

    const auto status = validate(config);
    if (!status.ok()) return status;
    out = std::move(config);
    return {DbCode::Ok, {}};
}

// 资源回收是框架提供的基础接线，不代替用户实现连接/查询逻辑。
MySqlConnection::~MySqlConnection() noexcept
{
    close();
}

// 临时句柄使用 RAII：设置选项、连接或后续步骤失败都会自动关闭。
DbStatus MySqlConnection::open(const MySqlConfig &config)
{
    if (isOpen()) return {DbCode::InvalidState, "MySQL connection is already open"};
    const auto status = validate(config);
    if (!status.ok()) return status;

    std::unique_ptr<MYSQL, decltype(&mysql_close)> local(mysql_init(nullptr), &mysql_close);
    if (!local) return {DbCode::DatabaseError, "mysql_init failed"};

    // 总是使用 TCP，避免 localhost 在不同平台被解释成 socket/命名管道。
    const unsigned int protocol = MYSQL_PROTOCOL_TCP;
    const bool reconnect = false;
    // 0 仍强制加密，只是不验证服务器身份；本机练习必须显式选择，绝不自动降级。
    const mysql_ssl_mode sslMode = config.requireVerifiedTls
        ? SSL_MODE_VERIFY_IDENTITY : SSL_MODE_REQUIRED;
    if (mysql_options(local.get(), MYSQL_OPT_CONNECT_TIMEOUT, &config.connectTimeoutSeconds) != 0 ||
        mysql_options(local.get(), MYSQL_OPT_READ_TIMEOUT, &config.readTimeoutSeconds) != 0 ||
        mysql_options(local.get(), MYSQL_OPT_WRITE_TIMEOUT, &config.writeTimeoutSeconds) != 0 ||
        mysql_options(local.get(), MYSQL_OPT_PROTOCOL, &protocol) != 0 ||
        mysql_options(local.get(), MYSQL_OPT_RECONNECT, &reconnect) != 0 ||
        mysql_options(local.get(), MYSQL_SET_CHARSET_NAME, "utf8mb4") != 0 ||
        mysql_options(local.get(), MYSQL_OPT_SSL_MODE, &sslMode) != 0)
        return databaseError(local.get(), "mysql_options");

    if (!config.tlsCaFile.empty() &&
        mysql_options(local.get(), MYSQL_OPT_SSL_CA, config.tlsCaFile.c_str()) != 0)
        return databaseError(local.get(), "MYSQL_OPT_SSL_CA");

    if (mysql_real_connect(local.get(), config.host.c_str(), config.user.c_str(),
                           config.password.c_str(), config.database.c_str(),
                           config.port, nullptr, 0) == nullptr)
        return databaseError(local.get(), "mysql_real_connect");

    // 除 options 之外，确认连接完成后的实际字符集设置也成功。
    if (mysql_set_character_set(local.get(), "utf8mb4") != 0)
        return databaseError(local.get(), "mysql_set_character_set");

    handle_ = local.release();
    return {DbCode::Ok, {}};
}

// 自动重连已禁用；探活失败后丢弃失效句柄，调用方显式决定如何恢复。
DbStatus MySqlConnection::ping()
{
    if (!isOpen()) return {DbCode::NotConnected, "MySQL connection is not open"};
    if (mysql_ping(handle_) != 0) {
        const auto status = databaseError(handle_, "mysql_ping");
        close();
        return status;
    }
    return {DbCode::Ok, {}};
}

// 每个有效句柄只关闭一次；此函数不发 SQL，也不删除表或数据库。
void MySqlConnection::close() noexcept
{
    if (handle_ != nullptr) {
        mysql_close(handle_);
        handle_ = nullptr;
    }
}

// 句柄的发布点只允许在 open 的全部步骤成功之后。
bool MySqlConnection::isOpen() const noexcept
{
    return handle_ != nullptr;
}

// 借用裸指针用于 C API 参数，不转移所有权。
MYSQL *MySqlConnection::nativeHandle() const noexcept
{
    return handle_;
}

} // namespace storage::db
