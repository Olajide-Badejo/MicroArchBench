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
- `include/numa.hpp`: removed an accidental duplicated function block that
  redefined `numa_available_safe`, `numa_alloc_local_safe`, `numa_free_safe`,
  and `pin_thread_to_core`, fixing compile-time redefinition errors.
- `CMakeLists.txt`: bumped Google Benchmark from `v1.8.4` to `v1.9.0` to
  match usage of `benchmark::Counter::kIs1000000000`.

