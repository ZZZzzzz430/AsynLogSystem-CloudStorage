# 单次断点避免每条启动日志都在同一位置停住。
set pagination off
set print pretty on
set breakpoint pending off
# GDB 默认不锁定调度；进程启动前不能设置 scheduler-locking。

tbreak mylog::AsyncLogger::Info
tbreak mylog::AsyncLogger::serialize
tbreak mylog::LogMessage::format
tbreak mylog::AsyncLogger::Flush
tbreak mylog::AsyncWorker::Push
tbreak mylog::Buffer::Swap
tbreak mylog::AsyncLogger::RealFlush
tbreak mylog::RollFileFlush::Flush

echo \n=== Info 调用链调试 ===\n
echo 已设置 8 个单次断点。输入 c 到下一断点，n 单步，s 进入函数，bt 查看调用栈。\n
echo 前 5 个断点在生产线程；Swap / RealFlush / RollFileFlush 在日志消费线程。\n
echo 异步阶段的调用栈不会包含 Info，使用 info threads 查看线程。\n
echo 不要锁定调度线程，否则等待条件变量时可能无法继续。\n
echo 退出：q。程序将启动真实服务，继续运行后默认监听 8081。\n\n
run
