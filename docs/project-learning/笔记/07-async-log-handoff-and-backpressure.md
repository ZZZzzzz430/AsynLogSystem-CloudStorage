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

沿用 [06 第 4 节](06-upload-roundtrip-and-validation.md#4-独立实验目录)建立新的独立目录，编译并复制最新 `gdb_test`；不要在已有数据目录反复上传，不要使用会删除存储数据的 `make clean`。将启动命令换为：

```bash
gdb ./gdb_test
```

先设断点并启动：

```gdb
set pagination off
break mylog::AsyncWorker::Push
break mylog::AsyncLogger::RealFlush
run
```

启动日志也会命中。每次先看 `bt`：启动链路不是上传链路；用 `continue` 让服务完成启动。再按 06 中的合法上传 curl 触发；GDB 停住整个进程时 curl 等待是正常调试现象，可把它的超时改为 60 秒。

命中 `Push` 且调用栈含 `Upload` 时记录：

```gdb
bt
info threads
p len
x/s data
p buffer_productor_.write_pos_
p buffer_productor_.read_pos_
p buffer_productor_.buffer_.size()
p async_type_
```

这条路径传入的是字符串，`x/s` 可看文本；接口本身按 `len` 接受字节，并不普遍保证 NUL 结尾。不要复制该观察方法到任意二进制缓冲区。

继续命中 `RealFlush`，观察：

```gdb
bt
info threads
p logger_name_
p buffer.write_pos_
p buffer.read_pos_
p buffer.buffer_.size()
```

比较两次 GDB 线程编号及调用栈。消费者可能一批包含多条日志；不要求一次 Push 严格对应一次 RealFlush。确认记录后禁用这两个断点再继续，避免 curl 超时。可进一步在 `ThreadEntry` 的 `Swap` 前后设置源码行断点，比较两个 Buffer 的位置，理解交换和重置。不要开启 scheduler-locking 后等待另一个线程完成工作。

预期证据：上传日志的 Push 在 HTTP 栈，RealFlush 在 ThreadEntry 栈；日志文件包含上传文本。前者证明线程交接，后者证明这次数据被输出，单独 HTTP 200 不能替代它们。

## 6. Linux 观察与一个简单性能对照

在 GDB 用 `info inferiors` 取得 PID，另一个终端观察：

```bash
ps -T -p PID -o pid,tid,stat,wchan:32,comm
ss -ltnp 'sport = :18081'
strace -f -p PID -e trace=futex,write,fsync -o /tmp/stage3.trace
```

`ps` 的 TID 对应 Linux 线程；`STAT` 中 S 和 futex 等待常见于正常空闲状态，不能单凭它断言死锁。监听只证明网络入口存在。`strace` 中查看 write/fsync 的线程与返回值；条件变量通常涉及 futex，但 futex 也可能属于其他锁。GDB 用 `thread apply all bt` 区分具体角色。若环境不允许 attach，可从启动时用 `strace -f ... ./gdb_test`，不要与 GDB 同时争用跟踪权限。

退出调试工具后，可在独立实验目录比较串行 20 次 GET 列表和 `xargs -P 6` 并发 20 次 GET 列表。给 curl 加 `--max-time 10`，记录 `time` 的 real、响应码和 `ps`/`pidstat -t -p PID 1` 中各线程 CPU。列表调用会产生日志，但也包含页面生成开销，这只能观察整体响应，不能据此计算纯日志吞吐率。磁盘慢、锁等待可能耗时大而 CPU 低。

## 7. 故障实验：先留下“现在已知现象”

用独立最小程序直接调用 AsyncWorker，把网络、文件压缩和备份排除。文件可建在 `experiments/async_worker_probe.cpp`，不要替换服务入口。包含 `<memory>`、`<iostream>`、`<string>`、`<atomic>`、`<chrono>`、`<thread>` 和 `../log_system/logs_code/AsyncWorker.hpp`，定义源码需要的全局变量：

```cpp
mylog::Util::JsonData* g_conf_data = nullptr;
```

自行写 main，流程如下（这里的步骤是实验程序，不是修复答案）：

1. `g_conf_data = mylog::Util::JsonData::GetJsonData()`，然后在任何 Buffer 创建前将该进程的 `buffer_size` 设置为 64。不改仓库配置文件，也不在工作器运行后改配置。
2. 构造一个 `ASYNC_SAFE` 工作器，回调只把 `ReadableSize()` 累加到局部 `atomic<size_t>`，不写文件、不写项目日志。计数变量必须比工作器先构造，确保回调引用存活。
3. 从命令行读入长度 N，生成 `std::string payload(N, 'x')`；调用 Push 前后用 `std::cerr` 输出 `before Push` 和 `after Push`。
4. 正常回归时，等回调计数达到 N，再结束工作器作用域；设短超时并报告失败。这避免把当前 Stop 的排空缺陷混入本块验收。

在项目根目录编译，进入 server 目录运行以满足当前配置相对路径：

```bash
g++ -std=c++17 -g3 -O0 -pthread experiments/async_worker_probe.cpp -ljsoncpp -o /tmp/async_worker_probe
cd src/server
timeout 3s /tmp/async_worker_probe 32
timeout 3s /tmp/async_worker_probe 65
```

**先记录现象，不先改代码。** 正常长度是否打印 after？异常长度是否只打印 before，timeout 的退出码是否为 124？这些是待验证的预期，尚未实际运行。

再用 `gdb --args /tmp/async_worker_probe 65`，`run` 后卡住时 Ctrl-C，执行：

```gdb
info threads
thread apply all bt
```

切换到调用栈包含 Push 的线程，选择对应 frame，打印 `len`、`buffer_productor_.buffer_.size()`、`write_pos_`、`read_pos_`。另一个线程停在哪里？是否已经到回调？此实验没有磁盘和网络操作，能否据此排除磁盘满、备份服务未启动等假设？

最后回到等待谓词：在空缓冲区、N=65、容量=64 的情况下，消费者即使正常运行，是否有能力让这个条件成立？用实测变量回答。这一步才完成“现象 → 排除外部因素 → 定位等待线程 → 验证条件”的闭环。结束故障探针用 GDB `kill`，不调用项目 Stop 来试图解救等待。

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

结论：**本块暂未通过，有一项必须修正的行为偏差。** 用户报告代码与测试完成，并在第 9 节填写回答；本次检查当前源码并独立运行回归，没有替用户修改核心实现。

### 已完成的部分

`Buffer::Push` 增加零长度返回，恰好填满时不再扩容；禁止增长时会检查空间。`ToBeEnough` 使用 size_t，检查长度求和和增长溢出，扩容目标至少达到 required，改善了原来只扩一次仍可能不足的问题。助手测试 1000 字节扩容后的长度与内容一致，结果通过；这不是所有扩容策略和极限输入的完整验证。

固定模式在空 Buffer 上的 32、64 字节正常写入通过；65 字节抛 length_error，通过；拒绝后再写入 32 字节，消费计数正确。说明你已经解决原先“空缓冲也永远容纳不下”的入口问题。

### 必须修正：总容量与剩余空间混淆，正常反压被删除

位置：[AsyncWorker.hpp](../../../log_system/logs_code/AsyncWorker.hpp) 的 Push。

当前判断 `fixed && len > buffer_productor_.WriteableSize()` 就抛异常，同时删除了 `cond_productor_.wait`。这把“单条消息永远容纳不下”与“暂时剩余空间不足”合并成拒绝，违反第 8 节的任务契约。

助手用 promise 控制消费者停在第一次回调，稳定复现：

1. 容量 64，Push 64，等消费者完成交换后阻塞其回调。
2. Push 48，此时生产者剩余空间 16。
3. 另一个生产线程 Push 32。它不超过总容量，应该等待消费者腾出空间；当前立即抛异常。
4. 放开回调，最终消费 112 字节，而正确接收全部数据应为 144 字节。

本次原始输出如下；并发拒绝数量依赖调度，不能作为固定预期：

```text
boundary 32: PASS
boundary 64: PASS
boundary 65: PASS
temporary-full: FAIL rejected=1 consumed=112
producers 2: FAIL rejected=1513 consumed=3896 expected=16000
producers 6: FAIL rejected=4342 consumed=13264 expected=48000
growth: PASS
```

多生产者实验为每线程连续写入 1000 个 8 字节块，捕获异常并计数。无磁盘、HTTP 或备份操作；回调仅统计字节，结束前等待已接收字节消费完。由此可将这些拒绝定位到 Push 契约，而非外部 I/O。没有捕获异常的业务调用可能因正常积压抛出异常，不能认为编译通过就可直接用于服务。

下一次只修正这一点：在锁内按**总容量**识别不可能容纳的单条消息；其余合法长度恢复按**剩余空间**等待，然后复制、通知。可在 Buffer 增加 const 的总容量访问器。保持固定模式不扩容，保留你本次扩容修正。先重跑受控慢消费者实验，再跑 2、6 个生产线程，避免只用“消费者恰好很快”的测试证明正确。原有 Stop 与排空问题不在这次修复范围内。

### 编译与测试证据边界

助手创建的独立回归源文件位于 `/tmp/stage7_review.cpp`，二进制为 `/tmp/stage7_review`，以 server 目录为 cwd，进程内将 buffer_size 设为 64，未修改仓库配置、未启动 HTTP 服务、未写存储数据。编译与 `timeout 15s` 运行退出 0；探针用输出报告失败，**进程退出 0 不代表所有用例通过**。

`make -C src/server gdb_test` 显示目标已是最新，未重新编译；`make -C src/server -B test` 实际重新编译并退出 0。`git diff --check` 未通过，AsyncBuffer.hpp 第 53、55、101 行有行尾空白，属于次要清理项。

仓库未找到 `experiments/` 或用户的探针源文件/测试输出。第 9 节记录的 Push 线程 7、RealFlush 线程 6 可作为用户观察反馈，但没有两份调用栈；本次不宣称复验了 HTTP 线程交接或原版 65 字节卡住过程。

### 三题反馈

1. “交换后操作不同缓冲区”方向正确。补充：Swap 在 mtx_ 内完成，连同读写位置交换；后台锁外输出消费者，业务追加生产者，下一次交换由同一个后台线程在本轮输出和 Reset 后进行。锁和所有权交接共同保证这段访问关系。
2. 不同线程的记录支持交接观察，但“Info 返回保证交换了缓冲区”不正确。正常返回只保证格式化后的字节已复制到生产者 Buffer，并发出了通知；消费者可能尚未执行 Swap，更不能保证文件输出或持久化。日志文本的 tid 是调用线程 ID。
3. 目前回答给出了检查方向，缺少关键证明。工程化答案：固定总容量 C=64，即使 Buffer 完全清空，剩余空间最多为 64；len=65 的等待条件永远不成立。notify 只让线程重新检查条件，快磁盘也不能改变 C。正常积压则是 len≤C、len>当前剩余空间，消费后有可能满足条件，所以应等待而非拒绝。

本次先收束在上述修正及回归；修好后再评审通过，不自动进入下一学习块。

## 11. 2026-10-12：修正后的独立回归

结论：**本块核心代码改造与本次运行验收通过。** 当前 Push 先按 `Capacity()` 拒绝不可能容纳的单条数据，再用条件变量等待剩余空间；固定模式禁止扩容。已消除第 10 节中合法消息因暂满被拒绝的问题。这里的通过指下列实际测试范围，不等于整个日志系统的所有并发与异常场景均已验证。

助手重新编译 `/tmp/stage7_review.cpp` 并在 server 目录运行 `timeout 15s /tmp/stage7_review`，结果：

```text
boundary 32: PASS
boundary 64: PASS
boundary 65: PASS
temporary-full: PASS rejected=0 consumed=144
producers 2: PASS rejected=0 consumed=16000 expected=16000
producers 6: PASS rejected=0 consumed=48000 expected=48000
growth: PASS
```

暂满实验沿用 promise 控制消费回调：32 字节发送在释放消费者前等待，释放后完成，累计 144 字节，无拒绝。多生产者各发送 1000 个 8 字节块，2、6 个生产线程的总计数均符合预期。这验证字节累计与合法请求接收，不单独证明每条日志内容和顺序。

用户同时改动了停止协议，助手补充编译 `/tmp/stage7_stop_review.cpp` 并用 15 秒外部超时运行：消费者处理第一批 64 字节时受控暂停，生产者再填满 64 字节，第三次 8 字节 Push 等待；另一个线程执行 Stop，观察生产者退出，再释放消费者，检查两批已接收数据均被处理。

```text
stop-wakeup: PASS
stop-drain: PASS consumed=128
sequential-repeat-stop: PASS
push-after-stop: PASS
```

停止等待中的生产者被唤醒并抛 runtime_error；Stop 返回前已消费 128 字节；顺序再次 Stop，以及随后析构，没有重复 join 故障；停止后 Push 被拒绝。这个补充实验未覆盖多个线程同时调用 Stop、回调内部调用 Stop或回调抛异常，不能据此宣称 Stop 在任意调用关系下线程安全。

编译验证：`make -C src/server -B test` 实际重新编译并退出 0；`make -C src/server gdb_test` 显示目标已最新，未重新编译。未启动 HTTP 服务、未修改存储数据，也没有重新获取 HTTP Push/RealFlush 调用栈。

剩余非阻塞清理项：AsyncWorker 超容量异常文本仍写着 remaining capacity，应改为 total/buffer capacity；`git diff --check` 在 AsyncBuffer.hpp 第 57、59、105 行报告行尾空白。助手本次只做测试与文档记录，没有改业务源码。

理解复盘继续沿用第 10 节的纠正：Info 正常返回只保证日志进入生产者 Buffer，不保证已交换或写盘；总容量不足与暂时空间不足需要两种不同策略。本次不自动展开下一学习块。
