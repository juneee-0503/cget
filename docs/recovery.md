# cget 恢复系统

## 当前已实现

`cget recover` 会扫描任务 JSON，尝试读取主文件或 `.bak`，并用 part 文件大小修正 Chunk 进度。

恢复规则：

- 主 JSON 损坏时尝试 `.bak`。
- part 文件大小小于 Chunk 大小时，Chunk 标记为 `Paused`。
- part 文件缺失时，Chunk 下载量修正为 0。
- part 文件大于 Chunk 大小时，截断到 Chunk 大小。
- 崩溃残留的 `Downloading` 或 `Retrying` 会转为可恢复状态。

## 远程元数据检查

当前版本会保存 `ETag`、`Last-Modified` 和最终 URL。继续下载前，如果发现已保存的 ETag 或 Last-Modified 与服务器返回值不一致，任务会进入 `MetadataMismatch`，不会自动覆盖本地数据。

## 后续计划

- `repair <task-id>`。
- 更详细的恢复报告。
- JSON schema 校验。
- 磁盘空间不足后的保守恢复。
