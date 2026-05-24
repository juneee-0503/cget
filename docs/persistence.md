# cget 持久化系统

## 当前目录结构

```text
~/.cget/
├── config.json
├── downloads/
├── logs/
│   ├── cget.log
│   └── cget.log.1
├── tasks/
│   ├── task_<id>.json
│   └── task_<id>.json.bak
├── metrics.json
└── temp/
    └── <task-id>/
        ├── chunk_0.part
        └── single.part
```

`CGET_HOME` 可以覆盖默认目录，测试时建议使用隔离目录。

## 任务 JSON

任务 JSON 保存任务状态、路径、Chunk、进度、错误信息、SHA256、远程元数据、限速和调度统计字段。v1.2 继续兼容旧任务 JSON，缺失的新字段按空值处理。

`metrics.json` 是运行时快照文件，用于 `cget stats`。它不是任务状态的唯一来源，损坏或缺失不会影响下载恢复。

## 写入策略

任务写入采用 `.tmp -> .bak -> rename` 的策略。恢复时如果主 JSON 损坏，会尝试读取 `.bak`。标准库没有跨平台 fsync 封装，当前实现不承诺完整数据库级持久性。

## 刷新策略

下载过程中按 `persistence.flush_interval_ms` 间隔刷新任务状态；暂停、失败和完成路径会强制保存。
