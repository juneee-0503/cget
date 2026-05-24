# cget 发布流程

## 当前建议流程

1. 确认 `main` 分支干净。
2. 更新 `CMakeLists.txt`、`include/cget/version.hpp`、`CHANGELOG.md` 和文档。
3. 运行 Debug build、Release build、unit tests、ASAN；调度相关改动还应运行 TSAN 和 stress。
4. 检查 README 和 `docs/` 中的已实现/计划功能描述。
5. 创建 tag，例如 `v1.2.0`。
6. 推送 tag，触发 `.github/workflows/release.yml` 构建发布 artifact。
7. 根据 tag 和 artifact 创建 GitHub Release。

## 本地验证示例

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/cget version
```

## 后续计划

GitHub Release 正文自动生成、checksums 和 changelog 自动生成还未实现。
