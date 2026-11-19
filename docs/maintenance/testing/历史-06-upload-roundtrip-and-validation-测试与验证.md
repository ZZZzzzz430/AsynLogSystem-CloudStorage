# 06-upload-roundtrip-and-validation：测试与验证

对应学习笔记：[06-upload-roundtrip-and-validation](../../project-learning/笔记/06-upload-roundtrip-and-validation.md)。

以下内容从原笔记按章节迁移，章节编号和历史状态保留。命令按正文指定的工作目录执行；未注明时以仓库根目录为准。整理目录不代表重新执行测试。

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

## 10. 2026-10-08：Service.hpp 修改评审

原因、修改与当时的验证边界见[修复与评审记录](../fixes/历史-06-upload-roundtrip-and-validation-修复与评审.md)。

## 11. 2026-10-08：按授权修正，等待 HTTP 实验

原因、修改与当时的验证边界见[修复与评审记录](../fixes/历史-06-upload-roundtrip-and-validation-修复与评审.md)。
