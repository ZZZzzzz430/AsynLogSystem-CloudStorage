# 使用 GDB 跟踪 Info 日志调用链

在 `src/server` 目录执行：

```bash
./debug_info.sh
```

脚本自动编译带 `-g3 -O0 -fno-omit-frame-pointer` 的 `gdb_test`，加载断点并启动程序。不需要先运行 `./test`。已有服务占用 8081 时，先停止旧服务，再继续本次调试。

这是实际服务调试，会读取现有配置和元数据并产生正常日志。启动过程中就会调用 Info，无需先上传文件。编译需要项目原有的 JsonCpp、bundle 和 Libevent 依赖。

## 断点与观察内容

断点都是 `tbreak`，每个命中一次后自动删除。每次停下先执行 `bt` 和 `info args`，再执行 `list` 查看当前位置。

| 停止位置 | 观察重点 | 建议命令 |
| --- | --- | --- |
| `AsyncLogger::Info` | 宏补充的源文件、行号和原始格式串 | `p file`、`p line`、`p format` |
| `AsyncLogger::serialize` | 格式化后的消息、日志级别 | `p level`、`x/s ret` |
| `LogMessage::format` | 消息如何拼接为完整日志 | `p *this`、`list` |
| `AsyncLogger::Flush` | 准备交给缓冲区的内容 | `p len`、`x/s data` |
| `AsyncWorker::Push` | 加锁、容量判断、写入、通知 | `p buffer_productor_`、`list` |
| `Buffer::Swap` | 后台线程交换双缓冲 | `bt`、`p *this`、`p buf` |
| `AsyncLogger::RealFlush` | 消费者收到的一批日志 | `p buffer`、`bt` |
| `RollFileFlush::Flush` | 滚动文件创建及真正写入 | `p len`、`p basename_`、`list` |

在函数入口，局部变量可能尚未初始化。例如 `Info` 中的 `ret` 必须等 `vasprintf` 执行后才能查看。后台缓冲区按长度保存字节，不保证以零结尾；观察后台输出时可使用 `p *data@len`，不要直接用 `x/s data` 读取整批缓冲。

## 推荐操作顺序

1. 首次停在 Info：执行 `bt`，确认是哪一行业务代码发起日志，再查看 `file`、`line`、`format`。
2. 输入 `c`，依次到消息组装、格式化、入队位置。需要观察某行执行结果时用 `n`；希望进入被调用函数时用 `s`。
3. 到 `Push` 后用 `c`，等待后台线程交换缓冲并落盘。GDB 会自动切换到命中断点的线程。
4. 在 `RollFileFlush::Flush` 中用 `n` 跟踪 `InitLogFile`、`fwrite`、`fflush` 和配置控制的 `fsync`。用 `s` 可进入 `InitLogFile` 查看文件创建逻辑。
5. 最后输入 `q`，按提示结束调试进程；若继续输入 `c`，程序会完成启动并等待 HTTP 请求。

`Info` 在 `MyLog.hpp` 中还是一个宏，会自动补上 `__FILE__` 和 `__LINE__`。因此断点设置在实际成员函数 `mylog::AsyncLogger::Info` 上，而不是宏定义处。

调用关系分为两个线程，不能通过连续 `s` 从业务线程“进入”后台线程：

```text
生产线程：Info → serialize → LogMessage::format
                        → Flush → AsyncWorker::Push → 写缓冲 / notify_one

消费线程：AsyncWorker::ThreadEntry → Buffer::Swap
                                → 回调 → AsyncLogger::RealFlush
                                       → RollFileFlush::Flush → fwrite
```

生产者通知后两个线程的执行先后不固定，后台一次也可能消费多条日志。因此这些断点展示真实调用链，不保证追踪到的每一批数据都只有第一条 Info。

## 常用命令

```gdb
bt                       # 当前线程调用栈
info threads             # 查看所有线程
thread apply all bt      # 查看各线程停在哪里
info breakpoints         # 查看剩余断点
n                        # 执行下一行，不进入函数
s                        # 单步进入
finish                   # 执行到当前函数返回
c                        # 继续到下一个断点
list                     # 查看源码
q                        # 退出
```

需要重复观察时，可以手动设置普通断点：

```gdb
break mylog::AsyncLogger::Info
break mylog::AsyncLogger::RealFlush
break mylog::RollFileFlush::Flush
continue
```

此时刷新页面或上传文件即可触发新日志。若只想看自己添加的某一条 Info，先在该调用行设置 `break 文件名:行号`，到达后再启用上述函数断点。

不要在这条异步调用链中设置 `set scheduler-locking on`：生产者可能需要等待消费者释放容量，锁定线程会妨碍进展。脚本保持调度不锁定。

如果 GDB 报 `ptrace: Operation not permitted`，需在允许调试进程的本地终端/容器环境运行；这不是断点或源码问题。
