# DONE — Companion clients (2026-10-16)

**Status:** complete — landed on master 2026-10-17 (`48e0af786`). Design: [README.md](README.md).

| Phase | Work | Status |
|:--|:--|:--|
| C0 | Design documents | done |
| C1 | Core: `CompanionRegistry`, the store, the environment, `NC_COMPANION_CHANGED`; scope catalogue (debug included), the pause rule, `CheckAction`, the action catalogue (`companionactions`); `Origin::Companion`, `OpSource::Companion`; `by` on take-back; unit tests | done |
| C2 | WebAPI: the gate's companion branch, `/api/v1/companions*`, take-back / allow-again, OpenAPI | done |
| C3 | CLI `companion`; MCP `companion_manage`; Lua / Python `companions()`, `companion_revoke()`; tests | done |
| C4 | unreal-qt: store path, approval, pairing prompt, Tools > Companions..., toasts; Qt tests | done |
| C5 | Live verification (`tools/verification/mcp/test_companion.py`); permanent documentation (interfaces, parity table, recipes, environment variables) | done |

The owner accepted D1-D6 on 2026-10-16 (a companion may do anything; steps need a pause; `me.actions` says what is
possible now). The landing also fixed the CLI server: the accept loop no longer closes sockets that their client threads
own (a reused descriptor closed under the next connection); regression in `tools/verification/mcp/test_cli_server.py`.
Not yet built on MSVC / MinGW, and the WebAPI / MCP / Qt files not yet through the gcc:16 check.

Follow-ups (owner 2026-10-17):

| # | Work | Status |
|:--|:--|:--|
| F1 | Companion events over a WebSocket (`/api/v1/companions/events`, protocol §4.1): `hello`, then changed instance entries; `CompanionEventHub`; unit and live tests | done |
| F2 | "Accept pairing requests from devices" in Tools > Companions… (QSettings): off refuses (`pairing_disabled`, 403) and denies the waiting ones; `pairing_allowed` in every list; unit and Qt tests | done |
| F3 | Per-instance scopes | not now (owner) |
