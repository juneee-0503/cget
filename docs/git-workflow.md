# cget Git 工作流

## 目标

cget 使用简化 Git Flow 和语义化版本 Tag，让代码演进可追踪、可回滚、可协作、可发布。

## 分支模型

```text
main        稳定发布分支
develop     日常集成分支
feature/*   新功能开发
fix/*       普通 bug 修复
hotfix/*    已发布版本紧急修复
release/*   发布候选分支
docs/*      文档修改
refactor/*  内部重构
test/*      测试补充
```

`main` 必须保持可构建、可测试、可发布。日常开发先进 `develop`，发布时从 `develop` 创建 `release/vX.Y.Z`，最终合并到 `main` 并创建 tag。

## 常规开发流程

```bash
git checkout develop
git pull origin develop
git checkout -b feature/scheduler-fairness

# work, build, test

git add .
git commit -m "feat(scheduler): add per-task chunk quota"
git push -u origin feature/scheduler-fairness
```

功能分支通过 PR 合并到 `develop`。推荐对 `feature/*`、`fix/*`、`docs/*`、`test/*` 使用 Squash Merge。

## Release 流程

```bash
git checkout develop
git pull origin develop
git checkout -b release/v1.2.0
git push -u origin release/v1.2.0
```

release 分支只允许发布准备工作：修复发布前 bug、更新版本号、更新 CHANGELOG、README、文档和 release notes。

发布前检查：

```text
CMakeLists.txt 版本号已更新
include/cget/version.hpp 版本号已更新
CHANGELOG.md 已更新
README.md 和 docs/ 已更新
Debug/Release 构建通过
核心测试通过
ASAN/TSAN 按需通过
release notes 已准备
```

合并和打 tag：

```bash
git checkout main
git pull origin main
git merge --no-ff release/v1.2.0
git tag -a v1.2.0 -m "cget v1.2.0"
git push origin main
git push origin v1.2.0
```

同步回 `develop`：

```bash
git checkout develop
git pull origin develop
git merge --no-ff release/v1.2.0
git push origin develop
```

## Hotfix 流程

hotfix 从 `main` 或已发布 tag 创建。

```bash
git checkout main
git pull origin main
git checkout -b hotfix/v1.2.1-http416-crash

# fix, build, test

git commit -m "fix(network): prevent crash when handling HTTP 416"
git checkout main
git merge --no-ff hotfix/v1.2.1-http416-crash
git tag -a v1.2.1 -m "cget v1.2.1 hotfix"
git push origin main
git push origin v1.2.1
```

hotfix 必须同步回 `develop`，避免后续版本重新引入同一问题。

## Tag 规范

cget 使用 annotated tag：

```bash
git tag -a v1.2.0 -m "cget v1.2.0"
git push origin v1.2.0
```

版本格式：

```text
vMAJOR.MINOR.PATCH
v1.2.0-rc.1
v1.2.0-beta.1
v1.2.0-alpha.1
```

正式 tag 一旦推送，不覆盖、不删除。发布有问题时创建补丁版本，例如 `v1.2.1`。

## Commit 规范

使用 Conventional Commits：

```text
<type>(<scope>): <subject>
```

常用 type：

```text
feat      新功能
fix       bug 修复
docs      文档修改
test      测试相关
refactor  不改变外部行为的重构
perf      性能优化
chore     杂项
ci        CI/CD
style     格式调整
build     构建系统
release   发布相关
```

推荐 scope：

```text
cli core task scheduler engine network filesystem persistence
metrics ratelimit config logging cmake docs test ci release
```

示例：

```text
feat(metrics): add system stats snapshot
fix(filesystem): reject path traversal output
docs(recovery): explain corrupted JSON repair flow
release: prepare v1.2.0
```

破坏性变更必须标注：

```text
refactor(config)!: change config file schema

BREAKING CHANGE: old flat config keys are no longer accepted.
```

## GitHub 保护建议

`main`：

- 禁止直接 push。
- 必须通过 PR。
- 必须通过 CI。
- 禁止 force push。
- 禁止删除分支。

`develop`：

- 建议通过 PR。
- 必须通过基础构建。
- 禁止 force push。

Tag：

- 保护 `v*`。
- 禁止覆盖或删除已发布 tag。

## 常用命令

创建 `develop`：

```bash
git checkout main
git checkout -b develop
git push -u origin develop
```

查看版本：

```bash
git tag
git show v1.1.0
```

基于旧版本修复：

```bash
git checkout -b hotfix/v1.1.1 v1.1.0
```
