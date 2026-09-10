# Cross-Platform & Compiler Compatibility

Unreal-NG is a heavily cross-platform project that must compile cleanly and function identically across all supported operating systems and compiler toolchains. 

## Supported Platforms
We officially support three primary platforms:
1. **Windows** (x86_64, ARM64)
2. **macOS** (Apple Silicon / ARM64, Intel / x86_64)
3. **Linux** (x86_64, ARM64)

## Supported Compilers
The codebase is validated against multiple modern C++ compilers to ensure standard compliance. You must ensure your code compiles on all of them:
- **GCC** (Linux/MinGW)
- **Clang** (macOS/Linux)
- **MSVC** (Windows)
- **MinGW** (Windows cross-compilation)

## Zero Warnings Policy
We enforce a strict **zero-warnings** policy. All builds must compile without triggering any compiler warnings across all supported toolchains. If a warning appears:
1. Fix the underlying issue rather than suppressing it.
2. If it is a false positive from a 3rd-party library, isolate the include and suppress the warning only around that specific include block.

## Development Guidelines
- **Path Handling**: Never hardcode `\` or `/` for file paths. Always rely on `std::filesystem::path` and its overloaded `/` operator to naturally build paths.
- **Platform-Specific Code**: Try to avoid platform-specific `#ifdef` macros (e.g., `#ifdef _WIN32` or `#ifdef __APPLE__`) unless absolutely necessary (like OS-specific system calls). Abstract platform-specific logic behind common interfaces.
- **Data Types**: Use explicit fixed-width integer types (`uint8_t`, `uint16_t`, `uint32_t`, etc.) from `<cstdint>` instead of ambiguous types like `int` or `long` to guarantee size consistency across 32-bit and 64-bit platforms.
- **Standard Library**: Stick to the C++ standard library. Avoid POSIX-only or Windows-only APIs.

By maintaining strict compatibility across these diverse environments, we ensure Unreal-NG remains universally accessible and robust.
