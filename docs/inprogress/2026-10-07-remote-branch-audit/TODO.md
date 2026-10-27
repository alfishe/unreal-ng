# Remote branch audit (GitHub and origin)

| | |
|---|---|
| **Created** | 2026-10-07 |
| **PLAN row** | [#100](../PLAN.md) |
| **Decision** | Owner, 2026-10-07: review first, **delete nothing yet** |

Branches on both remotes, checked against master `d59959065` (2026-10-07). "Merged" = the branch
tip is an ancestor of master: deleting it loses no commit. "+N" = commits on the branch that are
not on master. The origin list is from the local tracking refs; origin (`172.16.21.25`) was
unreachable on 2026-10-07, so re-fetch before acting.

## GitHub (`github`, github.com/alfishe/unreal-ng)

| Branch | Tip | Last commit | State | Check |
|---|---|---|---|---|
| `claude/great-dijkstra-3wy88q` | `6b61226c3` | 2026-10-06 | merged | - [ ] still used by a cloud session? |
| `claude/happy-ritchie-370vaz` | `192be7bdd` | 2026-10-06 | merged | - [ ] still used by a cloud session? |
| `media-multisource` | `c09447767` | 2026-10-07 | merged | - [ ] work continues on it (#95)? |
| `claude/funny-brahmagupta-1gh91b` | `a2314f223` | 2026-10-06 | +1 | - [ ] land or drop the commit |
| `automation-batch-mode` | `9717e1f82` | 2026-08-09 | +17 | - [ ] differs from origin's (`7e6cab770`, +19) |
| `crt-effects` | `7c1c38e66` | 2026-09-12 | +6 | - [ ] |
| `disasm-table` | `428566b6c` | 2026-09-21 | +59 | - [ ] |
| `frame-diagnostics` | `815c79453` | 2026-09-17 | +5 | - [ ] |
| `ios-client` | `4c07d5176` | 2026-09-21 | +16 | - [ ] |
| `master-z80lib` | `315983e14` | 2026-09-25 | +6 | - [ ] differs from origin's (`20efff9c7`, +2) |
| `new-gui` | `5c64c5f73` | 2026-09-14 | +33 | - [ ] |
| `nvenc-zero-copy` | `cb9f266a0` | 2026-10-05 | +4 | - [ ] |
| `recording-quicksync` | `9d909f01b` | 2026-08-19 | +6 | - [ ] |
| `uns-snapshots` | `e12204a51` | 2026-09-18 | +28 | - [ ] |
| `visualizations` | `233ffdaa7` | 2026-09-21 | +36 | - [ ] |

## origin (`origin`, 172.16.21.25/emulators/unreal)

| Branch | Tip | Last commit | State | Check |
|---|---|---|---|---|
| `ttd-engine` | `9b2d2a555` | 2026-10-05 | merged | - [ ] |
| `automation-batch-mode` | `7e6cab770` | 2026-08-11 | +19 | - [ ] which copy is current (GitHub has `9717e1f82`) |
| `crt-effects` | `7c1c38e66` | 2026-09-12 | +6 | - [ ] same tip as GitHub |
| `disasm-table` | `428566b6c` | 2026-09-21 | +59 | - [ ] same tip as GitHub |
| `frame-diagnostics` | `815c79453` | 2026-09-17 | +5 | - [ ] same tip as GitHub |
| `ios-client` | `4c07d5176` | 2026-09-21 | +16 | - [ ] same tip as GitHub |
| `master-z80lib` | `20efff9c7` | 2026-09-24 | +2 | - [ ] which copy is current (GitHub has `315983e14`) |
| `new-gui` | `5c64c5f73` | 2026-09-14 | +33 | - [ ] same tip as GitHub |
| `nvenc-zero-copy` | `cb9f266a0` | 2026-10-05 | +4 | - [ ] same tip as GitHub |
| `recording-quicksync` | `9d909f01b` | 2026-08-19 | +6 | - [ ] same tip as GitHub |
| `uns-snapshots` | `e12204a51` | 2026-09-18 | +28 | - [ ] same tip as GitHub |
| `visualizations` | `233ffdaa7` | 2026-09-21 | +36 | - [ ] same tip as GitHub |

## How to recheck

```bash
git fetch github --prune && git fetch origin --prune
for r in github origin; do
  git for-each-ref --format='%(refname:short) %(objectname:short)' refs/remotes/$r | grep -v '/HEAD\|/master ' |
  while read b sha; do
    git merge-base --is-ancestor $sha master && echo "$b merged" || echo "$b +$(git rev-list --count master..$sha)"
  done
done
```

Delete a merged branch only after the check above: `git push <remote> --delete <branch>`.
