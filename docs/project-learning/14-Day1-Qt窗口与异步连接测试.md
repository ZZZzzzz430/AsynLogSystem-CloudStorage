# Day 1：Qt 窗口与异步连接测试

状态：今天的练习已布置，尚未实现或验收。依据：13 的 Qt 客户端主线方案；用户报告 Qt 环境已安装。当前仓库未找到客户端 CMake/Qt 工程，本次只提供任务，不代写核心代码。

## 今天只交付一个结果

做出一个能输入服务地址、点击“测试连接”、异步显示成功或失败的桌面窗口。请求使用**现有服务的 GET /**，不需要今天新写 health 接口。

预算 100 分钟，最多 120 分钟。只完成 M0 的客户端部分和第一条真实网络请求；服务端 CMake、Router、登录、列表、上传下载按钮下次再推进。今天不改旧服务业务代码，不解析它返回的 HTML。

## 1. 时间安排

| 时间 | 做什么 | 此步完成标志 |
|---|---|---|
| 0～20 分钟 | Qt Creator 创建 Widgets + CMake 工程，选择已安装 Kit | 能编译并打开空窗口 |
| 20～45 分钟 | 做地址输入、测试按钮、状态标签、日志区域 | 调整窗口大小时布局正常，点击能进入槽函数 |
| 45～80 分钟 | ApiClient 异步 GET，结果回传界面 | 正确服务返回 HTTP 状态，失败有原因 |
| 80～100 分钟 | 正常、失败、输入错误和重复点击验证 | 有验收记录，失败后可再次请求 |
| 100～120 分钟 | 处理实际阻塞或补 5 秒总超时 | 不再新增页面或其他功能 |

如果只有 60 分钟，先完成工程、界面和一次异步请求，剩余测试明确记作待补；不要为了凑进度用同步等待替代。

## 2. 工程组织：从今天就分开声明与实现

在项目根目录新增 `client/`，不要覆盖已有 `src/client/`。Qt Creator 可直接把 Widgets 工程创建在 client；向导生成的文件先沿用，不为文件名大小写和目录搬动耗时。

最小结构参考：

```text
client/
  CMakeLists.txt
  main.cpp
  MainWindow.hpp / MainWindow.cpp
  MainWindow.ui                    # 使用 Designer 时保留
  ApiClient.hpp / ApiClient.cpp
```

使用手写布局或 Designer 二选一，都可通过。构建产物放独立 build 目录，不提交 `.user`、本机 Qt 路径或构建产物。头文件只放声明，普通成员实现在 cpp；今天不抽 controller、repository 或多层继承。

CMake 自行补齐：C++17、Qt Widgets 和 Network 模块、可执行 target 与源文件、自动 moc（使用 .ui 则启用自动 uic）。Qt Creator 默认生成的 CMake 可以直接扩展，重点是让 ApiClient 真正参与编译、链接 Network。

本任务默认沿用 13 的 Qt 6 选择；记录实际 Qt 版本、编译器和 Kit。若安装的是 Qt 5，先在提交中说明，避免把 Qt6 的查找或 setup 命令原样套上。官方文档中的示例版本不是强制要求你重新安装相同版本。

今天允许直接打开 `client/CMakeLists.txt` 构建；根 CMake 与服务端 target 下一次再集成，避免配置原服务依赖占掉全部练习时间。

## 3. 界面与交互要求

| 控件 | 要求 |
|---|---|
| 服务地址输入 | 默认 `http://127.0.0.1:8081`，标签明确是服务地址 |
| 测试连接按钮 | 发起一次 GET /，请求进行中禁用 |
| 状态标签 | 未连接 → 连接中 → 服务可达 / 连接失败 |
| 只读日志区域 | 时间、请求地址、HTTP 状态或错误描述 |

今天地址只接受 http/https 的 origin：有 host，可选 port，不带用户名密码、query、fragment 或业务 path。拒绝非法地址时直接给用户提示，不发网络请求、不长时间禁用按钮。用 QUrl 的解析与字段检查，不只检查字符串是否包含 http。

服务端不在 Qt 所在机器时，127.0.0.1 指向的是客户端自己；应填写服务端可访问 IP 和端口。WSL/虚拟机的连通性单独确认，不把错误地址误判为 Qt 信号槽问题。不要把 0.0.0.0 当客户端目标地址。

## 4. 核心任务：实现 ApiClient

下面仅是职责与接口提示，不是已有实现或完整答案：

```cpp
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);
    void testConnection(const QUrl& serverUrl);
signals:
    void connectionFinished(bool ok, int httpStatus,
                            const QString& message);
};
```

自行决定成员与命名，至少包含：

- 一个可复用的 QNetworkAccessManager，生命周期覆盖请求；不要在函数里构造马上销毁。
- 一次请求一个 QNetworkReply，通过 finished 异步处理；不在 ApiClient 操作界面控件。
- 获取 NetworkError、HTTP 状态和必要的错误文本。今天以成功收到 GET / 的 200 响应为“服务可达”；这不证明是正确版本服务或所有业务健康。
- MainWindow 创建 ApiClient 并连接结果信号，完成后恢复按钮状态。连接失败后可以再次测试。
- reply 使用 deleteLater 清理。若接 errorOccurred，只记录信息或更新内部原因，最终界面结果统一在 finished 收口，避免失败提示两次。

总超时建议 5 秒：为每个 reply 设置一个单次 QTimer，到期调用 abort；记录超时原因，使其与用户取消/连接拒绝区别。reply 完成即停止定时器，定时器和 reply 的生命周期绑定，避免旧请求定时器中止下一次请求。若超出今天时间盒，明确标记总超时未完成。

网络请求本身使用 Qt 异步 API，今天不需要 QThread、sleep、waitForFinished 或局部 QEventLoop。点击后必须让 GUI 事件循环继续运行。界面能响应与服务器能及时回复是两件事。

Qt 官方参考：[QNetworkAccessManager](https://doc.qt.io/qt-6/qnetworkaccessmanager.html)、[QNetworkReply](https://doc.qt.io/qt-6/qnetworkreply.html)。前者用于发请求，后者提供完成、错误与清理所需接口。不要为了跟随教程把所有 HTTP 实现都写进按钮槽。

## 5. 启动已有服务

若服务当前没有运行，在 Linux 服务端从仓库根目录执行：

```bash
make -C src/server gdb_test
cd src/server
./gdb_test
```

先以实际配置端口确认首页可达；默认 Storage.conf 为 8081，有自定义配置则按实际值。今天只有 GET 列表请求，不上传或删除文件；运行仍可能创建日志。不要启动多个服务争用端口，不使用会清理数据的 make clean。

先用浏览器/curl 验证服务地址，再用 Qt 测。服务端若运行在独立实验目录则记录真实端口，客户端不要继续使用默认值猜测。

## 6. 验收表：自己填写实际结果

| 用例 | 预期 | 实际结果 |
|---|---|---|
| 正确地址，服务运行 | 200，显示可达，按钮恢复 | 待填 |
| 可确认无监听的端口 | 明确连接失败，按钮恢复 | 待填 |
| 非法地址，例如 `abc` | 本地提示输入错误，不发请求 | 待填 |
| 请求进行中再次点击 | 不产生重复请求 | 待填 |
| 失败后改回正确地址 | 再次成功，不需要重启客户端 | 待填 |
| 请求期间移动/缩放窗口 | UI 继续响应 | 待填 |
| 无响应/延迟测试服务超过 5 秒 | 若实现总超时，显示超时且可恢复 | 待填/未做 |

连接拒绝通常立即返回，不能拿它证明 5 秒超时已经验证。若没有合适的受控延迟服务，今天超时项可留待下一次提供本地测试桩，不扫描陌生地址制造超时。请求正常很快完成时，也不要仅凭一次缩放推断所有情况下都不卡。

这一轮以交互和错误恢复验收，不要求写自动化测试。不要花时间做皮肤和图标；优先完成所有请求分支恢复按钮。

## 7. 交付与评审

交回 client 源码和 CMake；在本篇末尾追加“我的结果”：Qt/系统/Kit、实际构建方式、验收表结果、一个遇到的问题及处理方式。可以附截图，但窗口截图不代替实际 HTTP 状态和错误结果。

先自行回答三个问题：

1. 为什么 manager 不能是 testConnection 内立刻销毁的局部对象？
2. UI 没有 QThread，网络请求为什么仍能异步完成？
3. 如果 errorOccurred 和 finished 都更新“失败”，会出现什么交互问题？你在哪里统一收口？

我会按结构边界、网络对象生命周期、失败后恢复及解释能力评审。今天通过后，下一次增加文件选择、旧接口上传及进度；不是立即做登录或 Router。

构建参考：[Qt CMake 入门](https://doc.qt.io/qt-6/cmake-get-started.html)。本任务引用文档说明 API 和构建方法，不代表今天已执行任何客户端测试。
