# cget 架构设计

## 设计目标

cget 的架构目标是把命令解析、任务生命周期、下载执行、协议适配、持久化和文件系统操作拆开。CLI 不直接处理下载细节，下载引擎不直接解析命令，协议层不修改任务最终状态。

## 当前分层

- CLI 层：将 argv 解析为 `Command`，负责用户输入校验和帮助文本。
- Core 层：定义任务、Chunk、错误码、状态机、日志和 `DownloadManager`。
- Engine 层：执行单个任务的下载生命周期，包括 metadata、Range 分片、重试、合并和 SHA256 校验。
- Network 层：通过 `ProtocolHandler` 屏蔽 libcurl 细节，当前由 libcurl adapter 处理可用协议。
- Persistence 层：读写任务 JSON，扫描任务目录，按 part 文件修正进度。
- Filesystem 层：解析输出路径、创建目录、合并 chunk、保护 temp path。
- Config 层：加载默认值、配置文件、环境变量和 CLI 覆盖值。
- Scheduler 层：控制前台队列执行中的全局 worker、并发任务数和每任务 Chunk 配额。
- RateLimiter 层：用 Token Bucket 统一处理全局限速和任务级限速。
- Metrics 层：从任务快照和调度快照计算运行时统计，并写入 `metrics.json` 供 `cget stats` 读取。

## 边界原则

`DownloadManager` 是任务生命周期入口；`DownloadEngine` 只负责执行下载；`ProtocolHandler` 只负责网络传输；`PersistenceStore` 不能决定任务是否继续下载；Worker 线程不能绕过核心服务随意设置任务终态。

## 当前限制

cget 当前仍是前台运行模型，没有后台 daemon，也没有跨进程实时控制运行中的任务。`cget stats` 通过读取运行时快照工作，不是远程控制 API。
