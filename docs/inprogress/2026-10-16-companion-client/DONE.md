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

Possible follow-ups (not planned): companion events over the WebSocket instead of polling `me`; a GUI setting to
refuse pairing requests altogether; per-instance scopes.
