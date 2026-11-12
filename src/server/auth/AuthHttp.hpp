#pragma once
#include "AuthService.hpp"
#include <event2/http.h>
#include <event2/buffer.h>
#include <jsoncpp/json/json.h>
#include "RateLimiter.hpp"
#include <memory>
#include "AuthAudit.hpp"
namespace storage::auth {
// Runs on the same libevent thread as the borrowed repositories and connection.
class AuthHttp {
public:
    static void reply(evhttp_request *req,int status,const Json::Value &body) {
        Json::StreamWriterBuilder writer;writer["indentation"]="";
        auto text=Json::writeString(writer,body);
        auto *headers=evhttp_request_get_output_headers(req);
        evhttp_add_header(headers,"Content-Type","application/json; charset=utf-8");
        evhttp_add_header(headers,"Cache-Control","no-store");
        evbuffer_add(evhttp_request_get_output_buffer(req),text.data(),text.size());
        evhttp_send_reply(req,status,nullptr,nullptr);
    }
    static void failure(evhttp_request *req,int status,const char *message) {
        Json::Value body;
        body["error"] = message;
        reply(req,status,body);
    }
    static std::string bearer(evhttp_request *req) {
        const char *header=evhttp_find_header(evhttp_request_get_input_headers(req),"Authorization");
        if(!header)return {};
        std::string value(header);if(value.size()!=71||value.compare(0,7,"Bearer "))return {};
        return value.substr(7);
    }
    static bool handle(evhttp_request *req,const std::string &path,AuthService &auth) {
        if(path!="/api/v1/auth/login" &&
            path!="/api/v1/auth/me" &&
            path!="/api/v1/auth/logout" &&
            path!="/api/v1/auth/register") { return false; }

        const char* operation =
            path == "/api/v1/auth/register" ? "register" :
            path == "/api/v1/auth/login"    ? "login" :
            path == "/api/v1/auth/logout"   ? "logout" :
                                            "me";

        AuthAudit audit(operation);

        // 局部包装器：记录状态，再调用原有静态函数。
        auto reply = [&audit](
            evhttp_request* request,
            int status,
            const Json::Value& body)
        {
            audit.setStatus(status);
            AuthHttp::reply(request, status, body);
        };

        auto failure = [&audit](
            evhttp_request* request,
            int status,
            const char* message)
        {
            audit.setStatus(status);
            AuthHttp::failure(request, status, message);
        };

        try {
            const bool login = path == "/api/v1/auth/login";
            const bool registration = path == "/api/v1/auth/register";
            const bool logout = path == "/api/v1/auth/logout";

            const auto expected = (login || registration || logout)
                ? EVHTTP_REQ_POST
                : EVHTTP_REQ_GET;
            if(evhttp_request_get_command(req)!=expected) {
                evhttp_add_header(evhttp_request_get_output_headers(req),"Allow",expected==EVHTTP_REQ_GET?"GET":"POST");
                failure(req,405,"Method not allowed");return true;
            }

            // 新增一些计时器，用于限制登录和注册请求的频率，防止暴力破解和滥用。
            // static 保证计数器跨请求保留。
            // 不能创建成普通局部变量，否则每个请求都会重新计数。
            static RateLimiter loginLimiter(10);
            static RateLimiter registrationLimiter(5);

            if (login || registration) {
                auto& limiter = registration
                    ? registrationLimiter
                    : loginLimiter;

                const int retryAfter = limiter.check();

                if (retryAfter > 0) {
                    const std::string seconds = std::to_string(retryAfter);

                    evhttp_add_header(
                        evhttp_request_get_output_headers(req),
                        "Retry-After",
                        seconds.c_str());

                    failure(req, 429, "Too many requests");
                    return true;
                }
            }

            // 注册处理
            if (registration) {
                // 先检查原始请求体大小，再解析 JSON。
                auto* buffer = evhttp_request_get_input_buffer(req);
                const auto size = evbuffer_get_length(buffer);

                if (size == 0) {
                    failure(req, 400, "Empty registration body");
                    return true;
                }

                if (size > 4096) {
                    failure(req, 413, "Registration body too large");
                    return true;
                }

                auto* bytes = evbuffer_pullup(buffer, -1);
                if (!bytes) {
                    failure(req, 503, "Registration unavailable");
                    return true;
                }

                Json::CharReaderBuilder builder;
                builder["rejectDupKeys"] = true;
                builder["failIfExtra"] = true;
                builder["allowComments"] = false;
                builder["allowTrailingCommas"] = false;

                std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                Json::Value body;
                std::string errors;

                const char* start = reinterpret_cast<const char*>(bytes);

                // 根节点必须是对象，不能是数组、字符串或 null。
                if (!reader->parse(start, start + size, &body, &errors) ||
                    !body.isObject()) {
                    failure(req, 400, "Invalid registration JSON");
                    return true;
                }

                // username、password 必填且必须是字符串。
                // email 可省略；一旦提供，也必须是字符串。
                if (!body["username"].isString() ||
                    !body["password"].isString() ||
                    (body.isMember("email") && !body["email"].isString())) {
                    failure(req, 400, "Invalid registration fields");
                    return true;
                }

                const std::string username = body["username"].asString();
                const std::string password = body["password"].asString();
                const std::string email = body.isMember("email")
                    ? body["email"].asString()
                    : "";

                const auto result = auth.registerUser(username, password, email);

                switch (result.code) {
                    case AuthCode::Registered: {
                        Json::Value response;
                        response["user"]["id"] = result.userId;
                        response["user"]["username"] = result.username;
                        reply(req, 201, response);
                        break;
                    }

                    case AuthCode::InvalidInput:
                        failure(req, 400, "Invalid registration fields");
                        break;

                    case AuthCode::Conflict:
                        failure(req, 409, "Registration conflict");
                        break;

                    default:
                        // 包括 Unavailable 和意料之外的状态，统一按失败处理。
                        failure(req, 503, "Registration unavailable");
                        break;
                }

                // 注册请求已处理，不能继续进入下面的登录或会话逻辑。
                return true;
            }

            if(login) {
                auto *buffer=evhttp_request_get_input_buffer(req);auto size=evbuffer_get_length(buffer);
                if(size==0||size>4096){failure(req,400,"Invalid login body");return true;}
                auto *bytes=evbuffer_pullup(buffer,-1);
                if(!bytes){failure(req,503,"Authentication unavailable");return true;}
                Json::CharReaderBuilder builder;builder["rejectDupKeys"]=true;builder["failIfExtra"]=true;
                std::unique_ptr<Json::CharReader> reader(builder.newCharReader());Json::Value body;std::string errors;
                const char *start=reinterpret_cast<const char *>(bytes);
                if(!reader->parse(start,start+size,&body,&errors)||!body.isObject()||!body["account"].isString()||!body["password"].isString()) {
                    failure(req,400,"Invalid login body");return true;
                }
                auto result=auth.login(body["account"].asString(),body["password"].asString());
                if(result.code!=AuthCode::Authenticated){failure(req,result.code==AuthCode::Unavailable?503:401,result.code==AuthCode::Unavailable?"Authentication unavailable":"Invalid account or password");return true;}
                Json::Value response;response["user"]["id"]=result.userId;response["user"]["username"]=result.username;
                response["token"]=result.token;response["expiresAt"]=result.expiresAtUtc;reply(req,200,response);
            } else if (logout||path=="/api/v1/auth/me") {
                auto token=bearer(req);db::UserRecord user;
                auto code=logout?auth.logout(token):auth.authenticate(token,user);
                if(code!=AuthCode::Authenticated){failure(req,code==AuthCode::Unavailable?503:401,code==AuthCode::Unavailable?"Authentication unavailable":"Invalid or expired session");return true;}
                Json::Value response;
                if(logout)response["ok"]=true;
                else {response["user"]["id"]=user.id;response["user"]["username"]=user.username;}
                reply(req,200,response);
            }

        } catch(...) {failure(req,503,"Authentication unavailable");}
        return true;
    }
};
}
