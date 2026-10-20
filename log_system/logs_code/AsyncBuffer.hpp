/*日志缓冲区类设计*/
#pragma once
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <stdexcept>


#include "Util.hpp"

extern mylog::Util::JsonData* g_conf_data;

namespace mylog{
    class Buffer{
    public:
        Buffer() : write_pos_(0), read_pos_(0) {
            buffer_.resize(g_conf_data->buffer_size);
        }

        void Push(const char *data, size_t len, bool allow_growth = true)
        {
            if (len == 0) return;

            if (len > WriteableSize()){
                if (!allow_growth){
                    throw std::length_error(
                            "AsyncBuffer: insufficient capacity"
                            );
                }

                // 确保足够大小的字节
                ToBeEnough(len);
            }
            // 开始写入
            std::copy(data, data + len, &buffer_[write_pos_]);
            write_pos_ += len;
        }
        char *ReadBegin(int len)
        {
            assert(len <= ReadableSize());
            return &buffer_[read_pos_];
        }
        bool IsEmpty() { return write_pos_ == read_pos_; }

        void Swap(Buffer &buf)
        {
            buffer_.swap(buf.buffer_);
            std::swap(read_pos_, buf.read_pos_);
            std::swap(write_pos_, buf.write_pos_);
        }
        size_t Capacity() const {
            return buffer_.size();
        }
        size_t WriteableSize() const
        { // 写空间剩余容量  
            return buffer_.size() - write_pos_;
    
        }
        size_t ReadableSize()
        { // 读空间剩余容量
            return write_pos_ - read_pos_;
        }
        const char *Begin() { return &buffer_[read_pos_]; }
        void MoveWritePos(int len)
        {
            assert(len <= WriteableSize());
            write_pos_ += len;
        }
        void MoveReadPos(int len)
        {
            assert(len <= ReadableSize());
            read_pos_ += len;
        }
        void Reset()
        { // 重置缓冲区
            write_pos_ = 0;
            read_pos_ = 0;
        }
        //  bool --  be written
        // bool be-written ()

    protected:
        void ToBeEnough(size_t len)
        {
            if (len <= WriteableSize()) return;

            // 得到最大写入的容量
            const size_t max_size = buffer_.max_size();

            // if 容量小于 剩余容量返回
            if (len > max_size - write_pos_){
                throw std::length_error(
                        "AsyncBuffer: request too large"
                        );
            }

            // 得到长度
            const size_t required = write_pos_ + len; // 写入后的长度
            const size_t current = buffer_.size();

            size_t target = current;

            // 扩容策略 
            if (current < g_conf_data->threshold)
            {
                // 原项目的三倍容量增长策略
                if (current > max_size / 3)
                    target = max_size;
                else
                    target = current * 3;
            }
            else
            {
                // 原项目的线性增长策略
                const size_t growth =
                    g_conf_data->linear_growth;

                if (growth > max_size - current)
                    target = max_size;
                else
                    target = current + growth;
            }

            // 至少要容纳整条日志
            target = std::max(target, required);

            buffer_.resize(target);


        }


    protected:
        std::vector<char> buffer_; // 缓冲区
        size_t write_pos_;         // 生产者此时的位置
        size_t read_pos_;          // 消费者此时的位置
    };
} // namespace mylog
