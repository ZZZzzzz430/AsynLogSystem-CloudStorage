# 阶段 2：上传下载闭环与上传入口校验

进度更新（2026-10-09）：用户报告已过完本篇并要求开始下一阶段，现进入 07 的异步日志学习。此状态来自用户反馈；本次未重新执行 HTTP 验收，尚未附具体实验输出，不将其记作助手实测通过。以下历史任务与验证记录保留。

日期：2026-09-28。状态：已检查源码，本次布置实验与改造；未运行 HTTP 实验，未替用户实现核心代码。

最新状态（2026-10-08）：按用户后续授权已修正并接入上传入口校验，调试版与普通版编译通过；HTTP 实验由用户次日执行，尚未标记运行验收通过。原始任务与此前评审保留在下文。

本阶段按 2～3 小时时间盒推进：20 分钟追主链，30 分钟正常实验和断点，60 分钟改造，30 分钟回归与复盘。完整配置校验留为待办，不阻塞主线。此前配置修复记录是当时的验证结果，不代表当前所有代码均已重新验证。

这次提高难度的方式：从单个检查函数升级为一个完整请求入口的工程改造。你自己设计校验顺序、失败出口和边界测试，不再逐行照写。只聚焦普通上传；压缩、完整断点续传和日志内部实现留到后面。

## 1. 先建立问题：HTTP 200 能证明什么？

文件存储服务必须把请求字节、磁盘文件和下载索引联系起来。只有写入文件，没有元数据，列表和下载可能找不到它；只有元数据，没有文件，下载仍会失败。

本阶段需要证明：请求被接收 → 文件内容正确 → 元数据可查 → 下载字节一致。HTTP 200 本身不能证明这条链成立，更不能证明数据已持久化到物理磁盘。

## 2. 源码范围与对象

- [Test.cpp](../../../src/server/Test.cpp)：`main → service_module`，创建 HTTP 线程和全局 `data_`。
- [Service.hpp](../../../src/server/Service.hpp)：`RunModule / GenHandler / Upload / ListShow / Download`。
- [Util.hpp](../../../src/server/Util.hpp)：`FileUtil::SetContent`，只追普通文件写入。
- [DataManager.hpp](../../../src/server/DataManager.hpp)：`StorageInfo::NewStorageInfo`，`Insert / Storage / GetOneByURL`。

`Service s` 是 `service_module` 的栈对象，在事件循环返回后析构。`server_port_` 用于监听；`server_ip_` 虽被读取，实际绑定代码使用 `0.0.0.0`；`download_prefix_` 被缓存，但路由判定仍硬编码 `/download/`。不要把配置存在等同于配置实际生效。

`data_` 在主线程 `new`，HTTP 线程通过它操作索引；当前入口未显式删除它。`table_` 是 URL 到 `StorageInfo` 的映射；`rwlock_` 保护映射访问；`storage_file_` 指向元数据文件；`need_persist_` 让启动加载时插入数据不触发重复持久化。锁不等于整个“写文件 + 改索引 + 写元数据”是原子事务。

`FileUtil` 保存文件路径，`SetContent` 内创建局部 `ofstream`，关闭时释放文件资源。`Upload` 中的 `content / filename / storage_path / info` 属于当前回调调用，不是跨请求的共享变量。

## 3. 真实调用链、数据流和线程

```text
主线程 Test.cpp:main
  ├─ 初始化日志器、备份线程池
  ├─ new DataManager：加载索引
  └─ 创建 t1，随后 join

HTTP 线程 Test.cpp:service_module
  → Service.hpp:RunModule → event_base_dispatch
  → GenHandler(req)：根据路径分派
  → Upload(req)
      evbuffer 请求体 → std::string content（复制 len 字节）
      FileName 请求头 → Base64 解码 → filename
      StorageType=low → ./low_storage/ + filename
      Util.hpp:FileUtil::SetContent → ofstream 写文件
      DataManager.hpp:StorageInfo::NewStorageInfo → 文件属性、下载 URL
      DataManager.hpp:Insert → 写锁内更新 table_ → 解锁
      DataManager.hpp:Storage → GetAll 复制索引快照 → JSON → storage.data
      evhttp_send_reply：发送响应
  → 后续 GET /：ListShow → GetAll → HTML
  → 后续 GET /download/文件名：Download → GetOneByURL → open → evbuffer_add_file → 响应

日志消费线程：消费日志 Buffer，输出日志文件
备份线程池：处理重要日志备份；本阶段不深入
```

文件写入、索引序列化都同步发生在 HTTP 线程，不会因为使用异步日志就变成异步文件存储。回调中的 Info 日志进入日志模块，是另一条数据流。

`ofstream` 写入最终会涉及内核文件接口，但 C++ 缓冲、内核页缓存和物理磁盘是不同层次。源码没有在该路径调用 `fsync`；不能声称响应成功意味着断电也不会丢失。

当前只有一个 HTTP 事件循环线程。因此多个客户端可以同时建立连接，业务回调仍在这个线程上执行。不要把线程池误认成 HTTP 工作线程池。

## 4. 独立实验目录

从项目根目录执行，复制最小运行环境，避免覆盖已有文件。以下命令供你执行，本次尚未运行。

```bash
make -C src/server gdb_test
export LAB_ROOT=$(mktemp -d /tmp/asynlog-stage2.XXXXXX)
mkdir -p "$LAB_ROOT/src/server" "$LAB_ROOT/log_system/logs_code"
cp src/server/gdb_test src/server/Storage.conf src/server/index.html "$LAB_ROOT/src/server/"
cp log_system/logs_code/config.conf "$LAB_ROOT/log_system/logs_code/"
python3 - "$LAB_ROOT/src/server/Storage.conf" <<'PYCONF'
import json, sys
p = sys.argv[1]
with open(p) as f:
    config = json.load(f)
config['server_port'] = 18081
with open(p, 'w') as f:
    json.dump(config, f, indent=2)
PYCONF
printf '%s\n' "$LAB_ROOT"
cd "$LAB_ROOT/src/server"
./gdb_test
```

另开终端，将下面路径替换成上一步打印的真实路径：

```bash
export LAB_ROOT=/tmp/asynlog-stage2.实际后缀
export BASE=http://127.0.0.1:18081
ss -ltnp 'sport = :18081'
printf 'stage2 roundtrip\n' > "$LAB_ROOT/payload.txt"
export FILE_B64=$(printf %s stage2.txt | base64 -w0)
curl --max-time 10 -sS -D "$LAB_ROOT/upload.headers" \
  -H "FileName: $FILE_B64" -H 'StorageType: low' \
  --data-binary @"$LAB_ROOT/payload.txt" "$BASE/upload"
curl --max-time 10 -fsS "$BASE/" -o "$LAB_ROOT/list.html"
curl --max-time 10 -fsS "$BASE/download/stage2.txt" -o "$LAB_ROOT/download.txt"
cmp "$LAB_ROOT/payload.txt" "$LAB_ROOT/download.txt"
sha256sum "$LAB_ROOT/payload.txt" "$LAB_ROOT/download.txt"
rg 'stage2.txt' "$LAB_ROOT/list.html" "$LAB_ROOT/src/server/storage.data"
```

预期：上传 200；列表和元数据含目标文件；`cmp` 退出 0；两个摘要一致。上传使用原始请求体，不使用 `curl -F`，因为代码没有解析 multipart 表单。暂不发送 Range 请求；现有返回 206 的分支不能证明实现了区间下载。

## 5. GDB：证明执行线程与数据变化

先在运行服务的终端 Ctrl+C 停止该实验进程，再从同一个实验工作目录启动：

```text
gdb ./gdb_test
set pagination off
break storage::Service::Upload
break storage::FileUtil::SetContent
break storage::DataManager::Insert
run
```

另一个终端重新发送正常上传。命中 `Upload` 后执行 `bt`、`info threads`，记下当前线程号。用 `next` 走过对应变量初始化后观察：

```text
p len
p content.size()
p filename
p storage_type
p storage_path
```

在初始化之前查看变量可能看到无效值。命中 `SetContent` 时用 `bt`、`p filename_`、`p len` 区分“文件内容写入”和“storage.data 写入”，同一个函数会承担两种用途。命中 `Insert` 后执行 `p info.url_`、`p info.storage_path_`，比较各断点线程号。

最后执行 `thread apply all bt`：找到主线程的 join、HTTP 回调，以及日志相关线程。启动阶段也可能先命中 `SetContent`，根据调用栈判断后继续，不能把第一次命中一律当成上传。

## 6. 故障实验：先保存现象，再定位

保持其他字段有效，仅省略 FileName：

```bash
curl --max-time 5 -v -H 'StorageType: low' \
  --data-binary @"$LAB_ROOT/payload.txt" "$BASE/upload"
```

现在已知现象是什么？记录 HTTP 状态或连接错误、服务是否仍监听、终端诊断；没有 HTTP 响应就写“未收到响应”，不要编造为 500。

排查顺序：

1. 假设是请求拒绝、线程卡住或进程异常退出，分别用响应、`ss` 和 GDB 栈取证。
2. GDB 加 `catch throw`，重新运行并复现；若异常停住，执行 `bt`，定位第一个项目代码栈帧。
3. 在 `Upload` 里观察请求头查询的返回值与后续字符串构造，比较正常请求和缺字段请求。
4. 暂不改日志、网络库或元数据模块；先证明错误发生在哪一步。

若本机表现不同，保留实际栈和输出。故障表现在此不预先写成已验证结论。

## 7. 核心改造：为 Upload 建立输入契约

只修改 `Service.hpp` 的上传入口及必要的小辅助函数，核心实现由你编写。要求：

| 输入 | 改造后行为 |
|---|---|
| 非 POST 请求 `/upload` | 405，并返回 `Allow: POST` |
| 空请求体 | 400 |
| 缺失或空 FileName、StorageType | 400，不能使进程退出 |
| StorageType 不属于 low/deep | 400 |
| 解码后文件名为空、为 `.`/`..`、包含 `/`、反斜杠或 NUL | 400 |
| 请求体大于 1 MiB，即 1048576 字节 | 413 |
| 合法普通上传 | 保持原有存储、列表和下载流程 |

校验失败必须在创建存储目录、写入文件和更新索引之前结束；每条失败路径只能回复一次，并立即返回。请求头先保留指针并检查有效性，再构造字符串。方法读取可在本机 `/usr/include/event2/http.h` 查询 `evhttp_request_get_command`，与 `EVHTTP_REQ_POST` 比较。不要依赖可能不存在的状态码宏，必要时使用数值状态码。

超限判断放在获取 `len` 之后、分配 `content` 之前。注意这只避免应用层再次分配超大字符串；请求体已经到达 libevent 的输入缓冲，不能把它宣传成完整网络接收限流。

本次要求校验“解码后的文件名”，不要求实现严格 Base64 校验器；不要求完整跨平台文件名策略。这里只建立必要边界，不把它称为完整文件服务安全方案。

最少验收：表中每个拒绝分支、恰好 1 MiB 成功、1 MiB + 1 字节返回 413；每次拒绝后合法上传仍成功。可用 `truncate -s 1048576 文件路径` 生成边界文件。分别给两个边界请求使用唯一文件名。

对失败请求，比较前后实验目录文件列表与 `storage.data` 摘要，确认没有新增文件和索引；日志文件增加是正常的。先做正常上传生成元数据，再保存基线。修改后重新 `make -C src/server gdb_test`，停止实验进程，再复制新二进制到实验目录运行，避免验证旧程序。

## 8. Linux、并发与性能观察

从 `ss -ltnp` 输出取得实验进程 PID，替换下面的 PID：

```bash
ps -L -p PID -o pid,tid,stat,pcpu,comm
```

看 TID 区分线程，看 STAT 和 %CPU 判断采样时状态。空闲时多数线程休眠并不异常；请求慢而 CPU 低也不能单凭这一点断定磁盘慢。结合 `thread apply all bt` 确认是否等待锁、网络或其他操作。

如需观察文件 IO，停止原进程后从实验目录启动：

```bash
strace -f -tt -T -e trace=openat,write,close,fsync,fdatasync \
  -o "$LAB_ROOT/io.trace" ./gdb_test
```

看 TID、文件打开路径、返回值和调用耗时；日志文件与上传文件都可能出现 write，要通过 openat 返回的 fd 和线程关联。短 write 耗时不等于物理磁盘已完成。未安装 strace 时先用 GDB 完成主实验，不让工具安装阻塞改造。

并发实验：用 1、2、6 个客户端上传各自唯一命名的同样大小文件，每次用 curl 的 `-w '%{http_code} %{time_total}\n'` 保存状态和时间，再逐个下载校验。正常预期是内容正确，延迟可能增加；具体吞吐和延迟必须实测，不预填数字。

100 个客户端暂作推演：竞争事件循环处理时间、文件 IO、元数据全表序列化、日志 Buffer；当前串行回调不代表这些资源没有瓶颈。不要直接把吞吐下降归因为 `table_` 写锁。

若未来增加 HTTP 工作线程，`table_` 有锁也不足以保证同名文件写入和 `storage.data` 持久化安全。当前同名上传即使串行，也会覆盖相同文件和 URL 索引；本阶段并发测试使用唯一文件名。

## 9. 范围与通过标准

阅读还发现：`SetContent` 的部分写失败路径仍返回 true，`Storage` 写失败后也可能返回 true，上传未检查若干元数据操作返回值。它们属于后续“存储失败传播”问题，本次不自动替你修复，也不因此宣称上传具备事务性。不要用已有 200 响应证明异常磁盘情况下的数据可靠性。

提交给我三项证据：

1. 正常闭环结果与缺失 FileName 时的现象、关键调用栈。
2. 你修改的代码，以及边界测试的实际状态码和失败无副作用证据。
3. 以下三题的回答，每题 3～5 句话：
   - 6 个客户端同时上传时，究竟哪个线程执行 SetContent？你怎么证明？
   - 为什么在 content 分配前检查大小，仍不等于限制了全部接收内存？
   - 如果响应 200，但重启后列表没有该文件，你沿哪条链查证？

完成这些再评审本模块；当前不标记通过。通过后下一模块是异步日志主链路，本次不提前展开。

## 10. 2026-10-08：Service.hpp 修改评审

结论：当前修改尚不满足本文件第 7 节的入口校验要求，暂不通过。证据为当前源码、Git 差异，以及实际执行 `make -C src/server gdb_test` 的结果（退出码 2）。本次未修改业务代码；未运行当前版本的 HTTP 验收，因为当前版本无法编译。已有二进制不能证明这些修改生效。

### 编译阻塞：实际编译确认的四处错误

| 当前位置 | 问题 | 修正方向 |
|---|---|---|
| `Service.hpp:119` | `UploadParams` 结构体结尾缺分号 | 以 `};` 结束 |
| `Service.hpp:136` | 条件末尾为 `filename == ".." ||)`，缺少右操作数 | 删除末尾多余的 `||` |
| `Service.hpp:124、154` | 枚举声明为 `MethodNotAllowd`，使用为 `MethodNotAllowed` | 统一为 `MethodNotAllowed` |
| `Service.hpp:188` | `base64_decode(name)` 调用歧义：C++17 下同时存在 `std::string` 和 `std::string_view` 重载 | 指针校验成功后显式传入 `std::string(name)` |

### 核心行为缺口：校验尚未进入请求链路

`GenHandler → Upload` 的调用链仍直接执行原有上传逻辑，`Upload` 没有调用 `ValidateUploadRequest`，也没有使用 `UploadParams`。因此，即使仅修复以上编译错误，新校验仍不会生效：

- 方法检查被注释，不能保证非 POST 返回 405 和 `Allow: POST`。
- 原有 `Upload` 在第 232 行分配 `content`，此前没有大小限制，不能保证超限返回 413。
- 第 241、246 行仍将请求头返回指针直接用于构造字符串；缺失头的异常风险仍在。空头也未在实际处理链路拒绝。
- 解码后的文件名仍未经 `IsValidFilename` 检查就拼入存储路径，不能保证非法文件名在创建目录、写文件和更新索引前被拒绝。
- `UploadError` 还没有映射到 HTTP 响应；返回枚举本身不会回复客户端。

已有辅助函数的拆分方向合理：先检查方法、请求体和大小，再检查请求头、存储类型、解码后文件名；`len > 1024 * 1024` 的比较保留了恰好 1 MiB 的合法边界；文件名检查覆盖了任务要求的类别。这些属于源码设计评价，尚不是运行通过。

### 下一次修改与验收顺序

1. 先修复四处编译错误。
2. 在 `Upload` 开头、分配 `content` 之前调用校验函数。失败时将方法错误映射为 405（加 `Allow: POST`），超限映射为 413，其余输入错误映射为 400；每个失败请求回复一次并立即返回。
3. 成功后使用 `params.filename / storageType / contentLength` 继续原上传流程，替换旧的未经检查的请求头读取与重复解码。
4. 重新编译，在独立实验目录用新二进制执行第 7 节全部拒绝分支、1 MiB 和 1 MiB + 1 边界，以及正常上传、列表、下载内容比对。检查每次拒绝没有改变存储文件与索引，并确认合法请求仍可继续成功。

补充健壮性问题：本仓库 `base64.cpp` 的解码器遇到部分非法字符或不完整输入会抛出 `std::runtime_error` / `std::out_of_range`。当前校验函数未捕获。第 7 节不要求实现严格 Base64 校验器，但建议在解码边界捕获这两种输入异常并返回文件名错误，避免异常逃出 HTTP 回调；这一点是补充建议，不将严格 Base64 校验追加为原任务要求。

本次只完成代码评审与编译验证；正常闭环、故障调用栈、边界无副作用证据和第 9 节三题回答尚未在本次验证中完成，不标记整个学习模块通过。

## 11. 2026-10-08：按授权修正，等待 HTTP 实验

用户要求助手修正到能够编译且符合入口要求，HTTP 测试等内容留到次日由用户完成。本次已修改 `Service.hpp`：

- 修正结构体分号、条件表达式、枚举拼写和 Base64 重载歧义。
- 在 `Upload` 开头调用 `ValidateUploadRequest`；失败回复一次并立即返回，先于内容字符串分配、目录创建、文件写入和索引更新。
- 非 POST 映射为 405，并添加 `Allow: POST`；大于 1048576 字节映射为 413；空请求体、缺失或空头、非法类型、非法解码后文件名映射为 400。
- 成功路径使用校验后的参数，移除旧的直接从请求头指针构造字符串与重复解码。解码器的输入异常转换为文件名错误。
- 配置 libevent 允许其支持的全部已知方法进入路由，避免 PUT、PATCH 等方法在到达上传入口前被库的默认方法策略拒绝。此设置作用于整个 HTTP 服务；上传入口仍只接受 POST。
- 请求内容复制使用 C++17 的可写 `content.data()`，检查实际复制字节数；长度日志格式改为 `%zu`。

实际验证：`make -C src/server gdb_test` 和 `make -C src/server -B test` 均退出 0；`git diff --check` 通过。`gdb_test`、`test` 已重新生成。本次未启动服务、未执行 HTTP 测试，也未修改存储数据。

明日按第 4、7 节进行独立目录验收，务必复制本次新生成的二进制。重点记录：

1. 合法上传 200，列表和元数据可查，下载后 `cmp` 与 SHA-256 一致。
2. GET、PUT、PATCH 等非 POST 请求 `/upload` 返回 405，响应头包含 `Allow: POST`。
3. 空体、两个头分别缺失或为空、非法类型，以及解码后为空、`.`、`..`、含 `/`、反斜杠、NUL 的文件名分别返回 400。可以补测非法 Base64 字符和单字符输入，确认进程仍存活。
4. 恰好 1048576 字节成功，1048577 字节返回 413。
5. 每次拒绝前后比较存储目录与 `storage.data`，确认无变化；随后发送合法请求，确认服务继续可用。

当前结论：源码已落实本次入口契约、编译通过，运行行为待实测；整个阶段仍待实验和复盘。请求体大小检查仍发生在 libevent 已接收请求体之后；存储失败传播问题仍按第 9 节记录为后续范围。
