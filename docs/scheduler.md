# cget 调度系统

## 当前已实现

cget 当前支持前台队列执行：

- `add --queue` 创建排队任务。
- `run` 扫描 queued/pending/retrying/failed 任务。
- `Scheduler` 模块按 FIFO 默认策略调度 Range Chunk。
- `scheduler.max_global_workers` 控制全局 Chunk worker 数。
- `scheduler.max_concurrent_tasks` 控制同一进程内的并发任务数。
- `scheduler.max_chunks_per_task` 限制单个任务同时运行的 Chunk 数，避免大任务长期独占 worker。
- `scheduler.policy` 当前支持 `fifo` 和 `small_task_first`。

## 当前限制

Scheduler 只控制当前前台进程内的 `run` 执行，不是跨进程 daemon。单个 `add` 前台下载仍以直接执行为主，但复用同一限速与持久化路径。

## 后续计划

- priority 命令。
- 更完整的 backpressure。
- 更完整的暂停/取消队列语义。
- 后台 daemon 场景下的跨进程调度。
