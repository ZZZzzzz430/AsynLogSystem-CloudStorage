
#pragma once

#include <event2/http.h>
#include <event2/buffer.h>

#include <string>
#include <fstream>
#include <iterator>

namespace storage {

class WebPages {
public:
    // true：当前处理器已经处理该请求
    // false：不属于当前处理器，交给后续路由
    static bool handle(evhttp_request* req,
                       const std::string& path)
    {
        // W1：精确匹配静态资源
        const char* filename = nullptr;
        const char* contentType = nullptr;

        if (path == "/login") {
            filename = "web/login.html";
            contentType = "text/html; charset=utf-8";
        }
        else if (path == "/assets/auth.js") {
            filename = "web/assets/auth.js";
            contentType = "text/javascript; charset=utf-8";
        }
        else if (path == "/assets/login.js") {
            filename = "web/assets/login.js";
            contentType = "text/javascript; charset=utf-8";
        }
        else {
            // 不是本类负责的资源
            return false;
        }

        // W2：只允许 GET 方法
        if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
            evkeyvalq* headers =
                evhttp_request_get_output_headers(req);

            evhttp_add_header(headers, "Allow", "GET");

            evhttp_send_reply(
                req,
                405,
                "Method Not Allowed",
                nullptr
            );

            return true;
        }

        // W3：交给文件发送函数
        sendFile(req, filename, contentType);
        return true;
    }

private:
    static void sendFile(evhttp_request* req,
                         const char* filename,
                         const char* contentType)
    {
        // W4：以二进制方式读取文件
        std::ifstream file(filename, std::ios::binary);

        if (!file.is_open()) {
            evhttp_send_reply(
                req,
                500,
                "Internal Server Error",
                nullptr
            );
            return;
        }

        // 将文件内容读取到 std::string
        std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        if (file.bad()) {
            evhttp_send_reply(
                req,
                500,
                "Internal Server Error",
                nullptr
            );
            return;
        }

        // 创建 Libevent 响应缓冲区
        evbuffer* buffer = evbuffer_new();

        if (buffer == nullptr) {
            evhttp_send_reply(
                req,
                500,
                "Internal Server Error",
                nullptr
            );
            return;
        }

        // 将文件内容写入响应缓冲区
        if (evbuffer_add(
                buffer,
                content.data(),
                content.size()) != 0)
        {
            evbuffer_free(buffer);
            evhttp_send_reply(
                req,
                500,
                "Internal Server Error",
                nullptr
            );
            return;
        }

        // W5：设置 HTTP 响应头
        evkeyvalq* headers =
            evhttp_request_get_output_headers(req);

        evhttp_add_header(
            headers, "Content-Type", contentType
        );

        evhttp_add_header(
            headers, "Cache-Control", "no-store"
        );

        evhttp_add_header(
            headers, "X-Content-Type-Options", "nosniff"
        );

        // 发送 HTTP 200 响应
        evhttp_send_reply(req, 200, "OK", buffer);

        // 释放本函数创建的缓冲区
        evbuffer_free(buffer);
    }
};

} // namespace storage

