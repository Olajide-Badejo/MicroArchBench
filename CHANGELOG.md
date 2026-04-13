# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog and this project follows Semantic
Versioning for tagged releases.

## [Unreleased]

### Added
- Repository governance documents:
  - `CODE_OF_CONDUCT.md`
  - `SECURITY.md`
  - `CITATION.cff`
  - `docs/reproducibility.md`
- Publication metadata and contributor-facing release guidance.

### Changed
- `.gitignore` no longer ignores `Makefile` to avoid dropping a required build
  entry point from version control.
- `README.md` now links all project governance and reproducibility documents.

### Fixed
- Removed accidental brace-named artifact directory created by shell expansion
  misuse.
- `include/numa.hpp`: removed an accidentally duplicated second copy of
  `numa_available_safe`, `numa_alloc_local_safe`, `numa_free_safe`, and
  `pin_thread_to_core` that caused redefinition build errors.
- `CMakeLists.txt`: bumped Google Benchmark FetchContent pin from `v1.8.4`
  to `v1.9.0`.
- `src/peak_flops.cpp`, `src/stream.cpp`, `src/gemm_tile_predict.cpp`:
  replaced unsupported `benchmark::Counter::kIs1000000000` usage with explicit
  `/ 1.0e9` scaling and `kIsRate`.
- `src/stream.cpp`, `src/gemm_tile_predict.cpp`, `tests/test_stream_verify.cpp`:
  guarded `#pragma GCC ivdep` with `#if defined(__GNUC__) && !defined(__clang__)`
  to avoid Clang `-Wunknown-pragmas` warnings while preserving GCC hints.
- `cmake/toolchain-aarch64.cmake`: removed invalid 32-bit ARM flags
  (`-mfpu=neon-fp-armv8`, `-mfloat-abi=hard`) for AArch64 cross builds.
