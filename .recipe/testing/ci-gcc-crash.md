# Recipe: A crash only CI's gcc job sees

Goal: from a red `build-gcc-latest` job (CMake CI, container `gcc:16`) to the crashing frame and a checked fix,
when the test passes on the Mac, under ASan on the Mac and in the `build-linux` job. No MCP / WebAPI call is involved.

The case it was written on (2026-11-16): `TTDControl_BranchCreate_Test.InfoNamesChildrenAndAncestors` segfaulted in
shard 0 of `build-gcc-latest` only. It was a use-after-free (a pointer into a `TTDSessionInfo` copy that had been
replaced): clang left the freed memory intact, gcc 16 reused it. 2400 Mac runs of the test and the Mac shard with the
long tier all passed; the `gcc:16` container crashed on the first run of the test alone.

## 1. Which test crashed

The job's log has the shards' output interleaved, and a crashed shard prints no summary. Take the log without the
colour codes and list the tests that started and never finished:

```bash
gh run view <run-id>                                  # the failed job's ID
gh api --allow-escape-sequences repos/{owner}/{repo}/actions/jobs/<job-id>/logs \
    | sed 's/\x1b\[[0-9;]*m//g' | cut -c30- > scratch/ci-gcc.log   # cut: the timestamps
grep -E "Segmentation fault|Shard [0-9] FAILED|^\[  FAILED  \] [A-Z]" scratch/ci-gcc.log | sort -u
python3 - <<'EOF'
import re
run, done = set(), set()
for line in open('scratch/ci-gcc.log', errors='replace'):
    if m := re.search(r'\[ RUN      \] (\S+)', line): run.add(m[1])
    if m := re.search(r'\[\s+(OK|FAILED|SKIPPED)\s+\] (\S+) \(', line): done.add(m[2])
print(sorted(run - done))      # the test each crashed shard was in
EOF
```

`gh run view --log-failed` and `gh run view --job <id> --log` may come back cut short for a long step; the
`actions/jobs/<id>/logs` call returns the whole log.

Check the earlier runs of the job before deciding it is flaky: the same test in the same shard twice is a
deterministic crash, not a race.

## 2. The job's conditions

`.github/workflows/cmake-ci.yml`, job `build-gcc-latest`: container `gcc:16`, Release, `-DBUILD_QT_APPS=OFF
-DTESTS=ON`, target `core-tests`, then `test-parallel` with `UNREAL_LONG_TESTS=1` (the long tier,
`core/tests/_helpers/testtier.h`) and one shard per CPU of the runner (4: `scripts/run-tests-parallel.sh`).

On the Mac, the same shard with the long tier is a cheap first check (it found nothing in the case above):

```bash
cd cmake-build-agent-release/bin
UNREAL_LONG_TESTS=1 GTEST_TOTAL_SHARDS=4 GTEST_SHARD_INDEX=0 ./core-tests --gtest_brief=1
```

A filter changes which tests a shard gets: run the shard unfiltered, or the test alone (step 3).

## 3. Reproduce in `gcc:16`

Any Docker host does; Apple Silicon runs the image natively (arm64) and reproduced the x86_64 CI crash. Work in
your own worktree on that host (`scratch/wt-<task>`, with submodules), bind-mounted at `/src`:

```bash
git worktree add scratch/wt-<task> -b <task> github/master
cd scratch/wt-<task> && git submodule update --init --recursive
cat > scratch/gcc16.sh <<'EOF'
#!/bin/bash
# The build-gcc-latest steps: the CI's packages, configure, core-tests
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends cmake ninja-build uuid-dev libssl-dev zlib1g-dev \
  libudev-dev pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libegl-dev libdecor-0-dev gdb >/dev/null
git config --global --add safe.directory /src
cd /src
cmake -S . -B build-gcc16 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_APPS=OFF -DTESTS=ON > scratch/gcc16-configure.log
cmake --build build-gcc16 --target core-tests --parallel "${JOBS:-8}" > scratch/gcc16-build.log 2>&1 || exit 1
EOF
nohup docker run --rm --name <task>-gcc16 --cpus 8 -v "$PWD":/src docker.io/library/gcc:16 \
    bash /src/scratch/gcc16.sh > scratch/gcc16-run.log 2>&1 &
```

`--cpus` cannot exceed the Docker VM's CPU count (Docker Desktop refuses the run). A host whose Docker Hub pulls
fail can take the same image from `public.ecr.aws/docker/library/gcc:16`. The build takes about as long as the CI
step (the full `core-tests`).

Then the test alone, the shard, and the backtrace:

```bash
run() { docker run --rm -v "$PWD":/src -w /src/build-gcc16/bin docker.io/library/gcc:16 bash -c "$1"; }
run './core-tests --gtest_filter="Suite.Name"; echo exit $?'                        # 139: crashes alone
run 'UNREAL_LONG_TESTS=1 GTEST_TOTAL_SHARDS=4 GTEST_SHARD_INDEX=0 ./core-tests > /src/scratch/shard0.log 2>&1; echo $?'
run 'apt-get update -qq >/dev/null; apt-get install -y -qq gdb >/dev/null 2>&1;
     gdb -batch -ex run -ex bt --args ./core-tests --gtest_filter="Suite.Name" 2>&1 | grep -A30 SIGSEGV'
```

The Release build carries enough debug information for function names and source lines in the backtrace.

Failures that come from the bind mount, not the code (a macOS folder seen from Linux): `Gif_Test`'s non-UTF-8 file
name, `SessionJournal_Test.InsertReplaysDiscardsOrIgnores`, `TTDRecordingFolders_Test.FolderLifecycle`,
`TTDCdDrive_Test.FolderDiscReplaysIdenticallyAndAnotherDiscIsReported`. Compare with the CI log before chasing them.

## 4. Check the fix where it crashed

Apply the change to the worktree on the Docker host (`git diff > fix.patch`, copy it over, `git apply`), rebuild
incrementally in the same container image and run the test's suites:

```bash
run 'apt-get update -qq >/dev/null; apt-get install -y -qq cmake ninja-build uuid-dev libssl-dev zlib1g-dev \
       libudev-dev pkg-config >/dev/null 2>&1;
     cmake --build /src/build-gcc16 --target core-tests --parallel 8 && ./core-tests --gtest_filter="Suite.*"'
```

Then land as usual (AGENTS.md) and watch the next `build-gcc-latest` run: its concurrency group queues it behind the
job of the previous push, so it can start an hour after the push. Remove the worktree, its branch and the
`build-gcc16` folder on the Docker host when done.

## Why the Mac missed it

- A use-after-free reads freed memory; whether that memory still holds the old object depends on the allocator. The
  macOS allocator left it intact, glibc with gcc 16's code layout handed it out again.
- An ASan stress run of the TTD suites on the Mac did report errors, but its logs were overwritten by the next run
  before anyone read them: give every sanitizer run its own log folder. The container was faster anyway: when a
  gcc-only crash does not show in a few Mac runs, go to the container rather than more Mac runs.
- Sanitizers in the same container: `docker/linux/build.sh --asan` (CI image, gcc 14) - see
  [docs/testing/sanitizers.md](../../docs/testing/sanitizers.md).
