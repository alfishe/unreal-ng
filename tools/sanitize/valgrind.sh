#!/usr/bin/env bash
# valgrind memcheck over core-tests in the CI Linux image, the short profile: under an hour on 16 cores
# (docs/testing/sanitizers.md, "valgrind memcheck"). Finds what ASan does not: reads of uninitialized memory,
# mismatched new[] / delete, leaks of a test that never frees its machine.
#
# Usage: tools/sanitize/valgrind.sh [options]
#   --shards N      parallel valgrind processes (default: cores - 2; valgrind runs one process on one core)
#   --filter EXPR   gtest filter (default: everything); tools/sanitize/valgrind-exclude.txt is always taken out
#   --origins       --track-origins=yes (where an uninitialized value came from; about twice as slow)
#   --no-build      use the existing build in scratch/linux-<arch>-RelWithDebInfo
#   --keep-image    keep the valgrind image (removed at the end by default)
#   --out DIR       results (default: scratch/valgrind/<date-time>)
#
# The profile: RelWithDebInfo built by docker/linux/build.sh, the tests in --shards gtest shards at once,
# UNREAL_TEST_TIME_SCALE=20 (waits sized for a native run would report hangs that are not there),
# UNREAL_EXEC_FUZZ_ITERATIONS=2, suppressions tools/sanitize/valgrind.supp, a hard limit of 90 minutes per shard.
# Exit: 0 no memcheck error, 1 memcheck errors (summary.txt lists them), 2 build or setup failed, 3 a shard timed out.
# Test failures under valgrind are informational (listed, not counted): the run is about memory errors.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BASE_IMAGE="ghcr.io/alfishe/unreal-ng:qt6.9.3-ubuntu24.04"
VG_IMAGE="unreal-ng-valgrind:local"
CORES="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
SHARDS=$(( CORES > 3 ? CORES - 2 : 1 ))
FILTER="*"
ORIGINS=0
BUILD=1
KEEP_IMAGE=0
OUT=""

while [ $# -gt 0 ]; do
  case "$1" in
    --shards) SHARDS="$2"; shift 2 ;;
    --filter) FILTER="$2"; shift 2 ;;
    --origins) ORIGINS=1; shift ;;
    --no-build) BUILD=0; shift ;;
    --keep-image) KEEP_IMAGE=1; shift ;;
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

case "$(uname -m)" in
  arm64|aarch64) PLATFORM="arm64" ;;
  *) PLATFORM="amd64" ;;
esac
BUILDDIR="scratch/linux-$PLATFORM-RelWithDebInfo"
[ -n "$OUT" ] || OUT="$ROOT/scratch/valgrind/$(date +%Y-%m-%d_%H%M)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
S="$OUT/summary.txt"
note() { echo "$*" | tee -a "$S"; }

if [ "$BUILD" = 1 ]; then
  note "build: RelWithDebInfo core-tests $(date '+%F %T')"
  "$ROOT/docker/linux/build.sh" --type RelWithDebInfo core-tests > "$OUT/build.log" 2>&1 || { note "build failed: $OUT/build.log"; exit 2; }
fi
[ -x "$ROOT/$BUILDDIR/bin/core-tests" ] || { note "no $BUILDDIR/bin/core-tests"; exit 2; }

# The CI image plus valgrind, for this run only
if ! docker image inspect "$VG_IMAGE" > /dev/null 2>&1; then
  printf 'FROM %s\nRUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq valgrind && rm -rf /var/lib/apt/lists/*\n' "$BASE_IMAGE" |
    docker build -q -t "$VG_IMAGE" - > /dev/null || { note "valgrind image build failed"; exit 2; }
fi
[ "$KEEP_IMAGE" = 1 ] || trap 'docker rmi "$VG_IMAGE" > /dev/null 2>&1' EXIT

# Positive filter, then every exclusion as a negative pattern
EXCLUDE=$(grep -v '^[[:space:]]*\(#\|$\)' "$ROOT/tools/sanitize/valgrind-exclude.txt" | paste -sd: -)
GTEST_FILTER="$FILTER-$EXCLUDE"
VG="valgrind --tool=memcheck --error-exitcode=99 --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --num-callers=40 --suppressions=/src/tools/sanitize/valgrind.supp"
[ "$ORIGINS" = 1 ] && VG="$VG --track-origins=yes"

note "memcheck: $SHARDS shards, filter '$FILTER' minus $(echo "$EXCLUDE" | tr ':' '\n' | wc -l | tr -d ' ') exclusions, start $(date '+%F %T')"
docker run --rm --platform "linux/$PLATFORM" -v "$ROOT":/src -v "$OUT":/out -w "/src/$BUILDDIR/bin" \
  --tmpfs /scratch-tmp:exec,size=16g -e UNREAL_TEST_SCRATCH_DIR=/scratch-tmp \
  -e UNREAL_TEST_TIME_SCALE=20 -e UNREAL_EXEC_FUZZ_ITERATIONS=2 \
  -e SHARDS="$SHARDS" -e VG="$VG" -e GTEST_FILTER="$GTEST_FILTER" -e OWNER="$(id -u):$(id -g)" \
  "$VG_IMAGE" bash -c '
    seq 0 $((SHARDS - 1)) | xargs -P "$SHARDS" -I{} sh -c "
      GTEST_TOTAL_SHARDS=$SHARDS GTEST_SHARD_INDEX={} timeout -s KILL 90m $VG --log-file=/out/vg-{}.log \
        ./core-tests --gtest_color=no > /out/gtest-{}.log 2>&1
      echo shard {} rc=\$?"
    chown -R "$OWNER" /out
  ' > "$OUT/shards.txt" 2>&1
sort -V "$OUT/shards.txt" >> "$S"
note "end $(date '+%F %T')"

# The report: distinct memcheck errors by their first frames in our code, leaks, failed tests
python3 - "$OUT" <<'EOF' | tee -a "$S"
import collections, glob, re, sys
out = sys.argv[1]
runtime = re.compile(r"vg_replace|vgpreload|libc\.so|libstdc\+\+|ld-linux|operator new|operator delete|malloc|calloc|realloc|free$|mem(cpy|move|set)|str(len|cmp)")
kinds = collections.Counter()
errors = 0
for path in sorted(glob.glob(f"{out}/vg-*.log")):
    lines = open(path, errors="replace").read().split("\n")
    for line in lines:
        m = re.search(r"ERROR SUMMARY: (\d+) errors", line)
        if m:
            errors += int(m.group(1))
    i = 0
    while i < len(lines):
        m = re.match(r"==\d+== (Invalid .*|Conditional jump.*|Use of uninitialised.*|Syscall param.*|Mismatched .*|Source and destination overlap.*|[\d,]+ bytes in [\d,]+ blocks are definitely lost.*)", lines[i])
        if not m:
            i += 1
            continue
        kind = re.sub(r" at 0x.*|[\d,]+ bytes in [\d,]+ blocks|in loss record.*", "", m.group(1)).strip()
        frames = []
        i += 1
        while i < len(lines) and re.search(r"(at|by) 0x", lines[i]) and len(frames) < 3:
            fn = re.sub(r".*?: ", "", lines[i], count=1)
            fn = re.sub(r" \(.*", "", fn)
            if not runtime.search(fn):
                frames.append(fn[:90])
            i += 1
        kinds[(kind, " <- ".join(frames))] += 1
print(f"\nmemcheck errors (all shards): {errors}")
for (kind, where), n in kinds.most_common():
    print(f"{n:5}  {kind} | {where}")
failed = set()
for path in glob.glob(f"{out}/gtest-*.log"):
    for line in open(path, errors="replace"):
        m = re.match(r"\[  FAILED  \] ([A-Za-z].*?) \(\d+ ms\)", line)
        if m:
            failed.add(m.group(1))
print(f"\nfailed tests under valgrind (informational): {len(failed)}")
for name in sorted(failed):
    print("  " + name)
EOF

grep -q "rc=137" "$OUT/shards.txt" && { note "a shard hit the 90 minute limit"; exit 3; }
grep -q "memcheck errors (all shards): 0$" "$S" && exit 0
exit 1
