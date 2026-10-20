
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "AsyncWorker.hpp"

using namespace std::chrono_literals;
mylog::Util::JsonData* g_conf_data = nullptr;

int main()
{

    g_conf_data = mylog::Util::JsonData::GetJsonData();
    g_conf_data->buffer_size = 64;

    // 1. 控制消费者的门闩
    std::mutex gate_mtx;
    std::condition_variable gate_cv;
    bool release_callback = false;

    // 2. 通知主线程：第一次 callback 已经开始
    std::promise<void> callback_started;
    auto callback_ready = callback_started.get_future();

    std::atomic<int> callback_count{0};
    std::atomic<size_t> consumed_bytes{0};

    // 3. 消费回调：仅第一次进入时暂停
    auto callback = [&](mylog::Buffer &buf) {
        int id = ++callback_count;

        std::cout << "[CALLBACK] start #" << id << '\n';

        if (id == 1) {
            {
                std::lock_guard<std::mutex> lock(gate_mtx);

                // 向主线程报告：已进入第一次回调
                callback_started.set_value();
            }

            std::unique_lock<std::mutex> lock(gate_mtx);

            gate_cv.wait(lock, [&]() {
                return release_callback;
            });
        }

        size_t bytes = buf.ReadableSize();
        consumed_bytes.fetch_add(bytes);

        std::cout << "[CALLBACK] end #" << id
                  << ", bytes=" << bytes << '\n';
    };

    // 4. 创建 SAFE 模式异步工作线程
    mylog::AsyncWorker worker(
        callback,
        mylog::AsyncType::ASYNC_SAFE
    );

    // 第一次 Push：64 字节
    std::string first(64, 'A');

    worker.Push(first.data(), first.size());
    std::cout << "[MAIN] first Push(64) done\n";

    // 等待消费者确定进入 callback
    callback_ready.wait();
    std::cout << "[MAIN] callback entered\n";

    // 第二次 Push：64 字节
    std::string second(64, 'B');

    worker.Push(second.data(), second.size());
    std::cout << "[MAIN] second Push(64) done\n";

    // 第三次 Push：8 字节
    // 单独使用线程，避免主线程被阻塞
    std::string third(8, 'C');

    std::promise<void> third_started;
    auto third_ready = third_started.get_future();

    std::promise<void> third_finished;
    auto third_done = third_finished.get_future();

    std::thread producer([&]() {
        third_started.set_value();

        auto begin = std::chrono::steady_clock::now();

        try {
            worker.Push(third.data(), third.size());

            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<
                std::chrono::milliseconds
            >(end - begin).count();

            std::cout << "[PRODUCER] Push(8) took "
                      << ms << " ms\n";

            third_finished.set_value();
        }
        catch (...) {
            third_finished.set_exception(
                std::current_exception()
            );
        }
    });

    // 确认生产者线程已经启动
    third_ready.wait();

    // 在释放消费者前，观察第三次 Push 是否完成
    auto status = third_done.wait_for(200ms);

    if (status == std::future_status::timeout) {
        std::cout << "[MAIN] third Push still pending\n";
    } else {
        std::cout << "[MAIN] third Push finished early\n";
    }

    // 5. 打开消费者门闩
    {
        std::lock_guard<std::mutex> lock(gate_mtx);
        release_callback = true;
    }
    gate_cv.notify_all();

    // 6. 等待第三次 Push 完成
    try {
        third_done.get();
    }
    catch (const std::exception &e) {
        std::cerr << "[ERROR] " << e.what() << '\n';
        producer.join();
        worker.Stop();
        return 1;
    }

    producer.join();

    // 7. 排空已经接受的日志并停止
    worker.Stop();

    std::cout << "[RESULT] callback count = "
              << callback_count.load() << '\n';

    std::cout << "[RESULT] consumed bytes = "
              << consumed_bytes.load() << '\n';

    bool ok =
        status == std::future_status::timeout &&
        consumed_bytes.load() == 136;

    std::cout << (ok ? "[PASS]" : "[FAIL]") << '\n';

    return ok ? 0 : 1;
}

