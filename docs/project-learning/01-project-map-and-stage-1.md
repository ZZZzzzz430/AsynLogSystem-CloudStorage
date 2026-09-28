# 项目工程地图与第一阶段任务

整理日期：2026-09-21。

依据：项目根目录的 [AGENT.md](../../AGENT.md)，以及服务端、日志库、备份服务和客户端源码。

本文对应首次项目地图回答。分析时仅阅读代码，没有实际启动服务。下文的线程数量为源码推导，实验结果需要运行验证。

## 一、项目工程地图

项目是一个以异步日志为基础的文件存储服务，有四个入口：

| 入口 | 文件 | 用途 |
|---|---|---|
| 存储服务端 | [src/server/Test.cpp](../../src/server/Test.cpp) | 初始化日志、加载元数据、启动 HTTP |
| 日志示例 | [log_system/examples/test.cpp](../../log_system/examples/test.cpp) | 单独触发各级日志 |
| 日志备份服务 | [ServerBackupLog.cpp](../../log_system/logs_code/backlog/ServerBackupLog.cpp) | 接收 TCP 日志并保存 |
| 目录上传客户端 | [src/client/Test.cpp](../../src/client/Test.cpp) | 扫描目录，上传新增或变化的文件 |

浏览器页面位于 `src/server/index.html`。第一轮联调建议使用浏览器或 curl：现有 C++ 客户端直接发送文件名，服务端却按 Base64 解码，协议尚未统一；客户端还存在 `Sleep(1)` 的平台兼容问题。

### 1. 主要模块及依赖

以下路径均相对于项目根目录。

| 模块 | 核心文件 | 负责什么、依赖什么 |
|---|---|---|
| 日志配置与基础信息 | `log_system/logs_code/{Util,Level,Message}.hpp` | 加载日志配置，记录级别、时间、调用线程和正文 |
| 日志接口与管理 | `log_system/logs_code/{MyLog,Manager,AsyncLogger}.hpp` | 获取日志器、格式化消息、提交日志；依赖消息、异步工作器和输出器 |
| 日志内存缓冲 | `log_system/logs_code/AsyncBuffer.hpp` | 使用 `vector<char>` 保存字节，管理读写位置和容量 |
| 异步消费 | `log_system/logs_code/AsyncWorker.hpp` | 互斥锁、条件变量、双缓冲交换和后台线程 |
| 日志输出 | `log_system/logs_code/LogFlush.hpp` | 标准输出、文件输出、滚动文件；涉及 `FILE*` 和刷盘 |
| 远程日志备份 | `log_system/logs_code/ThreadPoll.hpp`、`log_system/logs_code/backlog/*` | 任务队列、线程池、TCP 发送和接收 |
| 存储配置与文件工具 | `src/server/{Config,Util}.hpp` | 配置、文件读写、JSON、bundle 压缩与解压 |
| 元数据管理 | `src/server/DataManager.hpp` | URL → 文件信息的哈希表，读写锁，JSON 持久化 |
| HTTP 业务 | `src/server/Service.hpp` | Libevent 事件循环，上传、列表、下载 |
| 上传端 | `src/server/index.html`、`src/client/*` | 浏览器上传和目录扫描上传 |

```text
HTTP 业务
├── 元数据管理 → 文件工具、JSON、配置
├── 文件工具 → 文件系统、bundle
└── 日志接口
    ├── LogMessage → 格式化字符串
    ├── AsyncWorker → Buffer → LogFlush → 日志文件
    └── ERROR/FATAL → ThreadPool → TCP 备份服务
```

文件工具和存储配置也会记录日志，所以日志初始化必须早于存储模块初始化。

### 2. 整体启动流程

真实调用链在 `src/server/Test.cpp`：

```text
main()                                              [主线程]
│
├─ log_system_module_init()
│  ├─ JsonData::GetJsonData()                        读取日志配置
│  ├─ new ThreadPool(thread_count)                   创建备份线程池
│  ├─ LoggerBuilder                                 配置滚动文件输出
│  └─ LoggerManager::GetInstance().AddLogger(...)
│     ├─ 管理器构造默认日志器                        创建默认日志后台线程
│     └─ 构建并注册 asynclogger                      创建业务日志后台线程
│
├─ new storage::DataManager()
│  ├─ Config::GetInstance()                          读取 Storage.conf
│  └─ InitLoad()                                    加载 storage.data
│
├─ thread t1(service_module)                        创建 HTTP 线程
│  └─ Service::RunModule()
│     ├─ event_base_new / evhttp_new
│     ├─ evhttp_bind_socket                         绑定 0.0.0.0:8081
│     ├─ evhttp_set_gencb(..., GenHandler, ...)
│     └─ event_base_dispatch()
│        └─ GenHandler → Upload / ListShow / Download
│
└─ t1.join()                                        主线程等待
```

配置中的 `server_ip` 为 `127.0.0.1`，但监听代码实际写死了 `0.0.0.0`。配置值与实际行为需要分别确认。

### 3. 线程、队列和共享资源

当前日志配置 `thread_count = 3`，正常启动后预计存储服务进程有 **7 个线程**：

```text
主线程 × 1                  等待 t1.join()
HTTP 事件循环线程 × 1       处理所有 HTTP 业务回调
日志后台线程 × 2            默认日志器、asynclogger 各一个
备份线程池线程 × 3          等待或执行 TCP 备份任务
```

备份接收服务是独立进程：主线程 `accept()`，每个连接创建一个分离的 pthread。

| 共享资源 | 访问者及保护方式 |
|---|---|
| 日志器管理表 `loggers_` | `LoggerManager::mtx_` 保护 |
| 生产者 Buffer | 多个日志调用线程、日志后台线程通过 `AsyncWorker::mtx_` 协调 |
| 消费者 Buffer | 交换后由后台线程输出，再重置 |
| 备份任务队列 `tasks` | `queue_mutex` 与条件变量保护 |
| 元数据表 `table_` | POSIX 读写锁保护 |
| `storage.data` | 持久化写文件没有与整次更新绑定的事务或专用写锁 |
| 上传文件、解压文件 | 使用路径和文件名定位，没有独立的文件级并发保护 |

日志通道是双 Buffer，不是逐条日志的 `std::queue`。明确存在的任务队列在备份线程池里。

### 4. 主要数据流与 IO

#### 普通日志

```text
调用线程：Info → vasprintf → LogMessage::format
         → std::string → AsyncWorker::Push → 生产者 Buffer

后台线程：ThreadEntry → Swap → RealFlush
         → RollFileFlush::Flush → fwrite → fflush → fsync
```

格式化和复制发生在调用线程；交换缓冲区时持锁，写日志文件时已释放该锁。当前 `flush_log = 2`，输出路径会调用 `fflush` 和 `fsync`。

#### ERROR / FATAL

在进入本地日志 Buffer 前，先 `tp->enqueue(start_backup, data)`，然后 `future.get()`。因此，调用方会等待备份任务完成，网络慢可能直接拖慢 HTTP 回调。

#### 文件上传

```text
HTTP 请求体 evbuffer
→ 完整复制到 std::string content
→ 普通写文件 / bundle 压缩后写文件
→ StorageInfo
→ table_
→ 全量 JSON 序列化
→ storage.data
```

上传写文件、压缩、元数据持久化都在 HTTP 事件循环线程执行。

#### 文件下载

```text
URL → 查询元数据
    → 普通文件直接打开
    → 深度文件先读入、解压并写入 low_storage
    → open → evbuffer_add_file → HTTP 响应
```

代码没有完整解析 `Range`，目前不能把返回 206 当作完整断点续传实现。

可直接从源码确认的系统接口包括 `socket/connect/read/write/close`、`open`、`stat`、`mkdir`、`fsync`。文件流和 Libevent 底层实际使用哪些读写或事件等待系统调用，应通过 `strace` 验证；不能仅凭 `evbuffer_add_file` 就认定运行时一定使用了 `sendfile`。

### 5. 最值得追踪的风险与面试点

| 位置 | 后续需要证明的问题 |
|---|---|
| HTTP 回调中的写盘、压缩 | 一个慢请求是否让其他请求一起等待？ |
| `AsyncLogger::serialize` | 为什么使用线程池后，错误日志仍可能阻塞业务？ |
| `AsyncWorker::Push` | 固定模式下，单条日志超过整个 Buffer 容量会怎样？ |
| `Buffer::ToBeEnough` | 一次扩容是否足够容纳任意长度的数据？ |
| `AsyncWorker::Stop` | 停止时是否排空数据，是否唤醒所有等待者，能否重复调用？ |
| `DataManager::Storage` | 哈希表加锁是否意味着持久化也安全？并发更新时如何验证？ |
| 深度下载的中转文件 | 与普通存储同名时会不会覆盖、删除其他文件？ |
| 备份服务 `service` | 一次 `read` 能否收完日志？读满 1024 字节时数组边界是否正确？ |

当前 HTTP 回调在同一线程执行，因此多个请求首先可能表现为排队和阻塞。元数据的多线程持久化风险，应与未来并行调用场景分开分析。

## 二、推荐学习路线

| 阶段 | 学什么、为什么这样安排 | 前置依赖 | 实操与应能回答的问题 |
|---|---|---|---|
| 1 | 启动、配置、运行基线；先建立可观察对象 | 无 | GDB 跟踪初始化、确认线程和监听；“程序如何真正启动？” |
| 2 | `Level / LogMessage`；先理解日志数据 | 1 | 构造一条日志、增加字段；“时间和线程 ID 在哪里产生？” |
| 3 | `Buffer`；为线程同步准备数据结构基础 | 2 | 小容量读写、边界故障、容量统计；“容量和有效数据长度有什么区别？” |
| 4 | `AsyncWorker`；理解双缓冲和等待条件 | 3 | 多生产者、慢消费者、停止实验；“谁阻塞、谁唤醒？” |
| 5 | 日志器、管理器与输出器；贯通日志链路 | 2—4 | 滚动文件、刷盘观察、写入失败；“日志调用返回意味着什么？” |
| 6 | 线程池与 TCP 备份；追踪日志的网络分支 | 4—5 | 断开备份服务、长日志实验；“线程池为什么没消除等待？” |
| 7 | 文件工具、配置、元数据持久化 | 5 | 保存、重启加载、失败恢复；“文件和索引如何保持一致？” |
| 8 | Libevent、路由、普通上传与列表 | 7 | curl 上传、缺失请求头、并发请求；“回调内慢操作影响谁？” |
| 9 | 压缩存储、下载与客户端协议 | 8 | 文件校验、同名文件、Range 请求；“下载正确如何证明？” |
| 10 | 综合并发、资源与性能分析 | 前面各阶段 | 1/2/6/100 并发分档实验；“如何区分 CPU 忙、IO 慢和线程等待？” |

## 三、第一阶段任务

阶段 1 的目标：能用断点、线程列表和系统观察，证明服务从哪里启动、读了什么配置、创建了哪些线程。

拆成四个小块：

1. **初始化顺序与工作目录**：配置文件怎么找到，日志为何先启动。
2. 线程创建与等待：核对预计的 7 个线程及调用栈。
3. HTTP 最小闭环：访问 `/`，跟踪 `GenHandler → ListShow`。
4. 启动诊断小改造：输出实际配置路径与监听地址，再重新验证。

现在只进入学习块 1。

### 阶段 1 · 学习块 1：初始化顺序与工作目录

这个小块解决一个工程问题：同一个二进制，为什么换个目录启动，行为可能不同？

只看三个位置：

- [Test.cpp](../../src/server/Test.cpp)：`main()`、`log_system_module_init()`。
- [日志 Util.hpp](../../log_system/logs_code/Util.hpp)：`JsonData` 构造函数。
- [Config.hpp](../../src/server/Config.hpp)：`ReadConfig()`。

这里有两条相对路径：

```text
日志配置：../../log_system/logs_code/config.conf
存储配置：Storage.conf
```

它们相对于进程当前工作目录解析。

#### 正常实验

```bash
cd /home/zhang/code/Poject/AsynLogSystem/src/server
make gdb_test
gdb ./gdb_test
```

不要执行 `make clean`：当前清理规则还会删除存储目录、日志和 `storage.data`。

在 GDB 中：

```gdb
set pagination off
break main
break log_system_module_init
break storage::Config::ReadConfig
run
bt
next
step
```

进入日志初始化后，用 `next` 执行完 `g_conf_data = ...` 这一行，再观察：

```gdb
p g_conf_data->buffer_size
p g_conf_data->thread_count
p g_conf_data->flush_log
```

预期分别为 `10000000`、`3`、`2`。随后：

```gdb
continue
bt
p storage::Config_File
```

应在 `ReadConfig()` 命中断点，调用栈能说明它由元数据初始化触发。这个小块不需要发送 HTTP 请求。

#### Linux 观察实验

可以单独运行：

```bash
strace -f -e trace=openat,newfstatat -o /tmp/asynlog-startup.trace ./gdb_test
```

观察完成后按 Ctrl-C，然后：

```bash
rg 'config.conf|Storage.conf|storage.data' /tmp/asynlog-startup.trace
```

重点看文件路径与返回值：成功打开返回非负 fd；文件不存在通常显示 `ENOENT`。`storage.data` 不存在是代码允许的首次启动情况，不能与配置缺失混为一谈。

#### 安全故障实验

切换到项目根目录，但只运行至配置解析前。

```bash
cd /home/zhang/code/Poject/AsynLogSystem
gdb ./src/server/gdb_test
```

```gdb
break mylog::Util::JsonUtil::UnSerialize
run
bt
p str
kill
quit
```

先回答：“现在已知现象是什么？”记录报错路径、`str` 内容和调用栈，再验证“工作目录导致相对路径失效”这一假设。回到 `src/server` 重复正常实验即可恢复，无需删除或改动配置文件。

#### 本块交付物与面试问题

- 一条实际初始化调用链。
- 三个日志配置值及观察位置。
- 正常与错误工作目录下的配置读取证据。

最后请先用证据回答三个问题：

1. 为什么 `DataManager` 必须在日志初始化之后创建？
2. 为什么二进制路径正确，配置文件仍可能打不开？
3. 如何证明失败发生在读配置阶段，而不是 HTTP 监听阶段？

完成这些观察后再评估本块是否通过，暂不进入学习块 2。
