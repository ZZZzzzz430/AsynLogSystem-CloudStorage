#pragma once

#include "DbStatus.hpp"

#include <string>

// 按本机 MySQL 8 SDK 的 struct MYSQL 声明做前置声明，避免 mysql.h 污染业务头文件。
// 真正包含 mysql.h 的实现文件由数据库独立目标编译，Qt 客户端不依赖该 SDK。
struct MYSQL;

namespace storage::db {

struct MySqlConfig {
    std::string host = "127.0.0.1";
    unsigned int port = 3306;
    std::string database;
    std::string user;
    // 这是服务端连接数据库的账号密码，不是登录云盘的用户密码。
    // 不硬编码、不打印、不提交真实配置；本对象仅在初始化期间短暂持有。
    std::string password;
    unsigned int connectTimeoutSeconds = 3;
    unsigned int readTimeoutSeconds = 3;
    unsigned int writeTimeoutSeconds = 3;
    // true: VERIFY_IDENTITY，需要 CA 且 host 匹配证书。
    // false: REQUIRED，仍强制 TLS 加密；仅建议本机练习显式选择，不验证服务器身份。
    bool requireVerifiedTls = true;
    std::string tlsCaFile;
};

// 读取约定的环境变量并校验。失败时 out 保持空凭据，不自动加载 .env 文件。
// 端口 1..65535，超时 1..3600 秒；TLS 标志只接受 0 或 1。
[[nodiscard]] DbStatus loadMySqlConfigFromEnvironment(MySqlConfig &out);

// 一个实例独占一个数据库连接；创建、使用、销毁均放在同一个服务端执行线程。
// 本类不是连接池，也不承诺线程安全。禁止复制/移动，避免仓储引用和句柄所有权悬空。
class MySqlConnection {
public:
    // 构造不连库，便于区分“创建 C++ 对象”和“真实网络连接成功”。
    MySqlConnection() noexcept = default;
    
    // 只做资源回收；析构不能抛异常，也不能在这里做重连。
    ~MySqlConnection() noexcept;
    MySqlConnection(const MySqlConnection &) = delete;
    MySqlConnection &operator=(const MySqlConnection &) = delete;
    MySqlConnection(MySqlConnection &&) = delete;
    MySqlConnection &operator=(MySqlConnection &&) = delete;

    // 真实 TCP 连接；全部设置成功后才发布句柄，不自动建库建表。
    // 调用方在启动工作线程前执行 mysql_library_init；所有线程退出后 library_end。
    // 工作线程内首次 mysql_init 会初始化线程状态，退出前须调用 mysql_thread_end。
    [[nodiscard]] DbStatus open(const MySqlConfig &config);

    // 真实探活；不自动重连，失败时关闭失效连接并返回 DatabaseError。
    [[nodiscard]] DbStatus ping();

    // 已给出的生命周期接线：回收自己拥有的句柄，可重复调用。
    void close() noexcept;

    // 这里只表示 open 已发布一个句柄，不保证服务器此刻仍可达；真实探活用 ping。
    [[nodiscard]] bool isOpen() const noexcept;

    // 只给 UserRepository 借用；调用方不能 mysql_close、跨线程共享或存到更长寿命对象。
    [[nodiscard]] MYSQL *nativeHandle() const noexcept;

private:
    // 不变量：只能为 nullptr，或完全连接成功后由本对象独占的 MYSQL*。
    // mysql_init 得到但尚未连接的临时句柄不应提前存到这里。
    MYSQL *handle_ = nullptr;
};

} // namespace storage::db
