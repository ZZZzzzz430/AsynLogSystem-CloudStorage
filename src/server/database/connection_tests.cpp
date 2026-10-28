#include "MySqlConnection.hpp"

#include <mysql.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

void set(const char *name, const char *value)
{
#ifdef _WIN32
    if (_putenv_s(name, value == nullptr ? "" : value) != 0) std::abort();
#else
    if ((value == nullptr ? unsetenv(name) : setenv(name, value, 1)) != 0) std::abort();
#endif
}

// 测试进程使用自己的虚构配置，绝不读取或连接开发者真实数据库。
void baseline()
{
    set("STORAGE_DB_HOST", "127.0.0.1");
    set("STORAGE_DB_PORT", nullptr);
    set("STORAGE_DB_NAME", "test_database");
    set("STORAGE_DB_USER", "test_user");
    set("STORAGE_DB_PASSWORD", "test_secret_do_not_log");
    set("STORAGE_DB_CONNECT_TIMEOUT", nullptr);
    set("STORAGE_DB_READ_TIMEOUT", nullptr);
    set("STORAGE_DB_WRITE_TIMEOUT", nullptr);
    set("STORAGE_DB_REQUIRE_VERIFIED_TLS", "0");
    set("STORAGE_DB_TLS_CA_FILE", nullptr);
}

void expectInvalid(const char *name, const char *value)
{
    baseline();
    set(name, value);
    storage::db::MySqlConfig config;
    config.user = "stale_user";
    config.password = "stale_secret";
    const auto status = storage::db::loadMySqlConfigFromEnvironment(config);
    check(status.code == storage::db::DbCode::InvalidConfig, "invalid environment rejected");
    check(config.user.empty() && config.password.empty(), "failure clears stale credentials");
    check(status.message.find("test_secret") == std::string::npos, "secret absent from errors");
}
} // namespace

int main(int argc, char **argv)
{
    using namespace storage::db;
    baseline();
    MySqlConfig config;
    check(loadMySqlConfigFromEnvironment(config).ok(), "valid local environment");
    check(config.port == 3306 && config.connectTimeoutSeconds == 3, "defaults preserved");

    for (const char *value : {"0", "65536", "abc", "3306xyz", "-1", "+1", " 3306", "3306 ", "99999999999999999999"})
        expectInvalid("STORAGE_DB_PORT", value);
    for (const char *name : {"STORAGE_DB_CONNECT_TIMEOUT", "STORAGE_DB_READ_TIMEOUT", "STORAGE_DB_WRITE_TIMEOUT"})
        for (const char *value : {"0", "3601", "-1", "3x", "99999999999999999999"})
            expectInvalid(name, value);
    for (const char *name : {"STORAGE_DB_NAME", "STORAGE_DB_USER", "STORAGE_DB_PASSWORD"})
        expectInvalid(name, nullptr);
    expectInvalid("STORAGE_DB_REQUIRE_VERIFIED_TLS", "true");
    expectInvalid("STORAGE_DB_REQUIRE_VERIFIED_TLS", "2");
    expectInvalid("STORAGE_DB_REQUIRE_VERIFIED_TLS", "1"); // 缺少 CA。
    expectInvalid("STORAGE_DB_REQUIRE_VERIFIED_TLS", nullptr); // 默认也要求 CA。

    baseline();
    set("STORAGE_DB_PORT", "65535");
    set("STORAGE_DB_CONNECT_TIMEOUT", "3600");
    set("STORAGE_DB_REQUIRE_VERIFIED_TLS", "1");
    set("STORAGE_DB_TLS_CA_FILE", "test-ca.pem");
    check(loadMySqlConfigFromEnvironment(config).ok() && config.requireVerifiedTls,
          "upper bounds and verified TLS configuration");

    if (mysql_library_init(0, nullptr, nullptr) != 0) return 1;
    {
        MySqlConnection connection;
        check(connection.ping().code == DbCode::NotConnected, "ping before open");
        config.port = 0;
        check(connection.open(config).code == DbCode::InvalidConfig && !connection.isOpen(),
              "direct open validates configuration");
        config.port = 3306;
        config.user = std::string("test\0hidden", 11);
        check(connection.open(config).code == DbCode::InvalidConfig && !connection.isOpen(),
              "embedded NUL rejected");

        // 可选参数由本机测试脚本提供一个刚关闭的环回端口；CTest 默认不连接网络。
        if (argc == 2) {
            baseline();
            set("STORAGE_DB_PORT", argv[1]);
            check(loadMySqlConfigFromEnvironment(config).ok(), "closed-port configuration");
            for (int attempt = 0; attempt < 2; ++attempt) {
                const auto status = connection.open(config);
                check(status.code == DbCode::DatabaseError, "closed endpoint fails to connect");
                check(!connection.isOpen() && connection.nativeHandle() == nullptr,
                      "connection failure never publishes a handle");
                check(connection.ping().code == DbCode::NotConnected, "ping after failed open");
            }
        }
        connection.close();
        connection.close();
        check(!connection.isOpen(), "repeated close is safe");
    }
    mysql_library_end();
    if (failures != 0) return 1;
    std::cout << "MySQL configuration and connection-state checks passed.\n";
    return 0;
}
