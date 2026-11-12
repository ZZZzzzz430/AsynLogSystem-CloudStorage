#pragma once

#include <chrono>

namespace storage::auth {

// 当前由同一个 libevent 线程访问。
// 如果以后改成多线程处理请求，需要增加同步保护。
class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    explicit RateLimiter(unsigned limit)
        : limit_(limit) {}

    // 返回 0 表示允许；
    // 返回正数表示拒绝，以及建议等待的秒数。
    // 测试可以传入时间，无需实际等待；生产代码仍调用 check()。
    int check(Clock::time_point now = Clock::now())
    {
        if (now >= windowEnd_) {
            used_ = 0;
            windowEnd_ = now + std::chrono::seconds(60);
        }

        if (used_ < limit_) {
            ++used_;
            return 0;
        }

        // 向上取整，避免剩余不足一秒时返回 0。
        const auto remaining =
            std::chrono::ceil<std::chrono::seconds>(
                windowEnd_ - now);

        return static_cast<int>(remaining.count());
    }

private:
    unsigned limit_;
    unsigned used_ = 0;
    Clock::time_point windowEnd_{};
};

} // namespace storage::auth
