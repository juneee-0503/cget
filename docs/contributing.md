# Contributing to cget

感谢你对 cget 的兴趣。cget 是一个 C++20 CLI 下载工具，当前重点是工程可靠性、可测试性和可维护性。

## 开发原则

- CLI 层不要写下载业务逻辑。
- Network 层不要修改任务最终状态。
- Persistence 层不要决定任务是否继续下载。
- 不要在持锁时执行网络请求或长时间磁盘 IO。
- 新功能需要更新测试和文档。

## 分支命名

```text
feature/download-engine
fix/resume-offset
docs/readme-update
refactor/task-scheduler
test/recovery-cases
```

## 提交流程

1. Fork 项目。
2. 创建 feature branch。
3. 提交聚焦修改。
4. 运行构建和测试。
5. 发起 Pull Request，说明行为变化和测试结果。

## Git 工作流

完整分支、Tag、提交和发布规范见 [git-workflow.md](git-workflow.md)。
