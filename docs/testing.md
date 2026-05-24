# cget 测试策略

## 当前测试

- 单元测试：CLI parser、Chunk planner、状态机、配置、错误模型、日志、持久化、恢复、SHA256、协议注册、下载引擎、Scheduler、Metrics、RateLimiter。
- Stress 测试：多任务并发下载和 SHA256 校验，可通过 `CGET_BUILD_STRESS_TESTS=ON` 开启。
- CI：Debug build、Release build、unit tests、ASAN，TSAN 作为 continue-on-error job。

## 本地命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## 后续计划

- 本地 HTTP server 集成测试。
- 故障注入测试：断网、kill 进程、JSON 写一半、part 文件丢失、磁盘空间不足。
- 更长时间的压力测试和 TSAN 覆盖。
