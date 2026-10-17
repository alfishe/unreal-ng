# Companion clients on every plane

Part of [README.md](README.md). The WebAPI in full: [protocol.md](protocol.md).

## 1. Parity

| Capability | WebAPI | CLI | MCP | Lua | Python | GUI |
|:--|:--|:--|:--|:--|:--|:--|
| act as a companion | `Authorization: Bearer` | `companion auth <token>` | never | never | never | (is the user) |
| who am I, control state | `GET /api/v1/companions/me` | `companion` / `companion status` | - | - | - | - |
| take control back | `POST …/control/take-back` | `companion take-back` | - | - | - | Take back control |
| allow re-takes | `POST …/control/allow-again` | `companion allow-again` | - | - | - | Allow again |
| request a pairing | `POST /api/v1/companions/pairings` | - | - | - | - | - |
| poll / cancel a pairing | `GET`, `DELETE …/pairings/{pid}` | - | - | - | - | - |
| approve / deny a pairing | never | never | never | never | never | the pairing prompt |
| list companions and pairings | `GET /api/v1/companions` | `companion list` | `companion_manage {action: "list"}` | `companions()` | `companions()` | Companions… |
| revoke one | `DELETE /api/v1/companions/{id}` | `companion revoke <id\|name>` | `companion_manage {action: "revoke", id}` | `companion_revoke(id)` | `companion_revoke(id)` | Revoke |
| revoke all | `DELETE /api/v1/companions` | `companion revoke all` | `companion_manage {action: "revoke_all"}` | `companion_revoke("all")` | `companion_revoke("all")` | Revoke all |
| unpair itself | `DELETE /api/v1/companions/me` | `companion revoke me` (authenticated) | - | - | - | - |

Why some cells are "never":
- **Approval** is the user's decision at the GUI (design §2).
- **MCP** serves agents; an agent acting as the user would defeat the session it should have.
- **Lua and Python** run inside the emulator as automation. A script the user wants unlocked runs through a session the
  user sees, as today.

Pairing a device happens on the WebAPI only, because that is what a device speaks; the CLI is for operators.

## 2. WebAPI

See [protocol.md](protocol.md). OpenAPI tag `Companions`. Live status: the WebSocket `/api/v1/companions/events`
([protocol §4.1](protocol.md#41-events)), served by `CompanionEventHub` (`core/automation/webapi/src/companion-event-hub.*`).

## 3. CLI

```
companion                         this connection: companion or not; the instance's control state
companion auth <token>            act as that companion on this connection
companion logout                  back to an ordinary connection
companion list                    companions and pending pairings
companion revoke <id|name|all|me> revoke (me: the companion this connection is)
companion take-back               take control back on the selected instance (needs takeback)
companion allow-again             let clients in a cooldown take control again (needs takeback)
```

```
> companion auth unc_3q2Vf0Kc…
Companion 'Desk panel' (run, input, media, machine, rewind, takeback, debug). Changes on this connection open no session.
> resume
Emulator resumed
> memory write 8000 00
(written: the usual answer, no session note)
> step
Error: not_now - the machine runs: stepping, direct runs and register writes need a pause - pause first
> companion
Companion 'Desk panel' (run, input, media, machine, rewind, takeback, debug)
Run control: free
Available now: pause, reset, nmi, turbo, speed, keyboard, joystick, mouse, tape, disk, snapshot_load, snapshot_save, rzx, model, configuration, memory, breakpoints, debug_mode
Not now:
  resume: the machine runs already
  rewind: no recorded history: start time travel first
  step: the machine runs: pause first
  ...
> resume
Error: held_by_session - mcp (MCP agent) controls the emulator; 'companion take-back' takes control back
> companion take-back
Control taken back from mcp (MCP agent)
```

- A companion connection runs its commands with the operation-log source `companion`.
- `session take` on it: `Error: companion_session - a companion acts as the user and takes no session`.
- An unknown token: `Error: companion_unknown - unknown or revoked companion token`; the connection stays ordinary.

## 4. MCP

Tool `companion_manage`:

| `action` | Arguments | Result |
|:--|:--|:--|
| `list` | - | the `GET /api/v1/companions` body, and a text table in `content[]` |
| `revoke` | `id` (id or name) | `{revoked, id}` |
| `revoke_all` | - | `{revoked: n}` |

The tool's description tells the agent that it cannot pair or approve, and that it must not revoke a companion unless
the user asks. The MCP server never sends a bearer token (protocol §1).

## 5. Lua

```lua
local c = companions()            -- { companions = { {id=, name=, device=, scopes={...}, source=, paired_ms=, last_seen_ms=}, ... },
                                  --   pairings   = { {pairing_id=, name=, device=, peer=, expires_in=, scopes={...}}, ... },
                                  --   approval_available = true }
companion_revoke("Desk panel")    -- the number revoked (0 or 1); "all" revokes every companion
```

## 6. Python

```python
c = companions()                  # dict, the same shape as the WebAPI list
companion_revoke("c-5f2a91")      # the number revoked (0 or 1); "all" revokes every companion
```

## 7. GUI

- **Pairing prompt** (non-modal): name, device, peer, code, scope check boxes, Allow / Deny; it closes when the request
  is cancelled or expires.
- **Tools → Companions…**: the list with Revoke and Revoke all; "Accept pairing requests from devices" (on by default):
  off refuses new requests (`pairing_disabled`) and denies the waiting ones. The lists on every plane show
  `pairing_allowed`.
- **Toasts:** paired, revoked, "Control taken back by companion 'X'".
- **Unchanged:** the remote control bar, the lock and the input block, which only sessions trigger.
