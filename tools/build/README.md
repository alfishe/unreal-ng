# tools/build - build and test gate for agents

Several agents build on one machine. Left alone they all compile at once and the machine
crawls. These scripts put a **machine-wide limit** on it:

| Pool    | Slots | What runs in it                             |
|---------|-------|---------------------------------------------|
| `build` | 2     | `ninja` builds (compile + link)             |
| `test`  | 1     | `core-tests` / `test-parallel` runs         |

The limit is shared by every agent, checkout and worktree of the same user on the
machine. A caller that finds both slots busy **waits in a queue** and prints a line saying
so; it does not fail.

## What agents run

```bash
tools/build/build.sh                 # the pre-commit full build (all targets)
tools/build/build.sh core-tests      # one target while iterating
tools/build/test.sh                  # builds core-tests, then runs test-parallel
tools/build/test.sh --gtest_filter='*Foo*'   # builds core-tests, runs only those tests
tools/build/slot.sh --status         # who holds which slot right now
```

`build.sh` / `test.sh` already apply the rules from `AGENTS.md`: half the logical cores
for `-j`, lowered priority (`nice 10`), build directory `cmake-build-agent-release`
(override with `BUILD_DIR`, cores with `UNREAL_JOBS`; sanitizer builds: `tools/sanitize/configure.sh asan|tsan`, then
`BUILD_DIR=$PWD/cmake-build-asan tools/build/build.sh core-tests` - [`docs/testing/sanitizers.md`](../../docs/testing/sanitizers.md)).

`build.sh` also **re-runs the CMake configure step before every build**, and configures a
missing build directory from scratch (`-G Ninja -DTESTS=ON`). Sources are globbed, so a file
that arrived with a merge or rebase is invisible to ninja until CMake runs again - without
this the link fails with undefined symbols for classes that "obviously exist". The
configure output goes to `<build dir>/configure.log` and is shown only if it fails. Set
`UNREAL_NO_CONFIGURE=1` to skip it. Why this matters and what the failure looks like without it:
`AGENTS.md`, section "Why `build.sh` configures every time". Benchmarks (`-DBENCHMARKS=ON`) still need one manual
configure, which later runs keep.

**Release builds link with LTO** ([`cmake/Lto.cmake`](../../cmake/Lto.cmake), `ENABLE_LTO`): the emulator's
per-instruction paths cross translation units, and with LTO a frame of the heavy machines costs 4-11 % less. The
price is the link: the final link of `core-tests` or `unreal-qt` takes noticeably longer. For a quick edit-build
loop configure once with `-DENABLE_LTO=OFF` (the next `build.sh` runs keep it). It is off by default on MinGW (the
link takes over 40 minutes there) and in sanitizer builds. gcc's cross-unit warnings (`-Wodr`) only appear with it:
a class or an enum that differs between two files is reported at link time.

`build.sh` also runs the **source checks of the CI `lint` job** first
(`tools/verification/gui-instance-reads.sh`, a fraction of a second). A failure is printed as
`== lint FAILED` above the build output and does not stop the build: the CI `lint` job fails on it alone, without
holding up the Linux build and tests, but an agent sees it before the commit rather than an hour later. A count below
its baseline prints a reminder to lower the baseline in the same change. `UNREAL_NO_LINT=1` skips the checks.

### How an agent should call them

Run the command and **wait for it to finish** - the slot is held exactly as long as the
command runs. A full build or test run can queue behind others and run for many minutes,
which is longer than a foreground tool call may last. So start it as a **background
command** (Claude Code: `run_in_background`), wait for the completion notice, then read its
output. Do not poll in a tight loop, and do not start a second copy to "check".

If the call is cancelled or times out, nothing is left behind: the build is terminated
and the slot is released within a few seconds (see below).

## Any other command

```bash
tools/build/slot.sh [-t SECONDS] [-q] POOL -- COMMAND [ARGS...]
```

- `POOL` is `build` (2 slots), `test` (1 slot), or any other name (1 slot). Resize with
  `UNREAL_SLOTS_<POOL>=N`, e.g. `UNREAL_SLOTS_BUILD=3`.
- `-t SECONDS` gives up waiting for a slot after that long (exit status 75).
- `-q` silences the "waiting" messages.
- The exit status is the command's; `128+N` if it died from signal `N`.
- Benchmarks and A/B timing runs: use `UNREAL_NICE=0` (or no slot at all) - lowered
  priority skews the numbers, and run them on a quiet machine.

## Landing on master

Work happens in a worktree (`scratch/wt-<name>`), never on a branch switched in the shared main checkout. When the
build is green, `land.sh` moves local master to the worktree's HEAD:

```bash
# in the worktree; merge github/master first (NOT local master: it may hold other sessions' unpushed commits)
git fetch github master && git merge --no-edit github/master
BUILD_DIR=$PWD/cmake-build-agent-release tools/build/build.sh \
  && tools/build/land.sh --push "$(git rev-parse master)"
```

- **Only a fast-forward, and only if master is still where you saw it.** The argument is the full SHA master is
  expected at; `git update-ref` swaps it compare-and-swap, so a landing that raced another session fails with
  `master moved: <sha>` and changes nothing. Merge master again and retry.
- **The main checkout follows, through a private index.** Other sessions work in it, often with uncommitted edits on
  the very files a landing brings. The new master's index is built in a file of its own and swapped in under
  `index.lock` (git's own lock), and files are written only on landed paths with no edits there. A file someone is
  editing stays byte for byte as it is; its edits show as uncommitted changes on top of the new master, and the script
  lists those files. Nothing is merged into someone's working file, so no conflict markers appear in it.
- **When it refuses** (nothing moves): the main checkout is on master and its `index.lock` is held (a git command is
  running there) or its index has staged changes (moving master under them would show the landing reverted). On
  another branch the main checkout is not touched and master still moves (the script says so).
- **Chain the gate with `&&`.** With `;` a failed build still lands and pushes.
- **`--push` publishes exactly the landed SHA** (`git push github <sha>:refs/heads/master`, never the `master` ref).
  Before master moves it checks `git log github/master..master`: when local master holds commits github lacks
  (another session's unpushed work), a push would publish them, so it refuses and changes nothing.
- **Publishing without landing: `land.sh --publish-only`.** For a branch built on `github/master`: pushes HEAD, leaves
  local master and the main checkout alone. Refuses when HEAD carries commits local master also holds and github lacks
  (a merge of local master by mistake), and when github/master has commits HEAD lacks.
- `origin` is pushed by the owner; the landing ends with the github push.

## How it works, and why it does not get stuck

`slot.py` (Python 3, standard library only; `slot.sh` just finds the interpreter) takes
an exclusive `flock(2)` on a file `~/.cache/unreal-ng/slots/<pool>/<N>.lock`
(`UNREAL_SLOTS_DIR` overrides the directory).

- **The kernel owns the lock.** When the process holding it dies for any reason - normal
  exit, crash, `kill -9`, closed terminal - the lock is released by the operating system.
  There is no pid list, no "is the owner still alive" guessing and no time-out rule, so
  a slot can neither leak nor be taken from a live owner.
- **No orphaned builds.** The command runs under a small watchdog that shares the lock
  with the wrapper. If the wrapper is killed, the watchdog terminates the command's
  process group (TERM, then KILL after 2 s) and only then exits, which frees the slot -
  within about 1-3 s. A normal finish skips all of that.
- **Ctrl-C / SIGTERM / SIGHUP** to the wrapper are forwarded to the command, which is
  waited for; the exit status is `128+signal`.
- The command does not inherit the lock, so a background process it leaves behind does
  not keep the slot.

Works on macOS and Linux (POSIX `flock`, `fork`, process groups). On Windows there is no
limiter: the command is simply run directly.

## Tests

```bash
tools/build/selftest.sh
```

About two minutes, safe next to real builds (private slots directory and pool names).
It covers: exit-status passthrough, the concurrency limits under a storm of short jobs,
SIGTERM / SIGINT / SIGKILL of the wrapper (also with a command that ignores SIGTERM),
a queued waiter that is killed, `-t` time-outs, a leftover lock file from a dead run, a
live owner that must never be taken over, and a chaos run with random SIGKILLs. Run it
after any change to `slot.py`.
