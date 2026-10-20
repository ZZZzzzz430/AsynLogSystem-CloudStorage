#pragma once
#include "DataManager.hpp"

#include <sys/queue.h>
#include <event.h>
// for http
#include <evhttp.h>
#include <event2/http.h>

#include <fcntl.h>
#include <sys/stat.h>

#include <regex>
#include <stdexcept>
#include <utility>

#include "base64.h" // 来自 cpp-base64 库
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <cstdio>

extern storage::DataManager *data_;
namespace storage
{
    class ScopeTimer
    {
    public:
        using Clock = std::chrono::steady_clock;

        // BUG-S01：构造函数、复制声明必须与 ScopeTimer 类名一致。
        explicit ScopeTimer(std::string path)
            : path_(std::move(path)), start_time_(Clock::now()),
              thread_id_(std::this_thread::get_id()) {}
        ScopeTimer(const ScopeTimer&) = delete;
        ScopeTimer& operator=(const ScopeTimer&) = delete;

        ~ScopeTimer() noexcept
        {
            // BUG-S02：try/catch 使用合法语法，析构中的异常不能向外传播。
            try
            {
                const auto cost_ms = std::chrono::duration_cast<
                    std::chrono::milliseconds>(Clock::now() - start_time_).count();
                std::ostringstream message;
                message << "callback path=" << path_ << " cost_ms=" << cost_ms
                        << " thread=" << thread_id_;
                const std::string text = message.str();
                auto logger = mylog::GetLogger("asynclogger");
                if (!logger)
                    throw std::runtime_error("callback logger unavailable");

                // BUG-S03：接入项目日志；每次回调按耗时只记录一个等级。
                if (cost_ms > kSlowCallbackMs)
                    logger->Warn("%s", text.c_str());
                else
                    logger->Info("%s", text.c_str());
            }
            catch (...)
            {
                // 不再次调用日志器，避免失败后递归；C 标准错误输出不抛 C++ 异常。
                std::fputs("[callback-timer] failed to record callback duration\n", stderr);
            }
        }

    private:
        static constexpr long long kSlowCallbackMs = 100;
        std::string path_;
        Clock::time_point start_time_;
        std::thread::id thread_id_;
    };

    class Service
    {
    public:
        Service()
        {
#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("Service start(Construct)");
#endif
            server_port_ = Config::GetInstance()->GetServerPort();
            server_ip_ = Config::GetInstance()->GetServerIp();
            download_prefix_ = Config::GetInstance()->GetDownloadPrefix();
#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("Service end(Construct)");
#endif
        }
        bool RunModule()
        {
            // 初始化环境
            event_base *base = event_base_new();
            if (base == NULL)
            {
                mylog::GetLogger("asynclogger")->Fatal("event_base_new err!");
                return false;
            }
            // 设置监听的端口和地址
            sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_port = htons(server_port_);
            // http 服务器,创建evhttp上下文
            evhttp *httpd = evhttp_new(base);
            // 绑定端口和ip
            if (evhttp_bind_socket(httpd, "0.0.0.0", server_port_) != 0)
            {
                mylog::GetLogger("asynclogger")->Fatal("evhttp_bind_socket failed!");
                return false;
            }
            // 设定回调函数
            // 指定generic callback，也可以为特定的URI指定callback
            // 让已知 HTTP 方法进入路由，由上传入口统一返回 405 和 Allow。
            evhttp_set_allowed_methods(httpd,
                EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD |
                EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE | EVHTTP_REQ_OPTIONS |
                EVHTTP_REQ_TRACE | EVHTTP_REQ_CONNECT | EVHTTP_REQ_PATCH);
            evhttp_set_gencb(httpd, GenHandler, NULL);

            if (base)
            {
#ifdef DEBUG_LOG
                mylog::GetLogger("asynclogger")->Debug("event_base_dispatch");
#endif
                if (-1 == event_base_dispatch(base))
                {
                    mylog::GetLogger("asynclogger")->Debug("event_base_dispatch err");
                }
            }
            if (base)
                event_base_free(base);
            if (httpd)
                evhttp_free(httpd);
            return true;
        }

    private:
        uint16_t server_port_;
        std::string server_ip_;
        std::string download_prefix_;

    private:
        static void GenHandler(struct evhttp_request *req, void *arg)
        {
            // BUG-S04：URI/path 指针可能为空；计时与路由使用同一份解码路径。
            const auto* uri = evhttp_request_get_evhttp_uri(req);
            const char* raw_path = uri ? evhttp_uri_get_path(uri) : nullptr;
            std::string path = raw_path && *raw_path ? raw_path : "/";
            path = UrlDecode(path);
            ScopeTimer timer(path);

            mylog::GetLogger("asynclogger")->Info("get req, uri: %s", path.c_str());

            // 根据请求中的内容判断是什么请求
            // 这里是下载请求
            //  TODO 路由转发
            if (path.find("/download/") != std::string::npos)
            {
                Download(req, arg);
            }
            // 这里是上传
            else if (path == "/upload")
            {
                Upload(req, arg);
            }
            // 这里就是显示已存储文件列表，返回一个html页面给浏览器
            else if (path == "/")
            {
                ListShow(req, arg);
            }
            else
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "Not Found", NULL);
            }
        }


        struct UploadParams
        {
            std::string filename;
            std::string storageType;
            size_t contentLength = 0;
        };

        enum class UploadError
        {
            None,
            MethodNotAllowed,
            EmptyBody,
            MissingHeader,
            InvalidStorageType,
            InvalidFilename,
            PayloadTooLarge
        };

        static bool IsValidFilename(const std::string& filename)
        {
            if (filename.empty() || filename == "." || filename == "..")
                return false;
            if (filename.find_first_of("/\\") != std::string::npos)
                return false;
            if (filename.find('\0') != std::string::npos)
                return false;
            return true;
        }

        static UploadError ValidateUploadRequest(
            struct evhttp_request* req, UploadParams& params)
        {
            if (evhttp_request_get_command(req) != EVHTTP_REQ_POST)
                return UploadError::MethodNotAllowed;

            struct evbuffer* buf = evhttp_request_get_input_buffer(req);
            if (!buf)
                return UploadError::EmptyBody;
            size_t len = evbuffer_get_length(buf);
            if (len == 0)
                return UploadError::EmptyBody;

            constexpr size_t MAX_SIZE = 1024 * 1024;
            if (len > MAX_SIZE)
                return UploadError::PayloadTooLarge;

            struct evkeyvalq* headers = evhttp_request_get_input_headers(req);
            const char* name = evhttp_find_header(headers, "FileName");
            const char* type = evhttp_find_header(headers, "StorageType");
            if (!name || !type || !*name || !*type)
                return UploadError::MissingHeader;

            std::string storage_type(type);
            if (storage_type != "low" && storage_type != "deep")
                return UploadError::InvalidStorageType;

            std::string filename;
            try
            {
                filename = base64_decode(std::string(name));
            }
            catch (const std::runtime_error&)
            {
                return UploadError::InvalidFilename;
            }
            catch (const std::out_of_range&)
            {
                return UploadError::InvalidFilename;
            }
            if (!IsValidFilename(filename))
                return UploadError::InvalidFilename;

            params.filename = std::move(filename);
            params.storageType = std::move(storage_type);
            params.contentLength = len;
            return UploadError::None;
        }

        static void ReplyUploadError(struct evhttp_request* req, UploadError error)
        {
            int status = HTTP_BADREQUEST;
            const char* reason = "Bad Request";
            switch (error)
            {
            case UploadError::MethodNotAllowed:
                status = 405;
                reason = "Method Not Allowed";
                evhttp_add_header(evhttp_request_get_output_headers(req), "Allow", "POST");
                break;
            case UploadError::PayloadTooLarge:
                status = 413;
                reason = "Payload Too Large";
                break;
            case UploadError::EmptyBody:
                reason = "Empty Body";
                break;
            case UploadError::MissingHeader:
                reason = "Missing Upload Header";
                break;
            case UploadError::InvalidStorageType:
                reason = "Invalid Storage Type";
                break;
            case UploadError::InvalidFilename:
                reason = "Invalid Filename";
                break;
            case UploadError::None:
                return;
            }
            evhttp_send_reply(req, status, reason, nullptr);
        }

        static void Upload(struct evhttp_request *req, void *arg)
        {
            UploadParams params;
            UploadError error = ValidateUploadRequest(req, params);
            if (error != UploadError::None)
            {
                ReplyUploadError(req, error);
                return;
            }

            mylog::GetLogger("asynclogger")->Info("Upload start");
            struct evbuffer* buf = evhttp_request_get_input_buffer(req);
            const size_t len = params.contentLength;
            mylog::GetLogger("asynclogger")->Info("evbuffer_get_length is %zu", len);
            std::string content(len, '\0');
            if (evbuffer_copyout(buf, content.data(), len) != static_cast<ev_ssize_t>(len))
            {
                mylog::GetLogger("asynclogger")->Error("evbuffer_copyout error");
                evhttp_send_reply(req, HTTP_INTERNAL, nullptr, nullptr);
                return;
            }

            const std::string& filename = params.filename;
            const std::string& storage_type = params.storageType;
            std::string storage_path = storage_type == "low"
                ? Config::GetInstance()->GetLowStorageDir()
                : Config::GetInstance()->GetDeepStorageDir();

            // 如果不存在就创建low或deep目录
            FileUtil dirCreate(storage_path);
            dirCreate.CreateDirectory();

            // 目录创建后加可以加上文件名，这个就是最终要写入的文件路径
            storage_path += filename;
#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("storage_path:%s", storage_path.c_str());
#endif

            // 看路径里是low还是deep存储，是deep就压缩，是low就直接写入
            FileUtil fu(storage_path);
            if (storage_path.find("low_storage") != std::string::npos)
            {
                if (fu.SetContent(content.c_str(), len) == false)
                {
                    mylog::GetLogger("asynclogger")->Error("low_storage fail, evhttp_send_reply: HTTP_INTERNAL");
                    evhttp_send_reply(req, HTTP_INTERNAL, "server error", NULL);
                    return;
                }
                else
                {
                    mylog::GetLogger("asynclogger")->Info("low_storage success");
                }
            }
            else
            {
                if (fu.Compress(content, Config::GetInstance()->GetBundleFormat()) == false)
                {
                    mylog::GetLogger("asynclogger")->Error("deep_storage fail, evhttp_send_reply: HTTP_INTERNAL");
                    evhttp_send_reply(req, HTTP_INTERNAL, "server error", NULL);
                    return;
                }
                else
                {
                    mylog::GetLogger("asynclogger")->Info("deep_storage success");
                }
            }

            // 添加存储文件信息，交由数据管理类进行管理
            StorageInfo info;
            info.NewStorageInfo(storage_path); // 组织存储的文件信息
            data_->Insert(info);               // 向数据管理模块添加存储的文件信息

            evhttp_send_reply(req, HTTP_OK, "Success", NULL);
            mylog::GetLogger("asynclogger")->Info("upload finish:success");
        }

        static std::string TimetoStr(time_t t)
        {
            std::string tmp = std::ctime(&t);
            return tmp;
        }

        // 前端代码处理函数
        // 在渲染函数中直接处理StorageInfo
        static std::string generateModernFileList(const std::vector<StorageInfo> &files)
        {
            std::stringstream ss;
            ss << "<div class='file-list'><h3>已上传文件</h3>";

            for (const auto &file : files)
            {
                std::string filename = FileUtil(file.storage_path_).FileName();

                // 从路径中解析存储类型（示例逻辑，需根据实际路径规则调整）
                std::string storage_type = "low";
                if (file.storage_path_.find("deep") != std::string::npos)
                {
                    storage_type = "deep";
                }

                ss << "<div class='file-item'>"
                   << "<div class='file-info'>"
                   << "<span>📄" << filename << "</span>"
                   << "<span class='file-type'>"
                   << (storage_type == "deep" ? "深度存储" : "普通存储")
                   << "</span>"
                   << "<span>" << formatSize(file.fsize_) << "</span>"
                   << "<span>" << TimetoStr(file.mtime_) << "</span>"
                   << "</div>"
                   << "<button onclick=\"window.location='" << file.url_ << "'\">⬇️ 下载</button>"
                   << "</div>";
            }

            ss << "</div>";
            return ss.str();
        }

        // 文件大小格式化函数
        static std::string formatSize(uint64_t bytes)
        {
            const char *units[] = {"B", "KB", "MB", "GB"};
            int unit_index = 0;
            double size = bytes;

            while (size >= 1024 && unit_index < 3)
            {
                size /= 1024;
                unit_index++;
            }

            std::stringstream ss;
            ss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
            return ss.str();
        }
        static void ListShow(struct evhttp_request *req, void *arg)
        {
            mylog::GetLogger("asynclogger")->Info("ListShow()");
            // 1. 获取所有的文件存储信息
            std::vector<StorageInfo> arry;
            data_->GetAll(&arry);

            // 读取模板文件
            std::ifstream templateFile("index.html");
            std::string templateContent(
                (std::istreambuf_iterator<char>(templateFile)),
                std::istreambuf_iterator<char>());

            // 替换html文件中的占位符
            //替换文件列表进html
            templateContent = std::regex_replace(templateContent,
                                                 std::regex("\\{\\{FILE_LIST\\}\\}"),
                                                 generateModernFileList(arry));
            //替换服务器地址进hrml
            templateContent = std::regex_replace(templateContent,
                                                 std::regex("\\{\\{BACKEND_URL\\}\\}"),
                                                "http://"+storage::Config::GetInstance()->GetServerIp()+":"+std::to_string(storage::Config::GetInstance()->GetServerPort()));
            // 获取请求的输出evbuffer
            struct evbuffer *buf = evhttp_request_get_output_buffer(req);
            auto response_body = templateContent;
            // 把前面的html数据给到evbuffer，然后设置响应头部字段，最后返回给浏览器
            evbuffer_add(buf, (const void *)response_body.c_str(), response_body.size());
            evhttp_add_header(req->output_headers, "Content-Type", "text/html;charset=utf-8");
            evhttp_send_reply(req, HTTP_OK, NULL, NULL);
            mylog::GetLogger("asynclogger")->Info("ListShow() finish");
        }
        static std::string GetETag(const StorageInfo &info)
        {
            // 自定义etag :  filename-fsize-mtime
            FileUtil fu(info.storage_path_);
            std::string etag = fu.FileName();
            etag += "-";
            etag += std::to_string(info.fsize_);
            etag += "-";
            etag += std::to_string(info.mtime_);
            return etag;
        }
        static void Download(struct evhttp_request *req, void *arg)
        {
            // 1. 获取客户端请求的资源路径path   req.path
            // 2. 根据资源路径，获取StorageInfo
            StorageInfo info;
            std::string resource_path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            resource_path = UrlDecode(resource_path);
            data_->GetOneByURL(resource_path, &info);
            mylog::GetLogger("asynclogger")->Info("request resource_path:%s", resource_path.c_str());

            std::string download_path = info.storage_path_;
            // 2.如果压缩过了就解压到新文件给用户下载
            if (info.storage_path_.find(Config::GetInstance()->GetLowStorageDir()) == std::string::npos)
            {
                mylog::GetLogger("asynclogger")->Info("uncompressing:%s", info.storage_path_.c_str());
                FileUtil fu(info.storage_path_);
                download_path = Config::GetInstance()->GetLowStorageDir() +
                                std::string(download_path.begin() + download_path.find_last_of('/') + 1, download_path.end());
                FileUtil dirCreate(Config::GetInstance()->GetLowStorageDir());
                dirCreate.CreateDirectory();
                fu.UnCompress(download_path); // 将文件解压到low_storage下去或者再创一个文件夹做中转
            }
            mylog::GetLogger("asynclogger")->Info("request download_path:%s", download_path.c_str());
            FileUtil fu(download_path);
            if (fu.Exists() == false && info.storage_path_.find("deep_storage") != std::string::npos)
            {
                // 如果是压缩文件，且解压失败，是服务端的错误
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: 500 - UnCompress failed");
                evhttp_send_reply(req, HTTP_INTERNAL, NULL, NULL);
            }
            else if (fu.Exists() == false && info.storage_path_.find("low_storage") == std::string::npos)
            {
                // 如果是普通文件，且文件不存在，是客户端的错误
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: 400 - bad request,file not exists");
                evhttp_send_reply(req, HTTP_BADREQUEST, "file not exists", NULL);
            }

            // 3.确认文件是否需要断点续传
            bool retrans = false;
            std::string old_etag;
            auto if_range = evhttp_find_header(req->input_headers, "If-Range");
            if (NULL != if_range)
            {
                old_etag = if_range;
                // 有If-Range字段且，这个字段的值与请求文件的最新etag一致则符合断点续传
                if (old_etag == GetETag(info))
                {
                    retrans = true;
                    mylog::GetLogger("asynclogger")->Info("%s need breakpoint continuous transmission", download_path.c_str());
                }
            }

            // 4. 读取文件数据，放入rsp.body中
            if (fu.Exists() == false)
            {
                mylog::GetLogger("asynclogger")->Info("%s not exists", download_path.c_str());
                download_path += "not exists";
                evhttp_send_reply(req, 404, download_path.c_str(), NULL);
                return;
            }
            evbuffer *outbuf = evhttp_request_get_output_buffer(req);
            int fd = open(download_path.c_str(), O_RDONLY);
            if (fd == -1)
            {
                mylog::GetLogger("asynclogger")->Error("open file error: %s -- %s", download_path.c_str(), strerror(errno));
                evhttp_send_reply(req, HTTP_INTERNAL, strerror(errno), NULL);
                return;
            }
            // 和前面用的evbuffer_add类似，但是效率更高，具体原因可以看函数声明
            if (-1 == evbuffer_add_file(outbuf, fd, 0, fu.FileSize()))
            {
                mylog::GetLogger("asynclogger")->Error("evbuffer_add_file: %d -- %s -- %s", fd, download_path.c_str(), strerror(errno));
            }
            // 5. 设置响应头部字段： ETag， Accept-Ranges: bytes
            evhttp_add_header(req->output_headers, "Accept-Ranges", "bytes");
            evhttp_add_header(req->output_headers, "ETag", GetETag(info).c_str());
            evhttp_add_header(req->output_headers, "Content-Type", "application/octet-stream");
            if (retrans == false)
            {
                evhttp_send_reply(req, HTTP_OK, "Success", NULL);
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_OK");
            }
            else
            {
                evhttp_send_reply(req, 206, "breakpoint continuous transmission", NULL); // 区间请求响应的是206
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: 206");
            }
            if (download_path != info.storage_path_)
            {
                remove(download_path.c_str()); // 删除文件
            }
        }
    };
}
