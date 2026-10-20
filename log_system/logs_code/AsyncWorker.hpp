#pragma once
#include <algorithm>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <chrono>

#include "AsyncBuffer.hpp"

namespace mylog
{
    enum class AsyncType
    {
        ASYNC_SAFE,
        ASYNC_UNSAFE
    };
    enum class PushResult { Accepted, TimeOut, Stopped, TooLarge };

    using functor = std::function<void(Buffer &)>;
    class AsyncWorker
    {
    public:
        using ptr = std::shared_ptr<AsyncWorker>;
        AsyncWorker(const functor &cb, AsyncType async_type = AsyncType::ASYNC_SAFE)
            : async_type_(async_type),
              callback_(cb),
              stop_(false),
              thread_(std::thread(&AsyncWorker::ThreadEntry, this)) {}
        ~AsyncWorker() { Stop(); }

        PushResult PushFor(const char* data, size_t len,
                           std::chrono::milliseconds timeout)
        {
            // 非正期限只检查当前空间，不等待。
            const auto deadline = std::chrono::steady_clock::now() +
                std::max(timeout, std::chrono::milliseconds::zero());
            std::unique_lock<std::mutex> lock(mtx_);
            if (stop_) return PushResult::Stopped;

            const bool fixed = (async_type_ == AsyncType::ASYNC_SAFE);
            if (fixed)
            {
                if (len > buffer_productor_.Capacity())
                    return PushResult::TooLarge;
                const bool ready = cond_productor_.wait_until(lock, deadline, [&]() {
                    return stop_ || len <= buffer_productor_.WriteableSize();
                });
                if (stop_) return PushResult::Stopped;
                if (!ready) return PushResult::TimeOut;
            }
            buffer_productor_.Push(data, len, !fixed);
            cond_consumer_.notify_one();
            return PushResult::Accepted;
        }
        void Push(const char *data, size_t len)
        {
            // 如果生产者队列不足以写下len长度数据，并且缓冲区是固定大小，那么阻塞
            std::unique_lock<std::mutex> lock(mtx_);

            if (stop_){
                throw std::runtime_error(
                        "AsuncWorker has stopped"
                        );
            }

            const bool fixed = (async_type_ == AsyncType::ASYNC_SAFE);

            if (fixed){
                // 当  len 大于缓冲区总容量就报错   len = 65, capacity = 64 无法写入
                if (len > buffer_productor_.Capacity()){
                    throw std::length_error(
                                "AsyncWorker: log exceeds remaining capacity"
                           );
                }

                // 第二层： 没有超过总容量，但暂时放不下，等待或者扩容.
                cond_productor_.wait(lock, [&](){
                       return stop_ ||
                        len <= buffer_productor_.WriteableSize();
                    });

                // 等待的时候收到 stop 就报异常
                if (stop_){
                    throw std::runtime_error(
                            "AsyncWorker stopped while waitting"
                            );
                }


            }
            // SAFE 禁止扩容， UNSAFE 允许扩容。
            buffer_productor_.Push(data, len, !fixed);
            cond_consumer_.notify_one();
        }
        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                stop_ = true;
            }

            cond_productor_.notify_all();
            cond_consumer_.notify_all(); // 所有线程把缓冲区内数据处理完就结束了
            if (thread_.joinable()){
                thread_.join();
            }
        }

    private:
        void ThreadEntry()
        {
            while (true)
            {
                {
                    std::unique_lock<std::mutex> lock(mtx_);

                    cond_consumer_.wait(lock, [&]() {
                        return stop_ ||
                            !buffer_productor_.IsEmpty();
                    });

                    // 已停止且没有待处理数据
                    if (stop_ && buffer_productor_.IsEmpty())
                    {
                        return;
                    }

                    buffer_productor_.Swap(buffer_consumer_);

                    if (async_type_ == AsyncType::ASYNC_SAFE)
                    {
                        cond_productor_.notify_all();
                    }
                }

                // 锁外执行磁盘写入回调
                callback_(buffer_consumer_);

                // 消费完成后重置缓冲区
                buffer_consumer_.Reset();
            }
        }
    private:
        AsyncType async_type_;
        std::atomic<bool> stop_; // 用于控制异步工作器的启动
        std::mutex mtx_;
        mylog::Buffer buffer_productor_;
        mylog::Buffer buffer_consumer_;
        std::condition_variable cond_productor_;
        std::condition_variable cond_consumer_;
        functor callback_; // 回调函数，用来告知工作器如何落地
        std::thread thread_;
    };
} // namespace mylog
