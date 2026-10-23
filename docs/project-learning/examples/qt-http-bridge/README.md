# Qt HTTP 传输参考工程

本示例配套 [接口架构设计](../../14-Qt与存储服务接口架构参考.md)，独立于正式 `Client/` 工程。面向当前 legacy 服务：POST /upload、GET /download/名称、FileName Base64、StorageType=low、非空且最多 1MiB。它没有实现拟定的 v1、登录、列表或队列。

- [LegacyTransfer.hpp](LegacyTransfer.hpp)：可供 Widgets 调用的传输对象。
- [LegacyTransfer.cpp](LegacyTransfer.cpp)：异步上传、QFile 生命周期、分块下载到 QSaveFile、状态/长度检查、取消与超时。
- [main.cpp](main.cpp)：不需要 GUI 的最小练习入口。

## 构建

Qt6 6.5+、C++17、CMake 3.21+；选择与 Qt 库匹配的编译器。示例没有依赖 Widgets。

```bash
cmake -S docs/project-learning/examples/qt-http-bridge -B build/reference-qt-http-bridge \
  -DCMAKE_PREFIX_PATH=<Qt的安装前缀>
cmake --build build/reference-qt-http-bridge
```

Windows 当前仓库的 Qt Kit 为 `E:/Qt/6.8.3/mingw_64`、编译器 `E:/Qt/Tools/mingw1310_64/bin/g++.exe`；可在 Qt 命令环境或 Creator 中打开本目录 CMakeLists。运行时需让 Qt bin 和编译器 bin 在 PATH 内。

## 运行

先在 Linux 以当前配置启动真实存储服务；Windows Qt 使用实际可达的服务 origin。下面用本机地址作示例。

```text
bridge-demo upload http://127.0.0.1:8081 path/to/demo.bin
bridge-demo download http://127.0.0.1:8081 demo.bin path/to/download.bin
```

命令结果为 0 表示服务确认/下载提交，1 表示传输失败，2 表示参数或本地预检失败。带空格路径用引号。退出码 0 不替代两端文件摘要比对，也不证明现有服务器持久化错误已经修复。

Windows PowerShell 可以比较：

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath 'path/to/demo.bin','path/to/download.bin'
```

`BRIDGE_CANCEL_AFTER_MS` 环境变量可让示例在请求发起后自动取消，例如 100；快速请求可能在取消前已完成。总期限 30 秒，无活动期限 15 秒。不要在正式 UI 直接把下载目录或覆盖决策交给远端文件名；本示例的目标路径由调用者明确指定。

UI 集成时：在同一 GUI 线程创建一个 LegacyTransfer，连接 progress/finished 到有 QObject context 的槽；保存返回 reply 的 QPointer 用于 abort。示例 progress/finished 不带任务 ID，命令入口一次只跑一个任务；多传输场景应加任务关联再使用，不能按当前选中行猜任务。

## 验证记录

2026-10-21：Windows、Qt 6.8.3、匹配的 MinGW GCC 13.1、CMake/Ninja，已配置并编译链接成功。

使用 [verify_reference.py](verify_reference.py) 启动仅监听 loopback 的临时 Python HTTP 服务，执行客户端检查，**17 项通过**：二进制和中文/空格/#/% 文件名上传下载；空文件、超限、无效地址、源文件缺失、目标不可写；上传 500；下载 500、206、截断正文、超限正文；取消、无活动超时、连接拒绝。失败下载确认原目标内容保持不变。CLI 输出固定为 UTF-8 stderr，便于 Windows 管道捕获。

重复检查的方法（需先构建，并设置 Qt/编译器运行库 PATH）：

```text
python docs/project-learning/examples/qt-http-bridge/verify_reference.py <bridge-demo完整路径> --deadline
```

`--deadline` 启用约 15 秒的无活动超时检查；不加它则跳过这一项。30 秒总期限在代码中实现，本次未另做持续传输触发总期限的测试。模拟服务器只验证客户端协议和保存行为，**真实 Linux C++ 服务回归、新协议、账户隔离、TLS 与崩溃恢复不属于已验证范围**。
