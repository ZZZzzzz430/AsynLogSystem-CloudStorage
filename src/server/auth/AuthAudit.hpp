#pragma once

#include "../../../log_system/logs_code/MyLog.hpp"
#include <chrono>
#include <cstdio>

namespace storage::auth {

class AuthAudit {
public:
    using Clock = std::chrono::steady_clock;

    explicit AuthAudit(const char* operation) noexcept
        : operation_(operation), start_(Clock::now()) {}

    AuthAudit(const AuthAudit&) = delete;
    AuthAudit& operator=(const AuthAudit&) = delete;

    void setStatus(int status) noexcept
    {
        status_ = status;
    }

    ~AuthAudit() noexcept
    {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - start_).count();

        const auto cost = static_cast<long long>(elapsed);

        try {
            auto logger = mylog::GetLogger("asynclogger");

            if (logger) {
                logger->Info(
                    "auth operation=%s status=%d cost_us=%lld",
                    operation_, status_, cost);
                return;
            }
        } catch (...) {
            // 日志失败不能改变认证结果，也不能再次调用日志器。
        }

        std::fprintf(
            stderr,
            "auth operation=%s status=%d cost_us=%lld\n",
            operation_, status_, cost);
    }

private:
    const char* operation_;
    Clock::time_point start_;
    int status_ = 503;
};

} // namespace storage::auth