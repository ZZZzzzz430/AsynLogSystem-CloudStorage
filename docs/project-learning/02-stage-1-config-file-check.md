# 阶段 1 · 代码实操：配置文件检查与启动控制

整理日期：2026-09-22。

状态：用户报告已完成测试；助手已检查当前源码，确认检查函数和 `main()` 的提前退出已接入，未独立运行验证。本文保存实操任务，仍属于第一阶段，不进入线程模块。

## 完成反馈（2026-09-22）

用户正确回答：结束程序的是 `main()` 中的 `return 1`；`CheckConfigFile()` 的 `return false` 只将失败结果交给调用方。

源码检查确认失败分支返回 `false`，入口在检查失败时返回 `1`。小问题：成功分支尚未输出 `path`，错误提示中的 `Chenk` 可改为 `Check`。这些不影响已经接入的退出逻辑。

本项基础练习可以结束。用户希望提高难度，下一项改造见 [日志配置加载失败处理](03-stage-1-config-load-failure.md)。运行结果目前依据用户报告，未收集原始终端输出。

## 任务目标

检查配置文件是否存在，缺失时停止启动。

前面的启动诊断只打印工作目录与配置路径；这次让检查结果真正控制服务能否启动。

修改位置：[src/server/Test.cpp](../../src/server/Test.cpp)。这个文件是服务端入口，`make test` 和 `make gdb_test` 都编译它，后者增加调试信息。因此本次修改直接参与实际服务启动。

## 1. 编写检查函数

在 `main()` 前面增加函数，先按骨架自行补完：

```cpp
bool CheckConfigFile(const std::filesystem::path& path)
{
    std::error_code error;
    bool exists = std::filesystem::exists(path, error);

    if (error)
    {
        // 输出：检查哪个路径失败，以及 error.message()
        // 返回 false
    }

    if (!exists)
    {
        // 输出：配置文件不存在，以及 path.string()
        // 返回 false
    }

    // 输出：配置文件存在，以及 path.string()
    // 返回 true
}
```

需要理解：

- `path` 是调用方传入的参数，同一个函数可以检查不同配置。
- `const ...&` 表示通过引用读取路径，不复制、不修改它。
- `bool` 返回值把检查结果交给 `main()`，由入口决定是否继续启动。

所需头文件为 `<filesystem>`、`<iostream>`、`<system_error>`，前面的练习已经要求添加。

## 2. 接入真实启动流程

在 `main()` 中、日志初始化之前接入：

```cpp
PrintStartupInfo();

if (!CheckConfigFile("../../log_system/logs_code/config.conf"))
{
    return 1;
}

if (!CheckConfigFile("Storage.conf"))
{
    return 1;
}

log_system_module_init();
```

保留日志初始化之后原有的元数据初始化和 HTTP 线程启动代码。

`return 1` 在 `main()` 中会结束整个程序，并向操作系统报告启动失败。此前 `PrintStartupInfo()` 内的 `return;` 只结束那个函数，要区分这两种位置。

```text
main
→ 打印启动信息
→ 检查日志配置 ──失败──→ 退出
→ 检查存储配置 ─失败──→ 退出
→ 初始化日志
→ 初始化元数据
→ 启动 HTTP 线程
```

检查函数由主线程执行，不创建新线程。参数引用在函数调用期间使用，局部变量在函数返回后销毁。

检查阶段继续使用 `std::cerr`，因为日志器尚未创建。

文件存在不代表可读，也不代表 JSON 合法；这次先只处理“是否存在”。存在性预检查也不能替代后续真正打开、读取配置时的错误处理。

## 3. 正常实验与 GDB 观察

```bash
cd /home/zhang/code/Poject/AsynLogSystem/src/server
make gdb_test
gdb ./gdb_test
```

```gdb
break CheckConfigFile
break log_system_module_init
run
```

用 `p path`、`bt`、`next` 观察参数、调用栈和分支。执行完 `exists` 调用后，可以用 `p exists` 和 `p error` 检查结果。

继续运行，应先后检查两份配置，最终命中日志初始化断点。观察完后可用 `kill`、`quit` 结束，无需继续启动 HTTP 服务。

不要执行 `make clean`：当前规则会删除存储目录、日志和 `storage.data`。

## 4. 安全故障实验

从项目根目录启动同一个二进制：

```bash
cd /home/zhang/code/Poject/AsynLogSystem
./src/server/gdb_test
echo $?
```

预期打印配置不存在，然后退出，退出码为 `1`。不用移动或删除任何配置文件，恢复正确工作目录即可恢复正常路径解析。

如果结果不符合预期，先记录实际输出和退出码，再检查：

1. 是否重新编译了修改后的代码？
2. `main()` 是否实际调用了检查函数？
3. 当前工作目录是什么？
4. 检查失败的分支是否返回 `false`？
5. `main()` 是否根据返回值执行了 `return 1`？

## 5. 本次验收

- 检查函数补充了明确的成功和失败返回值。
- 两份配置使用同一个函数检查。
- 正常目录启动能够到达日志初始化断点。
- 错误目录启动能够提前退出，退出码为 `1`。
- 能解释：`CheckConfigFile()` 返回 `false` 后，是哪一行代码让整个服务停止启动的？

完成后先检查代码与实验结果，再决定下一项实操，不自动进入线程模块。
