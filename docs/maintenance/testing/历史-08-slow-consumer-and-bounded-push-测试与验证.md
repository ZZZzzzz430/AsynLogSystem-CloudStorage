# 08-slow-consumer-and-bounded-push：测试与验证

对应学习笔记：[08-slow-consumer-and-bounded-push](../../project-learning/笔记/08-slow-consumer-and-bounded-push.md)。

以下内容从原笔记按章节迁移，章节编号和历史状态保留。命令按正文指定的工作目录执行；未注明时以仓库根目录为准。整理目录不代表重新执行测试。

## 4. 正常实验：用时间线理解等待传播

建立 `experiments/slow_consumer_probe.cpp`，自行实现探针，沿用 07 的配置初始化方法：从 server 目录运行，先加载 g_conf_data，再在本进程将 buffer_size 设为 64，之后创建工作器。不修改仓库配置或存储数据。

使用 INFO 链路的等效字节生产；探针直接调用 AsyncWorker，避免 ERROR/FATAL 的网络备份影响实验。回调仅累计字节和记录时间，不调用项目日志器。

按下面顺序构造可控时序：

1. 第一次 Push 64 字节，回调进入后通过 promise 通知主线程，然后等待另一个 future 门闩。
2. 确认回调已经进入，再 Push 64 字节，此时生产者 Buffer 满。
3. 单独的生产线程 Push 8 字节，记录 steady_clock 的开始和结束时间。
4. 主线程等 200ms 后打开门闩，所有后续回调直接完成。
5. 等生产线程结束并调用 Stop，核对累计消费 136 字节。

预期：第三次 Push 在门闩打开前不能完成；释放后成功，等待时间包含这段延迟。不要仅靠第一次 Push 后 sleep 猜测消费者已交换，必须用门闩确认位置。

输出至少包含：每次提交长度、调用开始/结束相对时间、callback 开始/结束、累计消费量。这里的计数表示回调处理的字节，不表示写到磁盘。

编译和运行示例，从项目根目录开始：

```bash
g++ -std=c++17 -g3 -O0 -pthread experiments/slow_consumer_probe.cpp -ljsoncpp -o /tmp/slow_consumer_probe
cd src/server
timeout 10s /tmp/slow_consumer_probe
```

探针应根据验证结果返回 0 或非零；不要只打印 FAIL 却仍让 main 返回 0。

## 5. 故障实验：Stop 卡住时，先证明谁没有返回

仍用同一个探针，增加 `--hold` 模式：在受控回调内一直等门闩；另一个线程调用 Stop；同时保留等待空间的第三次 Push。

先记录现象：生产者是否被 Stop 唤醒并拒绝？Stop 调用是否仍没有结束？不要看到 Stop 卡住就先改条件变量。

使用 GDB 从 server 目录启动：

```bash
gdb --args /tmp/slow_consumer_probe --hold
```

```gdb
set pagination off
break mylog::AsyncWorker::Stop
break mylog::AsyncWorker::ThreadEntry
run
```

运行到持有门闩的状态后禁用断点并 continue，再 Ctrl-C：

```gdb
info threads
thread apply all bt
```

记录三类证据：

- Stop 调用线程是否在 join 等待后台退出？
- 后台线程是否在你探针的 future.wait，而不是等 cond_consumer_？
- 第三次 Push 是否已经因 stop_ 被拒绝，而不是仍等待空间？

在 Push/Stop 的对应栈帧中观察 `stop_`（原子变量可查看其内部值）、`len`、`buffer_productor_.write_pos_ / read_pos_ / buffer_.size()`。在 callback 栈帧观察回调输入，不要用 Push 帧的变量名套到回调。

探针再提供一个恢复机制，例如独立控制线程从标准输入读取一行后打开门闩。在另一轮无 GDB 实验中按 Enter 释放，预期 Stop 随排空结束，消费总量为 128，未接受的 8 字节不计入。故障必须可恢复；外部 timeout 只是兜底，不是排空成功。

排查顺序：先取得线程栈，区分生产者空间等待、后台回调等待、停止 join 等待，再释放门闩验证因果。该实验没有真实磁盘，所以证明的是“回调不返回会拖住 Stop”，不声称复现了某块磁盘故障。

Linux 观察：

```bash
ps -T -p PID -o pid,tid,stat,wchan:32,comm
pidstat -t -p PID 1
```

从 GDB `info inferiors` 获取 PID；用 TID 与线程栈对应。阻塞线程通常 CPU 低，S/futex 等待本身不说明死锁。工具未安装则用 ps 和 GDB 即可。GDB 暂停全部线程会影响耗时，时间对比单独在无调试器模式运行。

## 7. 回归与性能观察

至少保存以下结果及进程退出码：

| 场景 | 预期 |
|---|---|
| 空闲写 32/64 字节 | Accepted，内容和长度正确 |
| 空闲写 65 字节，固定模式 | TooLarge，无内容副作用 |
| 暂满，期限 50ms，门闩保持关闭 | TimedOut，不追加该请求 |
| 暂满，期限 1s，约 100ms 后释放 | Accepted，累计内容正确 |
| 零/负期限 | 有空间成功，无空间立即 TimedOut |
| 等待期间调用 Stop | Stopped，已接受字节仍排空 |
| 超时后释放，再正常写入 | 工作器继续可用 |
| 2、6 个生产线程混合提交 | Accepted 字节数等于消费字节数，分别统计其他结果 |
| 07 原回归 | 原 Push 仍等待，原有边界和多生产者不退化 |

受控故障使用门闩；性能对比另用“每批回调 sleep 0ms / 10ms”的消费者，各提交相同数量的 8 字节块。记录总耗时、Accepted/TimedOut 数、PushFor 耗时分布和 CPU。超时模式如果少接收了数据，不能把总耗时缩短宣传为同负载吞吐提升；成功量也必须比较。性能采样不在 GDB 中完成。

重新编译探针，`make -C src/server gdb_test`，普通版需要 `make -C src/server -B test`；清理既有行尾空白并检查 `git diff --check`。不要执行会删除存储目录的 make clean。

## 9. 2026-10-14：作用域修正与独立测试

原因、修改与当时的验证边界见[修复与评审记录](../fixes/历史-08-slow-consumer-and-bounded-push-修复与评审.md)。
