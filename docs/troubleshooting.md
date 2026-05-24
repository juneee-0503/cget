# cget 故障排查

## 下载失败

先查看任务状态：

```bash
cget status <task-id>
```

再查看日志：

```text
~/.cget/logs/cget.log
```

## 任务无法恢复

执行：

```bash
cget recover
```

如果任务变为 `Corrupted` 或 `PendingRecovery`，当前版本不会自动删除临时文件。保留 temp 文件是为了避免丢失用户已经下载的数据。

## MetadataMismatch

该状态表示继续下载前发现远程文件元数据变化，例如 ETag 或 Last-Modified 不一致。当前版本不会自动覆盖本地数据。

## 目标文件已存在

默认不覆盖已有文件，会生成不冲突的文件名。需要强制覆盖时使用：

```bash
cget add <url> -o <path> --force
```

## 构建找不到 libcurl

安装 libcurl development package。Linux 示例：

```bash
sudo apt-get install -y libcurl4-openssl-dev
```
