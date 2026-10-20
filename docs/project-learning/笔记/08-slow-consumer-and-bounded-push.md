# 阶段 3 · 学习块 2：慢消费、停止排空与有界等待

日期：2026-10-12。状态：已核对当前源码，实验与改造待用户完成。本次未运行新实验，未替用户实现核心代码。07 已通过的边界和多生产者回归作为基础，不重复布置同一修复。

本块时间盒 2～3 小时，只改造一个目标：**让调用者能为等待日志缓冲空间设置期限**。停止流程用于解释和回归，不同时开展并发 Stop、回调异常处理或 HTTP 线程池改造。

## 1. 本块的工程问题

日志异步化之后，如果后台写盘一直很慢，业务还能一直正常运行吗？

当前实现的答案是：两块 Buffer 能吸收一段突发流量，但不是无限储存。消费者在锁外写盘，生产者先继续填另一块 Buffer；另一块也满了，合法 Push 就等待 `cond_productor_`。HTTP 回调中的 Info 也会跟着等待，事件循环因此不能继续处理其他回调。

当前 Stop 先拒绝新数据，再唤醒等待线程，最后 join 后台线程。它不能强制中断已经执行的 callback；如果 callback 不返回，Stop 也不会返回。**唤醒条件变量与中断磁盘 I/O 是两回事。**

## 2. 源码范围与状态

沿用 [AsyncWorker.hpp](../../../log_system/logs_code/AsyncWorker.hpp) 的 `Push / ThreadEntry / Stop`、[AsyncBuffer.hpp](../../../log_system/logs_code/AsyncBuffer.hpp) 的 `Capacity / WriteableSize / Swap / Reset`。

只向下补读 [AsyncLogger.hpp](../../../log_system/logs_code/AsyncLogger.hpp) 的 `Flush / RealFlush` 和 [LogFlush.hpp](../../../log_system/logs_code/LogFlush.hpp) 的 `RollFileFlush::Flush`：业务调用 Flush 进入 Push；后台回调调用 fwrite，配置为 2 时继续 fflush、fsync。本次探针用受控延迟模拟慢消费者，不需要真的填满磁盘。

对象仍由调用线程构造，构造函数启动 thread_，后台进入 ThreadEntry。两个 Buffer 随工作器存活；探针的门闩、计数和回调引用对象必须先于工作器创建，晚于它销毁。生产者与消费者通过 mtx_、两个条件变量交接，输出回调不持有 mtx_。

| 状态 | 主线程/生产者 | 后台线程 | 谁能推动进展 |
|---|---|---|---|
| 空闲 | 可以追加日志 | 等 cond_consumer_ | 生产者写入并通知 |
| 正在消费第一块 | 可以追加另一块 | 执行 callback | callback 返回 |
| 两块都占用 | 空间不足的 Push 等待 | 仍在 callback | 后台完成本批，再交换 |
| 停止中 | 新 Push 拒绝，等待者醒来后拒绝 | 消费已接收数据后退出 | callback 返回并排空 |
| 已退出 | Push 拒绝 | 已完成 | Stop 的 join 返回 |

ThreadEntry 当前只有在 `stop_ && buffer_productor_.IsEmpty()` 时退出，该检查在锁内，且本轮 callback 与 Reset 已在上一轮完成。停止标志不是“立刻丢弃剩余数据”。已接收数据指成功追加的字节；被 Stop 拒绝的等待请求不属于排空范围。

## 3. 调用链和线程图

```text
HTTP 线程：Service::Upload → AsyncLogger::Info → serialize → Flush
                                                      ↓
生产者：AsyncWorker::Push → mtx_ → 检查 → 等待空间 → 复制 → 通知
                                  ↑             ↓
                                  └── cond_productor_ 唤醒
后台：ThreadEntry → 锁内 Swap → 解锁 → callback / RealFlush → Reset
                                             ↓
                                       fwrite / fflush / fsync

停止调用者：Stop → 锁内 stop_=true → 通知两个条件变量 → join
后台：当前 callback 返回 → 消费生产者残留 → 检查空且停止 → 返回
```

数据从调用者字符串复制到生产者 vector，Swap 交给消费者，callback 读取消费者字节。慢消费期间没有额外日志队列，也不会因通知更多次而多出空间。有限 Buffer 解决突发缓冲，不能解决长期生产速率大于消费速率。

当前 `ASYNC_UNSAFE` 允许增长，代价是内存压力；切换模式不能证明系统不会阻塞或内存耗尽。当前 default 与 asynclogger 仍各有工作器，备份线程池不是这个消费者。

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

## 6. 小型改造任务：增加有期限的生产接口

核心实现由你完成。保留现有 Push 和 Stop 契约，新增接口，例如：

```cpp
enum class PushResult { Accepted, TimedOut, Stopped, TooLarge };
PushResult PushFor(const char* data, size_t len,
                   std::chrono::milliseconds timeout);
```

这四个名字是本次拟新增接口，不是当前源码已经存在的实现。

契约：

- 停止状态返回 Stopped；固定模式 len 大于总容量返回 TooLarge；错误或拒绝路径不写入字节。
- 固定模式空间足够就写入；暂时不足则在给定期限内等待。超时返回 TimedOut，成功才返回 Accepted 并通知消费者。
- 等待被 Stop 唤醒，返回 Stopped；停止判定优先于追加。
- 超时为零或负数视为不等待，但已有空间时仍可成功。
- 增长模式保持可增长语义；内存分配异常仍可传播，不能把坏分配伪装成等待超时。
- 不通过同一个日志器输出错误，避免日志反压时递归调用。

提示：使用 steady_clock 的单一截止时间与条件变量谓词，或带谓词的 wait_for；不要每次虚假唤醒就重新获得完整 timeout。状态检查、等待和追加仍在同一个 mtx_ 协议下。可以抽取少量重复逻辑，但不要为这次练习重写整个日志框架。

期限约束针对等待**缓冲空间**，不是整个函数的硬实时保证：取得 mutex、调度及内存复制也可能耗时。PushFor 也不能给 Stop 的 join 设置期限，不能中断 callback。

本次只将新接口接到探针。业务是否丢弃、重试或降级交给上层策略，不直接替换 HTTP 日志调用。重试本身可能继续增加延迟；只靠此接口不能宣称请求延迟已有上限。

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

## 8. 面试官问题与通过标准

先自己回答，再交给我评审：

1. 后台写盘在锁外，为什么生产者仍可能卡住？增加缓冲容量能解决持续慢消费吗？
写入磁盘速度太慢，导致生产区不断积压过多字节流，出现背压。不能通过增加缓存，否则会越来越大以至于溢出等问题。
2. Stop 已经 notify_all，为什么还可能等很久？你怎样用线程栈排除“消费者睡在条件变量里”的猜测？
因为消费者可能正在写入磁盘进行 Push ，Stop 需要等待写入磁盘完成。 测试在通知时 消费者的线程等数据，我不是很清楚
3. PushFor 超时后谁决定是否丢日志？Accepted 能否证明磁盘持久化？
TimeOut 丢弃当前记录。 Accepted 只能证明缓冲区交换，不能够保证磁盘持久化。
4. 如果实现遇到虚假唤醒就重新等待 50ms，会破坏什么契约？
这个是相对时间，破坏绝对时间等待的 Timeout 契约。

交回：探针源文件、正常与可恢复故障时间线、三类线程栈、PushFor 差异、回归表与你的回答。核心代码、运行证据与理解分别检查。停止并发调用、后台异常、输出失败和服务优雅退出仍有后续范围，不因这次超时接口通过就标记整个系统可靠。

本块完成后再选择下一模块；本次只布置以上内容。

## 9. 2026-10-14：作用域修正与独立测试

用户授权修正类外实现并判断测试结果。结论：**修正后核心接口与本次功能回归通过；整个学习块的故障调试、性能观察和面试回答仍待补证据。**

### 本次修正

原 PushFor 是全局自由函数，不能直接访问 AsyncWorker 的 mtx_、stop_ 和 Buffer 等成员，类内仅声明又没有对应成员定义。类外定义本身并非错误：可以在命名空间内写 `inline PushResult AsyncWorker::PushFor(...)`；本次采用更直接的方式，将函数体放入类的 public 部分并移除重复声明。

PushResult 移入 mylog 命名空间，保留用户的枚举拼写 `TimeOut`（任务示例为 TimedOut，语义一致）。修正 `tmieout / timeput / buffer_produtor_` 等拼写，使用 wait_until 的布尔返回值判断超时，并把非正期限归一为不等待。保留用户的单一截止时间、停止优先判断、固定模式总容量校验、成功追加后通知等主逻辑。补齐直接使用的 algorithm、memory 头文件。

这里不是“复制一个自由函数就自动能访问私有成员”：作用域、成员声明与定义必须匹配。头文件内的类内定义隐式 inline，可被多个翻译单元包含。

### 用户探针

实际文件位于 `src/server/experiments/slow_consumer_probe.cpp`。它的 `#include "AsyncWorker.hpp"` 需要额外 include 路径；助手没有搬动该文件，编译命令为：

```bash
g++ -std=c++17 -g3 -O0 -pthread -Ilog_system/logs_code src/server/experiments/slow_consumer_probe.cpp -ljsoncpp -o /tmp/slow_consumer_probe
cd src/server
timeout 10s /tmp/slow_consumer_probe
```

实测第三次 Push 等待 200ms，回调消费 64+64+8=136 字节，输出 PASS，退出 0。该探针只调用原 Push，没有测试 PushFor，也没有 --hold 恢复模式，因此仅凭它不能验收新增接口或故障调用栈。

### 助手独立 PushFor 回归

源码 `/tmp/stage8_review.cpp`，编译使用 C++17、pthread、JsonCpp；在 server 目录以 `timeout 15s /tmp/stage8_review` 运行，退出 0，全部检查 PASS：

| 检查 | 实测结果 |
|---|---|
| 空闲 32/64 字节 | Accepted，排空后内容逐字节正确 |
| 固定模式 65 字节 | TooLarge，无追加；随后 32 字节成功 |
| 满缓冲，50ms 期限 | TimeOut；时长不低于 40ms、不超过宽松的 500ms 上限，无追加 |
| 零与负期限 | 有空间 Accepted，满缓冲 TimeOut |
| 1s 期限，等待 100ms 后释放门闩 | 释放前未完成，释放后 Accepted；内容为 128 个 a 加 12345678 |
| 超时后恢复 | 后续写入 Accepted，总计 136 字节 |
| 等待中 Stop | 被唤醒返回 Stopped；已接收 128 字节排空 |
| 停止后提交 | Stopped |
| 2、6 个生产线程混合 0ms/2ms 期限 | 无意外结果，Accepted 字节数与消费总量一致 |
| ASYNC_UNSAFE 写 1000 字节 | Accepted，排空后内容正确 |

每个并发生产者提交 500 次，每次 8 字节；消费者每批延迟 1ms。本轮 2 线程 Accepted=876、TimeOut=124、消费 7008 字节；6 线程 Accepted=1894、TimeOut=1106、消费 15152 字节。具体成功/超时数量受调度影响，不作为固定验收数字，也不据此声称专业性能提升。

07 的独立探针重新编译并运行：32/64/65 边界、暂满等待、2/6 生产线程与增长均 PASS；原 Push 未退化。服务 `gdb_test` 和普通版 `test` 实际重新编译成功。未启动 HTTP 服务，未改动存储数据。

### 通过范围与待补项

本次验证常规期限与功能行为，没有测试极端接近时钟表示上限的期限；当前直接计算 now+timeout，不能声称任意大 duration 都安全。等待期限也不是包含加锁、调度和复制的硬实时上限。并发 Stop、回调异常和 I/O 错误传播未纳入本次测试。

仓库中没有本块三类线程的故障调用栈、完整性能对照和第 8 节四题回答，先记为学习证据待补，不重复布置已经通过的接口改造。AsyncBuffer.hpp 既有三处行尾空白仍使全仓 `git diff --check` 不通过，属于非功能清理项。
