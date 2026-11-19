# 阶段 3 · 学习块 1：异步日志交接与固定缓冲反压

日期：2026-10-09。依据：当前仓库源码及用户报告。06 的实验由用户报告完成；本次不重复验收，也不将其记作助手实测。本次完成源码梳理与任务布置，下面的实验、改造尚未执行。

文档序号不代表项目阶段号。07 接续 06，进入此前约定的异步日志主链路。时间盒约 2～3 小时：读链路 30 分钟、正常调试 30 分钟、故障定位与小改造 60 分钟、回归与复盘 30 分钟。本次只聚焦日志交接和超容量请求，停止排空、备份可靠性留到后续。

## 1. 先想清楚它解决什么问题

上传回调需要记录日志。如果它直接执行文件写入和 `fsync`，日志磁盘延迟就进入 HTTP 回调耗时，其他请求也可能等在同一个事件循环后面。

本项目让调用线程先格式化日志并复制到生产者 Buffer，后台线程接手输出。它减少业务线程直接等待日志磁盘的机会，但格式化、查找日志器、加锁和缓冲区等待仍发生在调用线程。后台消费赶不上生产时，缓冲区会积压。

本块要用证据回答：`Info()` 返回时，到底完成了哪一步？后台线程在哪里开始拥有这些字节？如果它消费得很慢，HTTP 线程会停在哪里？

## 2. 源码范围、对象与生命周期

| 源码 | 本次只读这些位置 |
|---|---|
| [Test.cpp](../../../src/server/Test.cpp) | `log_system_module_init / service_module / main` |
| [MyLog.hpp](../../../log_system/logs_code/MyLog.hpp) | `GetLogger`、`Info` 宏补入文件名和行号 |
| [Manager.hpp](../../../log_system/logs_code/Manager.hpp) | `GetInstance / GetLogger`、默认日志器创建 |
| [AsyncLogger.hpp](../../../log_system/logs_code/AsyncLogger.hpp) | `Info / serialize / Flush / RealFlush`、`LoggerBuilder::Build` |
| [Message.hpp](../../../log_system/logs_code/Message.hpp) | `LogMessage` 构造与 `format` |
| [AsyncWorker.hpp](../../../log_system/logs_code/AsyncWorker.hpp) | `Push / ThreadEntry / Stop` |
| [AsyncBuffer.hpp](../../../log_system/logs_code/AsyncBuffer.hpp) | `Push / Swap / Reset / ToBeEnough` |
| [LogFlush.hpp](../../../log_system/logs_code/LogFlush.hpp) | `RollFileFlush::Flush` |

`LoggerBuilder` 在主线程创建；构建的 `AsyncLogger` 由 `LoggerManager` 的 `shared_ptr` 保存，调用处获取共享所有权。`AsyncLogger` 的 `logger_name_` 标识日志器，`flushs_` 保存输出器，`asyncworker` 持有异步工作器。它的 `mtx_` 在当前主链路中没有被使用，不能把它说成日志缓冲区的保护锁。

`AsyncWorker` 构造时启动自己的 `thread_`，析构时调用 `Stop()`。关键成员：

| 成员 | 保存什么、由谁访问 |
|---|---|
| `buffer_productor_` | 调用线程在 `Push` 中追加；后台在线程入口中交换 |
| `buffer_consumer_` | 后台交换取得的数据，交给回调输出，然后重置 |
| `mtx_` | 保护生产者追加和双缓冲交换；写文件时不持有它 |
| `cond_consumer_` | 后台等待“停止或生产者有数据” |
| `cond_productor_` | 固定模式生产者等待“剩余空间足以容纳本条数据” |
| `callback_` | 绑定当前日志器的 `RealFlush`，捕获的是 `this` |
| `stop_` | 原子停止标志；原子变量本身不等于整个退出过程正确 |
| `async_type_` | `ASYNC_SAFE` 等待空间，`ASYNC_UNSAFE` 不经过该等待 |

`Buffer` 的 `buffer_` 是 `vector<char>`，`write_pos_ / read_pos_` 界定有效字节。本次消费将整块输出再 `Reset`，不是逐条取出日志；没有另外一个 `queue<日志>`。

生命周期需要知道：输出器必须在后台回调期间存活，工作器必须在相关成员销毁前结束。当前正常服务没有完整的优雅退出入口；不能把终端 Ctrl-C 当作执行了析构排空。本次不修改退出协议。

## 3. 真实调用链与数据流

以合法上传中的 `Info("Upload start")` 为例：

```text
HTTP 线程
Service.hpp: GenHandler → Upload
→ MyLog.hpp: GetLogger("asynclogger")
  → Manager.hpp: GetLogger（管理器锁内查表，返回 shared_ptr）
→ AsyncLogger.hpp: Info（vasprintf 产生 char*）
→ serialize（构造 LogMessage，format 返回 std::string data）
→ Flush(data.c_str(), data.size())
→ AsyncWorker.hpp: Push（锁内复制到 buffer_productor_，通知消费者）
→ 返回业务回调

该日志器的后台线程
AsyncWorker.hpp: ThreadEntry
→ 条件满足后锁内 Swap(buffer_consumer_)
→ 通知等待空间的生产者，释放锁
→ callback_(buffer_consumer_)
→ AsyncLogger.hpp: RealFlush
→ LogFlush.hpp: RollFileFlush::Flush
→ fwrite → 按 flush_log 决定 fflush / fsync
→ Buffer::Reset
```

`vasprintf` 分配字符数组，构造消息时形成字符串，`format` 通过字符串和流再次组织完整日志；之后 `Buffer::Push` 使用 `std::copy` 再复制字节。这里有多次构造和复制，不能说零拷贝。不要只凭源码给出固定分配次数，标准库实现与优化会影响它。

`Swap` 交换两个 vector 的内部存储及读写位置，不逐字节复制日志。交换后后台拥有原生产者的数据，生产者拿到另一块空 Buffer，可以继续写。后台 `Reset` 的对象是消费者，生产者不能直接往这块正在写盘的内存中追加。

`fwrite` 可能先写入 C 标准库缓冲；`fflush` 推进到内核，`fsync` 请求同步文件数据到存储设备。当前配置 `flush_log=2`，因此后台每批会调用 `fflush` 和 `fsync`。源码没有完整检查所有返回值，本块不承诺磁盘异常时不丢日志。

日志文本的 `tid_` 在 `LogMessage` 构造时捕获，是**产生日志的线程**，不是执行 `fwrite` 的后台线程。`Push` 返回只说明已复制到缓冲区，不证明已经写文件。

## 4. 线程图：不要把备份池当成日志消费池

```text
主线程：加载配置 → 备份线程池 → 创建日志器 → 创建 HTTP 线程 → join
                  │                 │
                  │                 ├─ default 的 AsyncWorker 后台线程
                  │                 └─ asynclogger 的 AsyncWorker 后台线程
                  └─ thread_count 个备份任务线程

HTTP 线程：事件循环 → 回调 → 格式化 → Push → 回调继续
                                          │
                                     双 Buffer 交换
                                          ↓
asynclogger 后台线程：等待 → 交换 → RealFlush → 文件输出 → 重置
```

`LoggerManager` 首次构造还会构建默认日志器，它也启动一个工作器线程。当前 `thread_count=3`，从创建路径推算主线程、HTTP、两个日志线程和三个备份线程共 7 个；实际数量和身份请用栈验证，不把预测当观测结果。

消费者没数据时通过条件变量休眠；生产者在固定模式空间不足时也休眠。通知仅提示重新检查条件，不替代条件本身。多个调用线程可以向同一个 `AsyncWorker` 生产，`Push` 的锁串行化追加；当前 HTTP 回调只有一个事件循环线程，多个 curl 不会变成多个 HTTP 生产线程。

ERROR/FATAL 在 `serialize` 中先提交备份任务并 `future.get()`，然后才执行 `Flush`。所以它们可能在进入日志 Buffer 前等待网络任务。本次正常实验用 INFO，避免混入这条等待路径。

## 5. 正常实验：证明交接发生在两个线程

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-07-async-log-handoff-and-backpressure-测试与验证.md)，保留原章节编号便于对照。

## 6. Linux 观察与一个简单性能对照

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-07-async-log-handoff-and-backpressure-测试与验证.md)，保留原章节编号便于对照。

## 7. 故障实验：先留下“现在已知现象”

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-07-async-log-handoff-and-backpressure-测试与验证.md)，保留原章节编号便于对照。

## 8. 你的改造：为不可能完成的 Push 定义错误契约

本次采用一个明确且小的策略：固定模式下，单条数据大于缓冲区总容量时，`Push` 立即抛 `std::length_error`；正常长度保持原来的等待和追加语义。保持现有 void 接口，核心判断由你实现。

范围：`AsyncWorker.hpp`，必要时在 `AsyncBuffer.hpp` 增加表达总容量的只读访问器，以及你的探针。不要用 `WriteableSize()` 当作总容量：缓冲区已有日志时，它只是剩余空间。检查应在复制前完成，并在合适的锁保护下读取状态；错误路径不写入任何字节。不要从此错误分支再调用同一个日志器，避免递归。

这个策略的取舍是拒绝超大日志，不提供“不丢日志”保证。当前服务调用方没有全面捕获该异常；探针必须捕获并报告。部署到业务链路前还需要决定在哪里处理异常或采用截断/分块策略，本次不要把探针通过误写成服务全链路已处理。

验收条件：

- N=32、64：Push 返回，消费者累计字节与输入相等；64 是等于总容量的边界。
- N=65：可捕获 length_error，耗时远小于 3 秒，拒绝未增加消费者字节；然后再 Push 32，证明工作器继续可用。
- 两次合法 Push 累计字节正确；缓冲区暂满时仍允许等待消费，不能错误地拒绝所有“超过当前剩余空间”的消息。
- 2、6 个生产线程各发送若干 8 字节块，消费者累计等于总输入；不要只用并发 curl 代替多生产者验证。
- 重新编译探针，并 `make -C src/server gdb_test`；检查接口改动是否影响服务编译。头文件改动后普通版需 `make -C src/server -B test`，现有普通目标没有完整头文件依赖。

多生产者争用同一 mtx_，磁盘慢会先让消费者卡在回调，再让生产者逐渐等待空间。本次累计计数只能验证总字节数，不能证明日志顺序或内容完整；不要扩大结论。

当前 `ToBeEnough` 还存在等于剩余空间时扩容、一次扩容不保证足够容纳任意大输入等问题；所以不要简单切换到 ASYNC_UNSAFE 作为修复。停止唤醒、重复 Stop 和排空也有待审计。本次把这些记作后续问题，保持修复范围明确。

## 9. 复盘与通过标准

交回四项证据：Push/RealFlush 的两份调用栈及线程身份、65 字节故障的调用栈与变量、你的代码差异、边界和多生产者回归结果。命令或预期输出不是运行结果。

先自己回答以下三题，我再按证据评价：

1. 为什么 Swap 后可以解锁写盘？生产者继续写时为什么不会覆盖正在输出的那批字节？
swap 之后，生产者和消费者操作的缓冲区不同，相互独立。两者同时进行是操作的不是同一个缓冲区
2. 日志中的线程 ID 和写日志文件的线程是什么关系？Info 返回能保证什么，不能保证什么？
Push 线程 7， 187584
RealFlush 线程 6 186393
Info 返回能保证交换了缓冲区，不能保存文件正确写入
3. 65 字节请求为什么不能靠多次 notify 或更快磁盘解决？你如何证明它与正常的缓冲区暂满不同？
先通过长度、容量、读写指针和调用路径，区分正常积压与实现缺陷

完成本块后，下一学习块再讨论后台消费变慢及停止排空；本次不自动展开。若暂时没有调试证据，记作实验待补，不能仅凭阅读标记掌握。

## 10. 2026-10-11：代码评审与助手独立回归

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-07-async-log-handoff-and-backpressure-测试与验证.md)，保留原章节编号便于对照。

## 11. 2026-10-12：修正后的独立回归

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-07-async-log-handoff-and-backpressure-测试与验证.md)，保留原章节编号便于对照。
