# 历史学习笔记索引

本目录归档 01～12 及 bug记录。后续扩展见[上层目录](../README.md)。原阶段记录保留，部分状态为当时的历史记录。


当前执行路线：[13-Qt客户端主线升级实施方案](../13-Qt客户端主线升级实施方案.md)——2026-10-20 用户改为 Qt 客户端驱动，服务端 API、认证、Router、数据与 CMake 按里程碑配套升级；先 M0/M1 可用客户端。

上一轮路线：[12-四周工程强化与上传追踪](12-四周工程强化与上传追踪.md)，保留历史任务与验收，执行优先级已由 13 调整。

总复习入口：[11-最终总结与后续安排](11-最终总结与后续安排.md)（截至 2026-10-18，汇总 01～10 与 bug记录，区分已验证、用户报告和待办，并给出后续顺序）。

本目录保存项目学习过程中的阶段讲解、实操任务、实验步骤和反馈，由助手持续维护。

文件序号表示文档顺序，不代表阶段编号。同一主题的补充或纠正更新原文档，新主题新增文档。

| 文档 | 所属阶段 | 内容 |
|---|---|---|
| [01-project-map-and-stage-1.md](01-project-map-and-stage-1.md) | 项目总览、阶段 1 | 工程地图、学习路线、初始化顺序与工作目录实验 |
| [02-stage-1-config-file-check.md](02-stage-1-config-file-check.md) | 阶段 1：启动与配置 | 检查配置文件是否存在，缺失时停止启动 |
| [03-stage-1-config-load-failure.md](03-stage-1-config-load-failure.md) | 阶段 1：启动与配置进阶 | 日志配置读取、解析、校验失败时阻止服务初始化 |
| [04-startup-review-and-interview-priorities.md](04-startup-review-and-interview-priorities.md) | 阶段复盘、求职优先级 | 完成边界、Linux 速查、面向求职的学习路线调整 |
| [05-config-bugfix-review.md](05-config-bugfix-review.md) | 阶段 1：配置纠错 | 空字符串误解析、同名函数返回值、异常路径修复与验证 |

| [06-upload-roundtrip-and-validation.md](06-upload-roundtrip-and-validation.md) | 阶段 2：上传下载闭环 | 入口校验修正、编译通过；2026-10-09 用户报告完成，具体运行输出未附，助手未复验 |
| [07-async-log-handoff-and-backpressure.md](07-async-log-handoff-and-backpressure.md) | 阶段 3：异步日志，学习块 1 | 2026-10-12 修正后独立回归通过：容量边界、暂满等待、多生产者；补测停止唤醒和排空通过，见第 11 节范围 |
| [08-slow-consumer-and-bounded-push.md](08-slow-consumer-and-bounded-push.md) | 阶段 3：异步日志，学习块 2 | 2026-10-14 作用域及拼写修正后，PushFor 独立回归和原 Push 回归通过；故障栈、性能与问答待补 |

| [09-review-backlog-and-http-blocking.md](09-review-backlog-and-http-blocking.md) | 复习清单、阶段 4：HTTP 阻塞边界 | 前部分标记 07/08 待补证据，不阻塞推进；后部分开始事件循环阻塞实验与回调耗时改造，待实操 |

文档中的任务与预期结果不代表已经完成运行验证。后续反馈应注明证据来源。

新增：[10-project-story-and-evidence.md](10-project-story-and-evidence.md)——阶段 5：项目表达与证据整理。09 首轮主线可收束，延迟对照等保留复习；本块先完成介绍、改造案例和证据边界，待用户作答评审。

错误修复专题：[bug记录](bug记录.md)——2026-10-16 Service 计时器编译与接入问题、源码标记、编译及独立 HTTP/计时回归结果。


2026-10-14 推进约定：按用户要求先走项目主线，再回头复习。08 核心功能测试通过，缺失的故障栈、性能和问答统一列入 09 前部分，不记作已经完成。
