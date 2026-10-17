# Companion clients: status

Design: [README.md](README.md). Branch `companion-client` (worktree `scratch/wt-companion`).

| Phase | Work | Status |
|:--|:--|:--|
| C0 | Design documents | done |
| C1 | Core: `CompanionRegistry`, the store, the environment, `NC_COMPANION_CHANGED`; scope catalogue (debug included), the pause rule, `CheckAction`, the action catalogue (`companionactions`); `Origin::Companion`, `OpSource::Companion`; `by` on take-back; unit tests | done |
| C2 | WebAPI: the gate's companion branch, `/api/v1/companions*`, take-back / allow-again, OpenAPI | done |
| C3 | CLI `companion`; MCP `companion_manage`; Lua / Python `companions()`, `companion_revoke()`; tests | done |
| C4 | unreal-qt: store path, approval, pairing prompt, Tools > Companions..., toasts; Qt tests | done |
| C5 | Live verification (`tools/verification/mcp/test_companion.py`); permanent documentation (interfaces, parity table, recipes, environment variables) | done |

Left before landing: the Linux gcc check (`docker/linux/build.sh --test`) and MSVC / MinGW builds; the owner accepted D1-D6
on 2026-10-16 (a companion may do anything; steps need a pause; `me.actions` says what is possible now).

Possible follow-ups (not planned): companion events over the WebSocket instead of polling `me`; a GUI setting to
refuse pairing requests altogether; per-instance scopes.
