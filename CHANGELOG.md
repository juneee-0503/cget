# Changelog

All notable changes to cget will be documented in this file.

The project follows Semantic Versioning and keeps unreleased work in the `Unreleased` section until a release branch is cut.

## [Unreleased]

### Added

- None.

### Changed

- None.

### Fixed

- None.

## [1.2.0] - 2026-05-24

### Added

- Added Scheduler module with FIFO and `small_task_first` policies, global worker limits, concurrent task limits, and per-task chunk quotas.
- Added MetricsService and `cget stats` for lightweight runtime observability through `metrics.json`.
- Added Token Bucket rate limiter with global and per-task limits.
- Added `cget add --limit <LIMIT>` with `KB/MB/GB`, `KiB/MiB/GiB`, and `unlimited` parsing.
- Added scheduler, metrics, and rate-limit configuration keys.
- Added persisted task fields for task rate limits, scheduler quota/priority, peak speed, failed chunk count, and retry count.
- Added unit coverage for scheduler behavior, metrics snapshots, bandwidth parsing, rate limiting, config upgrades, CLI parsing, and persistence compatibility.

### Changed

- Updated CLI version output to `cget 1.2.0`.
- Updated `run` to prepare queued ranged tasks through the scheduler so large tasks cannot monopolize all chunk workers.
- Updated `list` and `status` output to show speed, ETA, chunk, scheduler, and limit information.
- Kept v1.1 and legacy flat config keys compatible while adding `scheduler.*`, `metrics.*`, and `rate_limit.*` groups.

### Fixed

- Made rate-limit config values round-trip safely when saved and reloaded.
- Made bandwidth parsing accept human-readable `/s` suffixes.

## [1.1.0] - 2026-05-24

### Added

- Added Chinese engineering documentation under `docs/`.
- Added grouped configuration for download, network, persistence, and logging settings.
- Added configurable log levels, optional console logging, and simple log rotation.
- Added unified error-code helpers for string conversion, retry classification, and suggested CLI exit codes.
- Added `PendingRecovery` task state.
- Added remote metadata persistence for ETag, Last-Modified, and final URL.
- Added metadata mismatch protection before resuming downloads.
- Added tests for config migration, environment overrides, logger behavior, error helpers, and metadata mismatch.

### Changed

- Updated CLI version output to `cget 1.1.0`.
- Kept legacy flat config keys compatible while exposing dotted keys such as `download.max_threads`.
- Updated CI to include Release, ASAN, TSAN, stress, and warnings-as-errors coverage.
- Avoided creating the runtime state directory for read-only commands such as `help`, `version`, and `protocols`.

### Fixed

- Corrected unsafe resume behavior when saved remote metadata no longer matches the server.

## [1.0.0] - 2026-05-24

### Added

- Initial C++20 CLI project structure.
- Added HTTP/HTTPS download foundation through libcurl.
- Added Range chunk planning, chunk downloads, fallback single-stream downloads, and merge validation.
- Added task JSON persistence, backup loading, recovery, list/status commands, and foreground resume.
- Added queued foreground execution, SHA256 verification, proxy configuration, rate limiting, protocol listing, and basic CI.
