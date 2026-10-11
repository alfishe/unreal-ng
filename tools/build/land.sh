#!/usr/bin/env bash
# Land a worktree's HEAD on local master: a fast-forward guarded by compare-and-swap, then the main checkout follows
# when it is on master. Run from the worktree whose HEAD is to land (master merged into it first if master moved).
#
# Usage: tools/build/land.sh <full SHA master is expected at>
#        tools/build/land.sh "$(git rev-parse master)"
#
# Refuses (exit 1, nothing changed) when master is no longer at the expected SHA (someone landed meanwhile: merge
# master again and retry), HEAD does not contain it (not a fast-forward), or the main checkout is on master with its
# index.lock held or staged changes.
# The main checkout follows through a private index: the new master's index is built in a file of its own and swapped
# in under index.lock; files are written only on landed paths the main checkout has no edits on. A file another session
# is editing stays byte for byte as it is: its edits become uncommitted changes on top of the new master (listed).
# Not synced (master still moves, the script says so): the main checkout is on another branch.
# Pushing is a separate step: tools/build/README.md, "Landing on master".
set -euo pipefail

if [ $# -ne 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

OLD=$(git rev-parse -q --verify "$1^{commit}" || true)
[ -n "$OLD" ] && [ "$OLD" = "$1" ] || { echo "give the full SHA of master, not '$1' (git rev-parse master)"; exit 2; }
NEW=$(git rev-parse HEAD)
# The main checkout: the worktree that owns the common git directory
MAIN=$(cd "$(git rev-parse --git-common-dir)/.." && pwd)
GD=$(git -C "$MAIN" rev-parse --absolute-git-dir)

[ "$(git rev-parse master)" = "$OLD" ] || { echo "master moved: $(git rev-parse master)"; exit 1; }
[ "$NEW" != "$OLD" ] || { echo "nothing to land: HEAD is master"; exit 0; }
git merge-base --is-ancestor "$OLD" "$NEW" || { echo "not a fast-forward: merge master into HEAD first"; exit 1; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/land.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

edits() { (git -C "$MAIN" diff --name-only; git -C "$MAIN" ls-files --others --exclude-standard) | sort -u; }
onMaster() { [ "$(git -C "$MAIN" symbolic-ref -q HEAD)" = "refs/heads/master" ]; }
# Read-only (write-tree would take index.lock)
indexIsOld() { git -C "$MAIN" diff-index --cached --quiet "$OLD" --; }

if onMaster && [ -e "$GD/index.lock" ]; then
    echo "$GD/index.lock is held: a git command is running in the main checkout (or died and left it); retry"
    exit 1
fi
# Moved under staged changes, the main checkout would show the whole landing reverted in its index
if onMaster && ! indexIsOld; then
    echo "the main checkout is on master with staged changes: commit or unstage them there, then retry"
    exit 1
fi

SYNC=0
if onMaster && indexIsOld; then
    SYNC=1
    edits > "$TMP/edits"
    git diff --name-only "$OLD" "$NEW" | sort -u > "$TMP/paths"
    comm -12 "$TMP/paths" "$TMP/edits" > "$TMP/kept"
    # The private index: the new master, built without touching the main checkout
    GIT_INDEX_FILE="$TMP/index" git read-tree "$NEW"
fi

# Compare-and-swap: fails if master moved since the check above
git update-ref refs/heads/master "$NEW" "$OLD"
echo "master: $OLD -> $NEW"

# Checked again: another session may have switched the branch, staged or edited meanwhile
if [ $SYNC = 1 ] && onMaster && indexIsOld && edits | cmp -s - "$TMP/edits"; then
    # index.lock is git's own lock: a git command in the main checkout waits or fails instead of racing the swap
    if ! (set -o noclobber; : > "$GD/index.lock") 2>/dev/null; then
        echo "main checkout NOT synced: $GD/index.lock is held (a git command is running there); master has moved"
        exit 1
    fi
    trap 'rm -f "$GD/index.lock"; rm -rf "$TMP"' EXIT
    comm -23 "$TMP/paths" "$TMP/kept" > "$TMP/sync"
    git diff --name-only --diff-filter=D "$OLD" "$NEW" | sort -u > "$TMP/deleted"
    comm -23 "$TMP/sync" "$TMP/deleted" > "$TMP/write"
    comm -12 "$TMP/sync" "$TMP/deleted" > "$TMP/remove"
    while read -r f; do
        rm -f -- "$MAIN/$f"
        rmdir -p "$(dirname "$MAIN/$f")" 2>/dev/null || true
    done < "$TMP/remove"
    if [ -s "$TMP/write" ]; then
        (cd "$MAIN" && tr '\n' '\0' < "$TMP/write" | GIT_INDEX_FILE="$TMP/index" xargs -0 git checkout-index -f --)
    fi
    (cd "$MAIN" && GIT_INDEX_FILE="$TMP/index" git update-index -q --refresh) || true
    cp "$TMP/index" "$GD/index.lock"
    mv "$GD/index.lock" "$GD/index"
    trap 'rm -rf "$TMP"' EXIT
    echo "main checkout synced: $(wc -l < "$TMP/write" | tr -d ' ') written, $(wc -l < "$TMP/remove" | tr -d ' ') removed"
    if [ -s "$TMP/kept" ]; then
        echo "kept as they are (edits in the main checkout, now uncommitted on top of the new master):"
        sed 's/^/  /' "$TMP/kept"
    fi
else
    echo "main checkout NOT synced (not on master, or it changed during the landing)"
fi
