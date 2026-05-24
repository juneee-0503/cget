# cget

A modern, high-performance C++ command-line downloader.

## Project Overview

cget is a command-line download manager written in modern C++20. It is designed as more than a simple one-shot file fetcher: the project treats downloading as an engineering system with task state, persistence, retry behavior, recovery, protocol boundaries, filesystem safety, and testable modules.

The project solves a common problem in large-file workflows: downloads are often interrupted by unstable networks, process exits, server errors, or local filesystem issues. Instead of losing all progress, cget stores task metadata on disk and can resume from persisted task state and partial chunk files where possible.

cget is suitable for developer tooling, dataset downloads, SDK or binary artifact downloads, CI-style dependency fetching, and learning how to structure a production-oriented CLI system in C++. It currently focuses on foreground CLI execution rather than a background daemon, which keeps the runtime model explicit and easy to debug.

The codebase is intentionally modular. CLI parsing, task management, download execution, protocol handling, persistence, filesystem operations, configuration, logging, and tests are separated so future contributors can extend one area without turning the application into a single large controller.

cget is intended to be maintained as a long-term open-source project. APIs, command syntax, and internal architecture may continue to evolve before a stable release, but the current foundation is already structured for incremental protocol, scheduling, and reliability improvements.

Chinese engineering documentation is available under [`docs/`](docs/README.md). The docs describe the current architecture, configuration model, recovery behavior, testing strategy, and planned roadmap.

## Features

- **Command-line interface**: Provides implemented commands for adding tasks, queueing tasks, running queued downloads, resuming, listing, showing status, editing config, viewing protocols, and recovering persisted state.
- **HTTP / HTTPS download foundation**: Uses libcurl for HTTP and HTTPS transfers, including metadata requests, redirects, TLS handling through libcurl, and streaming writes.
- **Resume support**: Stores task metadata and chunk progress so interrupted tasks can be recovered and resumed.
- **Multi-threaded chunk download design**: Supports chunk planning and concurrent range downloads when a server and protocol support byte ranges.
- **Task scheduling**: Supports queued foreground execution through a scheduler module with FIFO ordering, global worker limits, concurrent task limits, and per-task chunk quotas.
- **Persistent task state**: Writes task JSON files under the cget home directory and keeps backup files for recovery from corrupted writes.
- **Retry mechanism**: Retries recoverable chunk failures with configurable retry count and base delay.
- **Network interruption recovery**: Repairs in-memory task progress from actual partial files during recovery, so stale JSON progress is corrected conservatively.
- **Remote metadata checks**: Stores ETag, Last-Modified, and final URL metadata where available, and blocks unsafe resume attempts when remote metadata changes.
- **SHA256 verification**: Supports task-level `--sha256` validation after the final file is assembled.
- **Rate limiting**: Supports legacy global byte-per-second limiting plus v1.2 global and per-task Token Bucket limits such as `--limit 5MB`.
- **Runtime metrics**: Writes a lightweight `metrics.json` snapshot for `cget stats`, including task counts, worker utilization, scheduler queue size, and aggregate speeds.
- **Proxy support**: Supports optional libcurl proxy configuration for HTTP, HTTPS, and SOCKS-style proxy URLs.
- **Configurable logging**: Supports log levels, file logging, optional console logging, and simple log rotation.
- **Modular architecture**: Keeps CLI, core task logic, engine, network, persistence, filesystem, config, logging, and tests in separate modules.
- **Cross-platform build target**: Uses CMake, C++20, `std::filesystem`, threads, and libcurl to target macOS, Linux, and Windows-compatible development paths.
- **Future protocol extension support**: Includes a protocol registry and libcurl-backed adapter boundary for HTTP, HTTPS, FTP, FTPS, and conditionally available SFTP/SCP support depending on the local libcurl build.

## Architecture

cget uses a layered architecture with narrow responsibilities:

- **CLI layer**: Parses argv into structured commands. It does not open network connections, write task JSON directly, or manage download threads.
- **Core layer**: Owns task models, task snapshots, chunk planning, state transitions, logging, and the download manager facade.
- **Download engine**: Drives the lifecycle of a single download task: metadata lookup, chunk planning, retry handling, progress persistence, merge, checksum verification, and final status update.
- **Protocol layer**: Hides transfer details behind `ProtocolHandler`. The `ProtocolRegistry` chooses a handler based on URL scheme and libcurl runtime capabilities.
- **Persistence layer**: Serializes task state to JSON, loads task files, repairs progress from partial files, and removes task metadata when requested.
- **Filesystem layer**: Owns cget home paths, task temp directories, output path resolution, safe chunk paths, file truncation, and chunk merging.
- **Config layer**: Reads and writes `config.json`, including thread limits, active task limits, retry policy, rate limits, and proxy settings.
- **Scheduler layer**: Coordinates queued foreground work with FIFO scheduling, worker limits, task concurrency, and per-task chunk quotas.
- **Rate-limit layer**: Applies global and task-level Token Bucket throttling without holding manager-wide locks while waiting.
- **Metrics layer**: Samples task and scheduler snapshots and writes a runtime metrics file that another CLI process can inspect with `cget stats`.
- **Test layer**: Provides unit tests and optional stress tests using fake protocol handlers so core behavior can be verified without external network dependencies.

The design keeps product entrypoints separated from download mechanics. CLI commands call application services; the download manager coordinates persisted tasks; the engine executes task lifecycle; protocol handlers shield the engine from HTTP/FTP/SFTP differences; persistence makes disk state authoritative during recovery; queue execution controls foreground concurrency.

## Project Structure

```text
cget/
├── .github/
│   └── workflows/
│       └── ci.yml
├── include/
│   └── cget/
│       ├── cli/
│       ├── config/
│       ├── core/
│       ├── crypto/
│       ├── engine/
│       ├── filesystem/
│       ├── metrics/
│       ├── network/
│       ├── persistence/
│       ├── ratelimit/
│       └── scheduler/
├── docs/
│   ├── README.md
│   ├── architecture.md
│   ├── cli.md
│   ├── configuration.md
│   ├── contributing.md
│   ├── filesystem.md
│   ├── git-workflow.md
│   ├── network.md
│   ├── persistence.md
│   ├── recovery.md
│   ├── release.md
│   ├── roadmap.md
│   ├── scheduler.md
│   ├── task-model.md
│   ├── testing.md
│   └── troubleshooting.md
├── src/
│   ├── cli/
│   ├── config/
│   ├── core/
│   ├── crypto/
│   ├── engine/
│   ├── filesystem/
│   ├── metrics/
│   ├── network/
│   ├── persistence/
│   ├── ratelimit/
│   └── scheduler/
├── tests/
│   ├── stress/
│   └── unit/
├── CMakeLists.txt
├── main.cpp
└── README.md
```

Generated build directories such as `build/`, `build-asan/`, `build-tsan/`, `build-stress/`, `cmake-build-debug/`, and IDE folders are intentionally ignored by Git.

## Build

### Requirements

- C++20 compiler
- CMake 3.20 or newer
- libcurl development package
- POSIX-like shell for the examples below

On macOS, libcurl is commonly available through the system SDK. On Linux, install the development package, for example:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev
```

### Build Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The main binary is generated at:

```text
build/cget
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Optional engineering builds:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCGET_WARNINGS_AS_ERRORS=ON
cmake --build build-release

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCGET_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCGET_ENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure

cmake -S . -B build-stress -DCMAKE_BUILD_TYPE=Debug -DCGET_BUILD_STRESS_TESTS=ON
cmake --build build-stress
ctest --test-dir build-stress --output-on-failure
```

### Common Build Issues

- **`cmake: command not found`**: Install CMake or use an IDE-provided CMake binary.
- **`Could NOT find CURL`**: Install libcurl development headers and libraries.
- **Sanitizer link errors**: Use a compiler/runtime that supports the selected sanitizer.
- **Old build cache errors**: Remove the build directory and configure again.

## Usage

Set an isolated runtime directory during testing:

```bash
export CGET_HOME=/tmp/cget-home
```

### Already Implemented

```bash
cget help
cget version
cget protocols

cget add https://example.com/file.zip
cget add https://example.com/file.zip -o ./file.zip --threads 4
cget add https://example.com/file.zip --limit 5MB
cget add https://example.com/file.zip --sha256 <64-character-hex-digest>
cget add https://example.com/file.zip --queue

cget run
cget list
cget stats
cget status <task-id>
cget pause <task-id>
cget resume <task-id>
cget remove <task-id>
cget recover

cget config get
cget config get download.max_threads
cget config set download.max_threads 8
cget config set download.max_active_tasks 2
cget config set network.max_retries 3
cget config set network.retry_base_delay_ms 1000
cget config set download.max_download_rate_bytes_per_sec 0
cget config set scheduler.max_global_workers 16
cget config set scheduler.max_chunks_per_task 4
cget config set rate_limit.global 20MB
cget config set rate_limit.default_per_task unlimited
cget config set network.proxy none
cget config set network.proxy http://127.0.0.1:8080
cget config set logging.level debug
```

Legacy config keys such as `max_threads`, `max_active_tasks`, `max_retries`, `retry_base_delay_ms`, `max_download_rate_bytes_per_sec`, and `proxy` remain supported.

### Planned Commands / Capabilities

- `repair <task-id>` for conservative task repair.
- `verify <task-id>` for standalone checksum verification.
- `list --json` and `status --json` for script-friendly output.
- Background daemon mode for pausing an actively running task from another process.
- Remote control API.
- Package manager distribution.
- More protocol-specific authentication options.

## Tech Stack

- **C++20**: Main implementation language.
- **CMake**: Build system and test registration.
- **C++ Standard Library**: Uses filesystem, threading, atomics, chrono, streams, containers, and synchronization primitives.
- **libcurl**: Used for network transfers and runtime protocol capability detection.
- **Internal JSON utility**: Minimal project-local JSON parser/writer for task and config persistence.
- **Internal SHA256 implementation**: Used for optional file verification.
- **Internal scheduler, metrics, and rate-limit modules**: Implement foreground fairness, runtime observability, and Token Bucket throttling without new third-party dependencies.
- **Planned / Optional**: Boost.Asio or standalone Asio for future advanced networking, OpenSSL for future explicit crypto/TLS features, package-manager-specific integration, and richer release workflows.

## Development

Clone and build:

```bash
git clone https://github.com/juneee_0503/cget.git
cd cget
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run locally:

```bash
./build/cget help
./build/cget protocols
```

Add a new module by creating matching headers under `include/cget/<module>/` and implementations under `src/<module>/`, then register the `.cpp` file in `CMakeLists.txt`.

Extend the protocol layer by adding a new `ProtocolHandler` implementation or extending the registry. Keep protocol-specific details out of the download manager and download engine unless the engine needs a generic capability flag.

Version numbers are tracked in `CMakeLists.txt`, `include/cget/version.hpp`, and `CHANGELOG.md`. Keep them aligned when preparing a release.

Before submitting code:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

For concurrency or memory-sensitive changes, also run sanitizer and stress builds.

## Contributing

1. Fork the project.
2. Create a feature branch.
3. Commit focused changes with clear messages.
4. Open a Pull Request with test notes and behavior changes.

The full Git workflow is documented in [`docs/git-workflow.md`](docs/git-workflow.md), and contributor basics are in [`CONTRIBUTING.md`](CONTRIBUTING.md).

Suggested branch names:

- `feature/download-engine`
- `fix/resume-offset`
- `docs/readme-update`
- `refactor/task-scheduler`

## License

No `LICENSE` file is currently included. MIT License is a reasonable default for this project when the repository is ready for public distribution.

## Project Status

> cget is currently in the v1.2 scheduling and observability stage. APIs, command syntax, and internal architecture may change before the first stable release.
