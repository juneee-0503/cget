# Contributing to cget

感谢你对 cget 的兴趣。cget 使用简化 Git Flow、Conventional Commits 和语义化版本发布。

## Branches

- `main`: 稳定发布分支，只通过 `release/*` 或 `hotfix/*` 合并。
- `develop`: 日常集成分支。
- `feature/*`: 新功能。
- `fix/*`: 普通 bug 修复。
- `hotfix/*`: 已发布版本紧急修复。
- `release/*`: 发布候选分支。
- `docs/*`, `refactor/*`, `test/*`: 文档、重构、测试。

## Commit Messages

使用 Conventional Commits：

```text
feat(scheduler): add per-task chunk quota
fix(network): handle HTTP 416 metadata mismatch
docs(git): add workflow guide
test(persistence): cover backup recovery
```

推荐 scope：`cli`、`core`、`task`、`scheduler`、`engine`、`network`、`filesystem`、`persistence`、`metrics`、`ratelimit`、`config`、`logging`、`cmake`、`docs`、`test`、`ci`、`release`。

## Pull Requests

- 一个 PR 只解决一个明确问题。
- PR 标题使用提交规范。
- 功能、修复、重构需要说明测试结果。
- 行为变化需要同步更新 README 或 `docs/`。

## Local Checks

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

涉及并发、内存或恢复逻辑时，也运行 ASAN/TSAN 或 stress 测试。

## More

完整 Git 工作流见 [docs/git-workflow.md](docs/git-workflow.md)。
