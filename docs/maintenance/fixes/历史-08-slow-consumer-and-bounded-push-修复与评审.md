# 历史-08-slow-consumer-and-bounded-push：修复与评审

对照[测试说明](../testing/历史-08-slow-consumer-and-bounded-push-测试与验证.md)。历史诊断及当时的验证边界按原文保留，未因归档重新执行。

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
