# cget 任务模型

## Task

任务包含 URL、目标路径、临时目录、状态、文件大小、下载进度、Chunk 列表、错误信息、SHA256 期望值和远程元数据。

v1.1 持久化的远程元数据包括：

- `remote_etag`
- `remote_last_modified`
- `final_url`

## Chunk

Chunk 使用闭区间 `[start, end]`。每个 Chunk 写入独立 part 文件，合并时按 index 顺序写入最终文件。

## 当前任务状态

- `Created`
- `Pending`
- `Queued`
- `Downloading`
- `Paused`
- `Retrying`
- `Failed`
- `Completed`
- `Cancelled`
- `Removed`
- `Corrupted`
- `PendingRecovery`
- `MetadataMismatch`

## PendingRecovery

`PendingRecovery` 表示任务状态不足以安全自动继续。当前版本没有 `repair` 命令，用户应查看 `status` 和日志后决定是否删除任务或等待后续修复能力。
