# Unreal-NG Coding Guidelines

## File Naming
- All source and header files should be strictly lowercase.
- Do NOT use underscores in filenames (e.g., use `testpathhelper.h` instead of `test_path_helper.h`).

## C++ Conventions
- **Methods/Functions**: Use `PascalCase` (e.g., `GetExecutableDir()`).
- **Variables/Fields**: Use `camelCase` (e.g., `framebufferDigest`, `tStates`).
- **Private Member Variables**: Prefix with an underscore (e.g., `_emulator`, `_context`).

## Testing Conventions
- **Test Classes**: Use the pattern `ClassName_Test` (e.g., `WD1793_Test`).
- **Test Setup**: Use the `CUT` (Class Under Test) pattern for exposing internal state in tests without polluting the public API.
- **File System/Paths**: Use `TestPathHelper::GetTestDataPath()` for fixtures and `TestPathHelper::GetTestScratchPath()` for outputs. Ensure all test artifacts go to `scratch/`.

## General Architecture
- **GUI Decoupling**: Keep `core/` completely decoupled from `unreal-qt/`. The core must remain headless and platform-agnostic.
- **Cross-Platform Compatibility**: Use `<filesystem>` for path manipulation to handle `/` and `\` transparently across macOS, Linux, and Windows.
