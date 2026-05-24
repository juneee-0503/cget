# cget 文件系统层

## 当前职责

`FileSystemService` 负责：

- 解析默认 home 目录和 `CGET_HOME`。
- 创建 `tasks`、`temp`、`downloads`、`logs`。
- 生成任务 temp 目录和 chunk 路径。
- 校验目录穿越。
- 解析输出路径并避免默认覆盖。
- 合并 Chunk 到最终文件。

## 安全策略

chunk temp path 必须是相对路径，不能包含 `..`。输出路径默认不覆盖已有文件，除非命令显式使用 `--force`。

## 当前限制

磁盘空间不足目前会表现为文件系统错误并使任务失败。后续会把该场景接入更细的恢复策略。
