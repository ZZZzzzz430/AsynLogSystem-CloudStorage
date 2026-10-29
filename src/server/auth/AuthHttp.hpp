#pragma once
#include "AuthService.hpp"
#include <event2/http.h>
#include <event2/buffer.h>
#include <jsoncpp/json/json.h>
#include <memory>
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
        Json::Value body;body["error"]=message;reply(req,status,body);
    }
    static std::string bearer(evhttp_request *req) {
        const char *header=evhttp_find_header(evhttp_request_get_input_headers(req),"Authorization");
        if(!header)return {};
        std::string value(header);if(value.size()!=71||value.compare(0,7,"Bearer "))return {};
        return value.substr(7);
    }
    static bool handle(evhttp_request *req,const std::string &path,AuthService &auth) {
        if(path!="/api/v1/auth/login"&&path!="/api/v1/auth/me"&&path!="/api/v1/auth/logout")return false;
        try {
            bool login=path=="/api/v1/auth/login", logout=path=="/api/v1/auth/logout";
            auto expected=(login||logout)?EVHTTP_REQ_POST:EVHTTP_REQ_GET;
            if(evhttp_request_get_command(req)!=expected) {
                evhttp_add_header(evhttp_request_get_output_headers(req),"Allow",expected==EVHTTP_REQ_GET?"GET":"POST");
                failure(req,405,"Method not allowed");return true;
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
            } else {
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
