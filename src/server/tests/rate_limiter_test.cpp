#include "../auth/RateLimiter.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    std::cout << "PASS " << name << '\n';
}

int main() {
    using namespace std::chrono;
    using storage::auth::RateLimiter;
    const auto start = RateLimiter::Clock::time_point{} + seconds(100);
    try {
        RateLimiter registration(5), login(10);
        for (int i = 0; i < 5; ++i)
            check(registration.check(start) == 0, "registration allowance");
        check(registration.check(start) == 60, "sixth registration rejected");
        check(registration.check(start + milliseconds(59100)) == 1, "Retry-After rounds up");
        check(registration.check(start + seconds(60)) == 0, "rejection does not extend window");
        for (int i = 0; i < 10; ++i)
            check(login.check(start) == 0, "login allowance independent of registration");
        check(login.check(start) == 60, "eleventh login rejected");
        check(login.check(start + hours(24)) == 0, "recovery after long idle");
        RateLimiter disabled(0);
        check(disabled.check(start) == 60, "zero allowance never permits request");
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
