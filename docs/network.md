# cget 网络层

## 当前已实现

网络层通过 `ProtocolHandler` 抽象暴露：

- `fetchMetadata`
- `downloadRange`
- `downloadSingle`

当前实现由 libcurl 驱动。HTTP/HTTPS 是主要目标；FTP/FTPS/SFTP/SCP 是否可用取决于本机 libcurl build，可通过 `cget protocols` 查看。

## 错误分类

网络层抛出 `CgetError`，但不直接设置任务终态。任务终态由 `DownloadEngine` 和 `DownloadManager` 统一决定。

## 后续计划

- 更完整的协议特定认证配置。
- 更稳定的协议注册接口。
- HTTP/3 和远程 API 不属于 v1.2 范围。
