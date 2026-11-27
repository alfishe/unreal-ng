#!/usr/bin/env bash
# Keep the host's master cache current: the worktree scratch/wt-ccache-master of the main checkout follows github/master
# and is built (all + core-tests); its own ccache, scratch/wt-ccache-master/scratch/ccache, is the master cache every
# other worktree of the host reads (cmake/Ccache.cmake). Run it after a landing, from any checkout of the host; when
# master has not moved since its last build it returns at once.
#
#   tools/build/ccache-master.sh            # fetch github/master, build it if it moved
#   tools/build/ccache-master.sh --status   # where it is, cache size
#
# The worktree belongs to this script, not to a task: it is never removed and holds no edits (a checkout is forced).
# Its cache is 40 GiB, trimmed by ccache itself, least recently used first.
# Builds through tools/build/build.sh (the build pool, lowered priority). Details: tools/build/README.md, "Compiler cache".
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
COMMON=$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir)
MAIN=$(cd "$COMMON/.." && pwd)
W=$MAIN/scratch/wt-ccache-master
SIZE=40G
STAMP=$W/scratch/ccache-master.built

if [ "${1:-}" = --status ]; then
    if [ -d "$W" ]; then
        echo "worktree: $W at $(git -C "$W" rev-parse --short HEAD), built: $(cat "$STAMP" 2>/dev/null | cut -c1-9)"
        du -sh "$W/scratch/ccache" 2>/dev/null || echo "no cache yet"
    else
        echo "no master worktree: $W"
    fi
    exit 0
fi

# One run at a time on the host (two would build the same directory): the ccache-master pool, 1 slot
if [ "${1:-}" != --locked ]; then
    exec "$HERE/slot.sh" ccache-master -- "$0" --locked "$@"
fi
shift

git -C "$MAIN" fetch -q github master
TARGET=$(git -C "$MAIN" rev-parse github/master)
if [ ! -d "$W" ]; then
    git -C "$MAIN" worktree add -q --detach "$W" "$TARGET"
fi
if [ "$(cat "$STAMP" 2>/dev/null)" = "$TARGET" ] && [ -f "$W/cmake-build-agent-release/build.ninja" ]; then
    echo "master cache current at ${TARGET:0:9}"
    exit 0
fi
git -C "$W" checkout -q --force --detach "$TARGET"
git -C "$W" submodule update -q --init --recursive --force
mkdir -p "$W/scratch"
B=$W/cmake-build-agent-release
if [ ! -f "$B/CMakeCache.txt" ]; then
    cmake -S "$W" -B "$B" -G Ninja -DTESTS=ON -DUNREAL_CCACHE_SIZE="$SIZE" > "$W/scratch/ccache-master-configure.log" 2>&1 \
        || { cat "$W/scratch/ccache-master-configure.log"; exit 1; }
fi
START=$(date +%s)
BUILD_DIR=$B UNREAL_NO_LINT=1 "$W/tools/build/build.sh" all core-tests
echo "$TARGET" > "$STAMP"
echo "master cache at ${TARGET:0:9}, built in $(( $(date +%s) - START )) s"
