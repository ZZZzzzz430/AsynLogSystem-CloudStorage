# bug记录

## 2026-10-16：Service 回调计时器修复

范围：本次新增的 ScopeTimer 和 GenHandler 接入。修复前已实际执行 `make -C src/server gdb_test`，退出 2，复现下列编译错误；修复后调试版、普通版编译及运行回归通过。源码使用 `BUG-S01`～`BUG-S04` 注释标记对应修正处。

### 错误位置与修正

| 编号 | 位置 | 原错误/问题 | 原因与修正 |
|---|---|---|---|
| BUG-S01 | [ScopeTimer 构造及复制声明](../../../src/server/Service.hpp#L31) | `ScpoeTimer` 与类名 `ScopeTimer` 不一致 | C++ 构造函数必须和类名完全一致；复制构造参数和赋值返回类型也需匹配。统一拼写，保留禁止复制，避免同一计时重复记录 |
| BUG-S02 | [ScopeTimer 析构](../../../src/server/Service.hpp#L39) | `try () {}`、`catch {}` 不合法，且异常直接吞掉 | 改为 `try { ... } catch (...) { ... }`；字符串构造与日志输出均置于 try 中。保持 noexcept，异常时直接用 fputs 写 stderr，不递归调用日志器 |
| BUG-S03 | [计时日志输出](../../../src/server/Service.hpp#L54) | cout 中拼接 `[INFO]` 不等于进入项目日志；超过阈值还会重复输出 INFO 和 WARN | 通过注册的 asynclogger 输出。超过 100ms 只记录 WARN，其余只记录 INFO；统一包含 path、cost_ms、thread。使用 `%s` 传入完整消息，路径中的百分号不被当作格式符 |
| BUG-S04 | [GenHandler 路径与计时器](../../../src/server/Service.hpp#L140) | 记录原始 URI，路由却用另一份解码路径；从可能为空的 path 指针直接构造 string | 检查 uri/path，空路径回退 `/`，解码后给路由与计时器共用。计时器持有字符串副本，不保存 req 指针；覆盖上传校验提前 return 后的回调结束 |

行号以本次文件为准，后续编辑可能移动；可按 BUG 编号搜索。构造类名错误会进一步导致“只有构造函数能使用初始化列表”“找不到接受字符串的构造函数”等连锁诊断，先修最早的命名错误，不需要为每条诊断新增一个补丁。

其他整理：移除重复 utility include 和多余注释；将 TIME_EXPECTED 全局宏改成计时器私有的 constexpr 阈值，避免宏名称影响其他代码；补齐 sstream、cstdio 依赖。原来的 `endl` 依赖其他头文件引入命名空间，本次不再使用该写法。

### 实际验证

1. `make -C src/server gdb_test`：修复后实际重新编译，退出 0。
2. `make -C src/server -B test`：实际重新编译，退出 0。
3. `git diff --check -- src/server/Service.hpp`：退出 0。
4. 独立 HTTP 环境：`/tmp/service-review-bml7j08d/src/server`，配置使用 18089 端口。复制二进制、配置及页面，未复用已有上传数据；实验结束已终止本次服务。

HTTP 脚本 `/tmp/service_http_review.py` 的实测结果：

```text
GET /                  200
POST /upload（缺头）   400
GET /upload            405
GET /missing           404
GET /?q=1              200
callback records       6
```

计入一次启动就绪 GET，加上上述五次请求，共 6 条 callback 日志；两次 /upload 提前结束均有记录，每条包含耗时和线程字段。查询参数不再混入计时路径，`/?q=1` 对应路径 `/`。

独立计时探针 `/tmp/service_timer_review.cpp`：直接包含真实 Service 计时器实现，并使用项目日志输出，测试结果：

```text
/probe-fast%20  INFO，一条
/probe-slow     WARN，一条（受控 sleep 130ms）
/probe-early    INFO，一条（提前 return）
/probe-unwind   INFO，一条（异常展开）
```

探针还通过编译期断言验证计时器不可复制/赋值、析构 noexcept。脚本最终输出 ALL PASS 并退出 0。第一次 HTTP 尝试因沙箱禁止 socket 而退出；获得工具权限后在另一临时目录完成上述 HTTP 实测。沙箱失败不属于本次业务代码故障。

### 结论与验证边界

**本次计时器编译错误及接入问题已修复，所列回归通过。** 慢路径 WARN 是独立计时探针验证，没有在最终上传代码保留人为 sleep；09 的“一个上传阻塞其他请求”的延迟对照仍待用户执行，不能用本次测试替代该证据。

计时从解码路径后开始，到回调作用域结束计算；不表示上传全程或响应全部发送完毕。输出仍通过现有日志 Push，析构 noexcept 只约束异常传播，不保证日志满时不会等待。兜底分支已实现，但本次没有注入日志器故障验证其运行路径。

这不是 Service 的全部问题清零：下载未命中元数据后的处理、错误回复后继续执行、深度解压与同名普通文件冲突、Range 不完整等既有问题仍未在本次修改和验证范围内。按当前主线先完成计时任务，不把这些未修问题记作已解决。
