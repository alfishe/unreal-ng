# Token ledger: run B (and run A), Manic Miner, 2026-10-24

The P0 baseline for subagent tokens. Every later phase is measured against these lines with the same A/B setup
([plan.md](../plan.md#how-we-measure)). Machine-readable copy: [run-b-token-ledger.json](run-b-token-ledger.json).

Source: the run logs of the two runs (copied into the analysis-layers worktree as `scratch/reaper-mm/`, not in the
repo). Run B's log has one line per subagent from its completion notification (tokens, tool uses, seconds); run A's
log has per-phase totals only. Lead tokens are not counted in either run.

## Run B (zx-reaper + unreal-ng), per agent

Wall time 19:07:43-20:31 (about 1 h 23 min). Coverage and classification (19:09-19:26) were done by the lead, with no
subagent.

| Agent | Group | Model | Scope | Tokens | Tool calls | Wall time | unreal-ng used |
| --- | --- | --- | --- | ---: | ---: | ---: | --- |
| A | annotation wave 1 | sonnet | front end | 88,706 | 31 | 349 s | no (zxrun only) |
| H | annotation wave 1 | sonnet | caverns 10-19 | 120,135 | 54 | 403 s | no (zxrun only) |
| F | annotation wave 1 | sonnet | cavern setup, items, portal, sprite, ending | 136,336 | 48 | 613 s | yes (MCP + WebAPI) |
| C | annotation wave 1 | sonnet | lives, air, score, game over | 113,897 | 46 | 635 s | yes (WebAPI) |
| B | annotation wave 1 | sonnet | main loop, cheat | 117,656 | 46 | 718 s | no (zxrun + own trace script) |
| D | annotation wave 1 | sonnet | Willy | 193,508 | 83 | 808 s | no (zxrun) |
| G | annotation wave 1 | sonnet | caverns 0-9, stack area, leftover source text | 191,606 | 83 | 821 s | no (zxrun) |
| E | annotation wave 1 | sonnet | guardians, specials | 174,872 | 61 | 895 s | yes (WebAPI + MCP breakpoint) |
| W | relocation | sonnet | comment-address warnings (`#R` / `@ignoreua`) | 94,456 | 34 | 252 s | |
| X | bug hunt | sonnet | findings F77-F89 | 155,872 | 56 | 597 s | |
| G2 | gallery | sonnet | 44 images, `scripts/docs-images.sh` | 160,419 | 84 | 576 s | |
| V | verification | sonnet | 17 inferred findings to observed, 3 refuted, F90-F91 | 213,774 | 97 | 898 s | |
| C (2nd) | comment fixes | not stated | comment fixes in `src/` | 49,290 | 11 | 71 s | |
| article | article | inherited (lead's) | 14 files, 13,403 words | 224,324 | 72 | 820 s | |

The eight wave-1 agents ran in parallel from 19:30:09; the wave ended at 19:53:46 (lead cuts included).

### Totals, checked against the run report

| Item | Sum of the lines | Run report | README |
| --- | ---: | ---: | ---: |
| annotation wave 1 (8 agents) | 1,136,716 | 1,136,716 | 1,136,716 |
| later (6 agents) | 898,135 | 898,135 | |
| all subagents | **2,034,851** | 2,034,851 | 2,034,851 |
| article | 224,324 | | 224,324 |
| reverse engineering part (all minus article) | **1,810,527** | | 1.81 M |
| subagent tool calls | 806 | 806 | |

The README groups (wave 1,136,716; W 94,456; V 213,774; X 155,872; G2 160,419; C 49,290; article 224,324) all
match the log. One wording in the README's pie chart is loose: the wave annotated 99 *entries* (38 of them code), not
99 routines.

Wave-1 spread: 88,706 to 193,508 tokens and 31 to 83 tool calls per agent; mean 142,090 tokens.

## Run A (zx-reaper alone), per phase

Wall time 18:48:58-20:23 (about 1 h 34 min). The log has no per-agent lines.

| Phase | Time | Subagents | Tokens |
| --- | --- | --- | ---: |
| 3. annotation | 19:09:58-19:33:18 | 10 sonnet (A, B, C1, C2, E, F, G, H, I, J), two waves | 1,144,505 |
| 6. document | 19:57:15-20:22:43 | K bug hunt + audit (sonnet), L gallery (sonnet, 1 resume), M article (opus, 1 resume) | 879,970 |
| total, as stated | | 13 + 2 resumes | 2,023,475 |
| total, sum of the phases | | | 2,024,475 |

The run A report's total is 1,000 tokens below the sum of its own two phase figures. Without per-agent lines the run
cannot say which figure is the typo; the difference is 0.05 % and does not change any conclusion.
