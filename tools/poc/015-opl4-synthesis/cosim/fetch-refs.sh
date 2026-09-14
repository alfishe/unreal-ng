#!/bin/sh
# Fetch the pinned co-simulation reference sources into refs/.
# Idempotent: re-running only verifies the pinned revisions.
set -e
cd "$(dirname "$0")"

# ymfm (Aaron Giles) — the ymf278b implementation this harness was written
# against. Any revision drift invalidates the calibrated scenario thresholds;
# re-pin deliberately and re-tune if you bump this.
YMFM_REF=81aec25ccbb98f4873a255f7551ac4dadac59b4a
YMFM_REPO=https://github.com/aaronsgiles/YMFM.git

if [ ! -d refs/ymfm/.git ]; then
    rm -rf refs/ymfm
    echo "cloning ymfm..."
    git clone --quiet "$YMFM_REPO" refs/ymfm
fi
git -C refs/ymfm fetch --quiet origin master 2>/dev/null || true
git -C refs/ymfm checkout --quiet "$YMFM_REF"
echo "ymfm @ $(git -C refs/ymfm rev-parse HEAD)"
