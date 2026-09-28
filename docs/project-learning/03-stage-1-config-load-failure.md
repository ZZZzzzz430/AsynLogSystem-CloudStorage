# 阶段 1 · 进阶实操：让错误配置无法启动服务

日期：2026-09-22。

状态：任务已布置，尚未实现或运行验证。上一项文件存在性检查已完成源码审阅，用户报告测试完成。

进度更新（2026-09-23）：已纠正用户的配置读取与解析失败处理代码，完成编译和合法、非法、空、缺失配置验证；字段校验仍未实现。详见 [配置纠错记录](05-config-bugfix-review.md)。下文保留原始完整任务要求。

## 为什么提高到这个任务

用户已理解函数返回值和程序退出码，希望加快进度。本次不再逐行照写，完成一个跨少量函数的真实工程改造：配置读取失败、JSON 无法解析或关键字段非法时，明确报告原因并停止启动。

一个文件存在，不等于里面的配置能够使用。若错误值流入线程池和 Buffer，故障会在离根因更远的位置出现。本次把错误留在配置加载边界。

## 修改范围与真实调用链

只改两个文件：

- [日志 Util.hpp](../../log_system/logs_code/Util.hpp)：`JsonUtil::UnSerialize`、`JsonData` 构造函数。
- [服务入口 Test.cpp](../../src/server/Test.cpp)：在初始化调用边界处理配置加载异常。

注意不要误改同名的 `src/server/Util.hpp`。本次只完善日志配置，不展开存储配置、线程池和 Buffer 内部实现。

```text
main                                      主线程
→ log_system_module_init
  → JsonData::GetJsonData
    → JsonData 构造函数
      → File::GetContent                   文件内容进入 std::string
      → JsonUtil::UnSerialize              文本解析成 Json::Value
      → 字段校验                          本次新增
      → 初始化配置成员
  → new ThreadPool                        仅在配置成功后执行
  → 创建日志器和后台线程
→ new DataManager
→ 创建 HTTP 线程
```

配置失败时应沿调用链返回到 `main()` 的异常处理分支，输出错误并返回 `1`。本次加载发生在线程池创建之前，不需要新增锁。

## 必须达到的行为

1. 读取配置失败后，不能继续解析空内容。
2. JSON 解析失败后，不能继续读取字段。
3. 缺失字段、错误类型、越界值必须有明确诊断。
4. 错误消息包含配置路径；字段错误还要包含字段名称和要求。
5. 配置失败不能到达 `new ThreadPool`，也不能启动 HTTP 线程。
6. 原有配置仍然能够成功初始化。

本次字段校验集中于以下四项，其他字段保持原有读取方式，不宣称已实现完整配置校验：

| 字段 | 规则 | 工程目的 |
|---|---|---|
| `buffer_size` | 整数，1 到 64 MiB（含边界） | 避免零容量和明显过大的初始内存申请 |
| `thread_count` | 整数，1 到 32（含边界） | 避免零消费者和过量创建线程 |
| `flush_log` | 整数，只允许 0、1、2 | 避免不支持的刷盘模式静默生效 |
| `backup_port` | 整数，1 到 65535（含边界） | 避免端口转换后截断或无效值 |

64 MiB 和 32 是本次练习选择的启动约束，不是原项目已有规范。根节点应是 JSON 对象。要先检查存在性和整数类型，再转换和比较范围，最后赋给成员；不要先把负数转成 `size_t` 或先把过大端口转成 `uint16_t`。

## 实现提示：不给完整答案

建议使用构造函数抛异常、入口捕获的方案。构造函数没有普通返回值，在里面写 `return;` 不代表创建失败。

必要语法：

```cpp
#include <stdexcept>

// 在配置构造函数中遇到不可继续的错误：
throw std::runtime_error("具体的配置错误信息");
```

```cpp
// main 中围绕日志初始化设置边界：
try
{
    log_system_module_init();
}
catch (const std::exception& error)
{
    std::cerr << "[startup] " << error.what() << '\n';
    return 1;
}
```

思考异常发生时，`GetJsonData()` 中的 `new JsonData` 是否已经成功完成，以及后面的 `new ThreadPool` 是否还会执行。本次保证的是配置失败发生在资源线程创建之前，不要求顺带解决所有后续初始化异常的资源回收。

开始修改前先做一个小验证：让 `JsonUtil::UnSerialize` 解析合法 JSON，观察它的返回值是否符合函数语义。核对失败和成功路径各自返回什么，再决定怎么改，不能仅凭函数名相信返回值正确。

字段校验可以抽成小辅助函数，也可以先直接写分支。优先让错误信息清楚、执行顺序正确，不急于做通用框架。初始化失败使用 `std::cerr`，因为日志器尚未注册。

## 正常与故障实验

先重新编译：

```bash
cd /home/zhang/code/Poject/AsynLogSystem/src/server
make gdb_test
```

测试副本使用临时目录，保留相同目录层级，不修改项目原始配置。以下命令在同一个终端依次执行：

```bash
lab_dir=$(mktemp -d /tmp/asynlog-config-lab.XXXXXX)
mkdir -p "$lab_dir/src/server" "$lab_dir/log_system/logs_code"
cp gdb_test Storage.conf "$lab_dir/src/server/"
cp ../../log_system/logs_code/config.conf "$lab_dir/log_system/logs_code/"
cd "$lab_dir/src/server"
```

每次只改临时日志配置的一个条件，下一次实验前恢复它。重新修改源码后，需要重新编译并更新临时二进制副本。

| 用例 | 修改方式 | 预期 |
|---|---|---|
| 正常 | 使用原配置 | 到达线程池构造断点；四个字段值正确 |
| 语法错误 | 在临时配置中删除一个必要逗号 | 报 JSON 解析错误，退出 1 |
| 缺字段 | 删除 `thread_count` 字段 | 报缺少字段，退出 1 |
| 类型错误 | 将 `thread_count` 改成字符串 `"3"` | 报字段类型错误，退出 1 |
| 范围错误 | 分别使用 `thread_count: 0`、`buffer_size: -1`、`backup_port: 70000`、`flush_log: 3` | 每次报对应字段错误，退出 1 |

故障用例运行 `./gdb_test` 后紧接着执行 `echo $?`，记录实际诊断和退出码。正常用例只需在 GDB 中验证初始化，不必启动完整服务。

## GDB：证明失败停在正确位置

```bash
gdb ./gdb_test
```

```gdb
catch throw
break ThreadPool::ThreadPool
run
```

错误配置触发异常时：

```gdb
bt
info threads
continue
```

观察抛出位置是否属于配置加载调用链，随后是否进入入口的错误处理并退出。异常可能来自 JSON 库自身，也可能来自自己新增的分支，要用调用栈区分。

核心证据：错误配置不能命中线程池构造断点；配置加载抛错时应仍只有主线程。正常配置应命中线程池构造断点。验证正常路径后可用 `kill` 结束，避免继续启动服务。

## 交付与面试问题

交付修改后的两个源文件，以及至少四组实验记录：正常、JSON 语法错、类型错、范围错。其余用例也应自行验证。

每条记录包含：输入变化、实际错误信息、退出码或断点证据。

回答三个问题：

1. 为什么存在性检查不能替代真正读取与解析时的错误处理？
2. 为什么构造函数中的 `return;` 和抛异常不是同一种失败处理？
3. 你用什么证据证明非法配置没有流入线程池或 Buffer？

完成本任务后再评估启动与配置模块，不自动展开下一模块。
