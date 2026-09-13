# Unreal-NG Coding Guidelines

## File Naming
- All source and header files should be strictly lowercase.
- Do NOT use underscores in filenames (e.g., use `testpathhelper.h` instead of `test_path_helper.h`).

## Paths in Documentation and Scripts
- Reference repository files **relative to the repository root only** — never with machine-specific absolute paths (documentation, Python, and code comments alike).
- Python scripts must compute the root at runtime (`REPO_ROOT = Path(__file__).resolve().parents[N]`) instead of hardcoding it — this works from any working directory.
- In GUI code use platform APIs (e.g., `QStandardPaths::writableLocation()`) instead of hardcoded user directories.
- To detect and repair leaks, run `python3 tools/fix-absolute-paths.py` (dry run over tracked markdown by default; add `--ext py --apply` to widen and write). Supports `--root` to also strip historical mount points of this checkout.

## C++ Conventions
- **Methods/Functions**: Use `PascalCase` (e.g., `GetExecutableDir()`).
- **Variables/Fields**: Use `camelCase` (e.g., `framebufferDigest`, `tStates`).
- **Private Member Variables**: Prefix with an underscore (e.g., `_emulator`, `_context`).

## Testing Conventions
- **Test Classes**: Use the pattern `ClassName_Test` (e.g., `WD1793_Test`).
- **Test Setup**: Use the `CUT` (Class Under Test) pattern for exposing internal state in tests without polluting the public API.
- **File System/Paths**: Use `TestPathHelper::GetTestDataPath()` for fixtures and `TestPathHelper::GetTestScratchPath()` for outputs. Ensure all test artifacts go to `scratch/`.

## Performance & Memory Management
- **Large POD Buffer Allocation**: Avoid `std::vector<T>::resize(n, 0)` for multi-megabyte buffers of trivial/POD types. Under unoptimized or Debug builds (`-O0`), standard library implementations (`libc++`, `libstdc++`) execute scalar per-element copy-construct loops (costing ~50–60 ms per 24 MiB array). Prefer value-initialized array allocation (`new T[n]()` / `std::make_unique<T[]>` or specialized zero-init buffers like `ZeroInitBuffer`) which standard C++ guarantees lowers to kernel zero-fill/`calloc`/`memset` across all optimization levels.
- **Bulk Memory Zeroing**: Use vectorized `std::memset` for large POD arrays instead of `std::fill`, which can remain a scalar loop under unoptimized compilation.
- **Deallocation**: Avoid `std::vector<T>().swap(v)` idioms that create and discard temporary vector objects; prefer explicit ownership primitives (`std::unique_ptr<T[]>::reset()`).
- **Lazy Buffer Allocation**: Profiling and diagnostic subsystems (e.g., memory tracking, trace buffers) must allocate counters lazily upon feature activation and release them promptly when deactivated.

## General Architecture
- **GUI Decoupling**: Keep `core/` completely decoupled from `unreal-qt/`. The core must remain headless and platform-agnostic.
- **Cross-Platform Compatibility**: Use `<filesystem>` for path manipulation to handle `/` and `\` transparently across macOS, Linux, and Windows.
