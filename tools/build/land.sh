#!/usr/bin/env bash
# Land a worktree's HEAD on local master: a fast-forward guarded by compare-and-swap, then the main checkout follows
# when it is on master. Run from the worktree whose HEAD is to land (master merged into it first if master moved).
#
# Usage: tools/build/land.sh <full SHA master is expected at>
#        tools/build/land.sh "$(git rev-parse master)"
#
# Refuses (exit 1, nothing changed) when
#   - master is no longer at the expected SHA (someone landed meanwhile: merge master again and retry);
#   - HEAD does not contain the expected master (not a fast-forward);
#   - the main checkout is on master and has uncommitted edits on a path this landing changes.
# The main checkout is synced (read-tree, a fast-forward of index and files) only when it is on master and its index
# matches the old master; otherwise master moves and the main checkout is left as it is, with a message saying so.
# Pushing is a separate step: tools/build/README.md, "Landing on master".
set -euo pipefail

if [ $# -ne 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

OLD=$(git rev-parse -q --verify "$1^{commit}" || true)
[ -n "$OLD" ] && [ "$OLD" = "$1" ] || { echo "give the full SHA of master, not '$1' (git rev-parse master)"; exit 2; }
NEW=$(git rev-parse HEAD)
# The main checkout: the worktree that owns the common git directory
MAIN=$(cd "$(git rev-parse --git-common-dir)/.." && pwd)

[ "$(git rev-parse master)" = "$OLD" ] || { echo "master moved: $(git rev-parse master)"; exit 1; }
[ "$NEW" != "$OLD" ] || { echo "nothing to land: HEAD is master"; exit 0; }
git merge-base --is-ancestor "$OLD" "$NEW" || { echo "not a fast-forward: merge master into HEAD first"; exit 1; }

PATHS=$(git diff --name-only "$OLD" "$NEW")
SYNC=0
if [ "$(git -C "$MAIN" symbolic-ref -q HEAD)" = "refs/heads/master" ] &&
   [ "$(git -C "$MAIN" write-tree)" = "$(git rev-parse "$OLD^{tree}")" ]; then
    DIRTY=$( (git -C "$MAIN" diff --name-only; git -C "$MAIN" ls-files --others --exclude-standard) | sort -u)
    CLASH=$(comm -12 <(echo "$PATHS" | sort -u) <(echo "$DIRTY"))
    if [ -n "$CLASH" ]; then
        echo "main checkout has edits on landed paths:"
        echo "$CLASH"
        exit 1
    fi
    SYNC=1
fi

# Compare-and-swap: fails if master moved since the check above
git update-ref refs/heads/master "$NEW" "$OLD"
echo "master: $OLD -> $NEW"
# Checked again: another session may have switched the main checkout's branch meanwhile
if [ $SYNC = 1 ] && [ "$(git -C "$MAIN" symbolic-ref -q HEAD)" = "refs/heads/master" ]; then
    git -C "$MAIN" read-tree -m -u "$OLD" "$NEW" && echo "main checkout synced"
else
    echo "main checkout NOT synced (not on master or its index differs from the old master)"
fi
