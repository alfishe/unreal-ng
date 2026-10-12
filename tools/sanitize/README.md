# tools/sanitize

Instruments for memory, thread and lifetime bugs. The method: [`docs/testing/sanitizers.md`](../../docs/testing/sanitizers.md).

| Script | What it does |
|:--|:--|
| `configure.sh asan\|tsan` | One-time configure of `cmake-build-asan` (address + undefined) or `cmake-build-tsan` with the CMake option `-DSANITIZE=<list>` (`cmake/Sanitize.cmake`; any configure takes it); build with `BUILD_DIR=... tools/build/build.sh <target>` |
| `stress_round.sh <seconds> <tag> [build-dir]` | Builds the sanitizer `unreal-qt`, runs it headless on port 8190, drives it with `webapi_stress.py`, reports the finding / hang / survival; output in `scratch/sanitize/<tag>/` |
| `webapi_stress.py <port> <seconds> <workers>` | The WebAPI load: workers create, run, read, model-switch and delete instances; two neighbours keep running |
| `ips_stacks.py <report.ips> \| --list` | A macOS crash report as text: exception, sanitizer runtime or not, crashed thread, project threads |
| `valgrind.sh [--filter EXPR] [--origins] [--no-build]` | valgrind memcheck over `core-tests` in the CI Linux image, the short profile (`valgrind-exclude.txt`, `valgrind.supp`, `UNREAL_TEST_TIME_SCALE=20`); results in `scratch/valgrind/<date>/summary.txt` |
