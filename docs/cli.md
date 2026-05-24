# cget CLI 使用

## 当前已实现

```bash
cget help
cget version
cget protocols

cget add <url>
cget add <url> -o <path> --threads 4
cget add <url> --sha256 <64-character-hex>
cget add <url> --queue

cget run
cget list
cget status <task-id>
cget pause <task-id>
cget resume <task-id>
cget remove <task-id>
cget recover

cget config get
cget config get download.max_threads
cget config set download.max_threads 8
cget config set network.proxy http://127.0.0.1:8080
cget config set logging.level debug
```

旧配置键仍可使用，例如 `max_threads`、`max_active_tasks`、`max_retries`、`retry_base_delay_ms`、`proxy`。

## 运行模型

`add` 默认创建任务并前台下载；`add --queue` 只入队；`run` 执行队列中的任务。`pause` 修改持久化任务状态，但当前版本不支持从另一个进程实时暂停正在运行的下载。

## 后续计划

- `repair <task-id>`
- `verify <task-id>`
- `list --json`
- `status <task-id> --json`
- 后台 daemon 与远程控制 API
