# cget 文档中心

cget 是一个 C++20 编写的命令行下载工具。当前版本重点是 HTTP/HTTPS 下载、Range 分片、断点续传、任务持久化、前台队列执行、调度公平性、运行时指标、限速、配置、日志和恢复能力。

本文档目录用于记录工程设计和维护约定。README 面向开源入口，`docs/` 面向开发者和长期维护。

## 当前已实现

- 前台 CLI：`add`、`pause`、`resume`、`remove`、`run`、`list`、`stats`、`status`、`recover`、`config`、`protocols`、`help`、`version`。
- 下载基础：libcurl 驱动 HTTP/HTTPS，支持 Range 时可并发分片下载，不支持 Range 时退化为单流下载。
- 任务状态：任务 JSON 持久化、chunk part 文件、崩溃后 `recover`、SHA256 校验、代理配置、全局限速和任务级限速。
- 工程加固：分组配置模型、日志级别、日志轮转、错误码辅助函数、远程元数据保存与恢复检查。
- 调度与观测：Scheduler、每任务 Chunk 配额、运行时 `metrics.json` 快照和 `cget stats`。

## 后续计划

- `repair <task-id>`：保守修复损坏任务。
- `verify <task-id>`：对已完成文件执行单独校验。
- `list --json`、`status --json`：脚本友好的 JSON 输出。
- 后台 daemon、远程控制 API、插件协议系统和 priority 命令。

## 文档索引

- [架构设计](architecture.md)
- [路线图](roadmap.md)
- [CLI 使用](cli.md)
- [配置系统](configuration.md)
- [任务模型](task-model.md)
- [调度系统](scheduler.md)
- [持久化系统](persistence.md)
- [恢复系统](recovery.md)
- [网络层](network.md)
- [文件系统层](filesystem.md)
- [测试策略](testing.md)
- [贡献指南](contributing.md)
- [Git 工作流](git-workflow.md)
- [发布流程](release.md)
- [故障排查](troubleshooting.md)
