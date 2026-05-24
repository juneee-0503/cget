# cget 配置系统

## 配置优先级

配置加载顺序固定为：

```text
默认值 < ~/.cget/config.json < 环境变量 < CLI 参数
```

CLI 参数目前主要指 `add --threads` 等命令级覆盖。

## 当前配置模型

```json
{
  "download": {
    "max_threads": 8,
    "max_active_tasks": 2,
    "max_download_rate_bytes_per_sec": 0
  },
  "network": {
    "max_retries": 3,
    "retry_base_delay_ms": 1000,
    "proxy": null
  },
  "persistence": {
    "flush_interval_ms": 1000
  },
  "logging": {
    "level": "info",
    "console": false,
    "max_file_bytes": 1048576,
    "max_rotated_files": 3
  }
}
```

## 兼容性

v1.1 仍能读取旧版扁平配置：`max_threads`、`max_active_tasks`、`max_retries`、`retry_base_delay_ms`、`max_download_rate_bytes_per_sec`、`proxy_url`。

## 环境变量

- `CGET_HOME`
- `CGET_MAX_THREADS`
- `CGET_MAX_ACTIVE_TASKS`
- `CGET_MAX_RETRIES`
- `CGET_RETRY_BASE_DELAY_MS`
- `CGET_MAX_DOWNLOAD_RATE_BYTES_PER_SEC`
- `CGET_PROXY`
- `CGET_LOG_LEVEL`
- `CGET_LOG_CONSOLE`
