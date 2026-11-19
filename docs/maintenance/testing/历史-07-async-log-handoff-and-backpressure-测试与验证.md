# 07-async-log-handoff-and-backpressure：测试与验证

对应学习笔记：[07-async-log-handoff-and-backpressure](../../project-learning/笔记/07-async-log-handoff-and-backpressure.md)。

以下内容从原笔记按章节迁移，章节编号和历史状态保留。命令按正文指定的工作目录执行；未注明时以仓库根目录为准。整理目录不代表重新执行测试。

## 5. 正常实验：证明交接发生在两个线程

沿用 [06 第 4 节](../../project-learning/笔记/06-upload-roundtrip-and-validation.md#4-独立实验目录)建立新的独立目录，编译并复制最新 `gdb_test`；不要在已有数据目录反复上传，不要使用会删除存储数据的 `make clean`。将启动命令换为：

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

## 10. 2026-10-11：代码评审与助手独立回归

原因、修改与当时的验证边界见[修复与评审记录](../fixes/历史-07-async-log-handoff-and-backpressure-修复与评审.md)。

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
