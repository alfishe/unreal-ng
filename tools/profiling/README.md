# Profiling tools

Tools that show where the host CPU time of the emulator goes. They answer the question a benchmark cannot: not
"how long", but "in which function and on which line". Measuring "how long" and comparing two builds is the
A/B procedure of [performance-guidelines.md §4](../../docs/guidelines/performance-guidelines.md).

| Folder | Platform | What it does |
|:--|:--|:--|
| [xctrace/](xctrace/README.md) | macOS (Xcode command line tools) | records a Time Profiler trace of any command or running process (a benchmark, a test, the GUI) and reports from it: threads, functions (inclusive / self), source lines, the callees of one function |

The recipe for an agent: [.recipe/testing/host-profiling.md](../../.recipe/testing/host-profiling.md).
