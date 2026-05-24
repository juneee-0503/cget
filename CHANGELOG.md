# Changelog

All notable changes to cget will be documented in this file.

The project follows Semantic Versioning and keeps unreleased work in the `Unreleased` section until a release branch is cut.

## [Unreleased]

### Added

- Added Git workflow documentation, contribution guidance, GitHub issue templates, and release automation scaffolding.

### Changed

- Centralized runtime version constants in `include/cget/version.hpp`.

### Fixed

- None.

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
