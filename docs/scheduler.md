# cget 调度系统

## 当前已实现

cget 当前支持前台队列执行：

- `add --queue` 创建排队任务。
- `run` 扫描 queued/pending/retrying/failed 任务。
- `download.max_active_tasks` 控制同时运行的任务数量。

## 当前限制

当前 Scheduler 还不是独立模块，Chunk 级公平调度尚未实现。一个大任务内部仍可能按自身 Chunk 数量占用线程。

## 后续计划

- 独立 `Scheduler` 模块。
- 全局 worker 数控制。
- 每任务最大 Chunk worker 数控制。
- FIFO 默认策略。
- 未来支持 priority、backpressure 和更完整的暂停/取消队列语义。
