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

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-08-slow-consumer-and-bounded-push-测试与验证.md)，保留原章节编号便于对照。

## 5. 故障实验：Stop 卡住时，先证明谁没有返回

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-08-slow-consumer-and-bounded-push-测试与验证.md)，保留原章节编号便于对照。

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

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-08-slow-consumer-and-bounded-push-测试与验证.md)，保留原章节编号便于对照。

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

本节内容已归档至[对应测试与验证说明](../../maintenance/testing/历史-08-slow-consumer-and-bounded-push-测试与验证.md)，保留原章节编号便于对照。
