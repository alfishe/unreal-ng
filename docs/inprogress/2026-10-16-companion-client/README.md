# Companion clients: remote hands of the user

**Started:** 2026-10-16 · **landed** on master 2026-10-17 (`48e0af786`) · status: done ([DONE.md](DONE.md))

Every automation surface goes through the automation gate ([execution arbiter design §6a](../2026-10-09-execution-arbiter/design.md#6a-automation-sessions-visible-remote-control-x7)).
A client that changes the machine opens a run-control session. The GUI then shows the remote control bar, locks every
control and blocks keyboard and mouse input until the session ends or the user takes control back. That is right for
an agent or a script. It is wrong for a device the user holds in their hand: a button panel, a handheld or desktop
application that resumes, resets, types or swaps a tape for the user. Such a device is the user, not someone else.

A **companion client** is a paired device that acts as the user's own hands:

- it changes the machine without opening a session, so the GUI shows no remote control bar and does not lock;
- it may do anything the user may: run control, input, media, rewind, the machine's configuration, the debugger;
- it knows at every moment what it can do now and why not (stepping needs a pause, a session holds the machine, ...);
- while an agent holds a session it is locked out like the GUI, and it may take control back like the GUI;
- the user pairs it once at the GUI (or provisions it in the environment of a headless instance), and can revoke it
  at any time.

| Document | What |
|:--|:--|
| [requirements.md](requirements.md) | goals, use cases, functional and non-functional requirements, non-goals, acceptance |
| [design.md](design.md) | the model: trust, scopes, the gate's decision table, sessions and take-back, pairing, the store, threading |
| [protocol.md](protocol.md) | the WebAPI: authentication, endpoints, bodies, errors, the client state machine |
| [surfaces.md](surfaces.md) | every automation plane (WebAPI, CLI, MCP, Lua, Python) and the GUI, with the parity table |
| [client-guide.md](client-guide.md) | how to write a companion: pairing UX, the action catalogue, handling a held session, reconnects |
| [testing.md](testing.md) | the test plan: unit, integration, Qt, live verification |
| [DONE.md](DONE.md) | what landed, phase by phase; follow-ups not planned |

## Decisions (2026-10-16)

D1-D5 were proposed in the session and accepted by the owner; D3 was widened and D6 added by the owner the same day.

| # | Decision |
|:--|:--|
| D1 | A companion never opens a run-control session: no remote control bar, no GUI lock, no input block. |
| D2 | A companion is authenticated by a token issued at pairing and approved by the user at the GUI. A headless instance gets companions from `UNREAL_COMPANION_TOKENS`. No automation surface can approve a pairing. |
| D3 | A companion may do any action, the machine's configuration and the debugger included. Every action belongs to one scope (`run`, `input`, `media`, `machine`, `rewind`, `debug`, `takeback`); all are granted by default, the user may untick some at pairing. |
| D4 | While a session is held, a companion's changing requests are refused `held_by_session`, as the GUI is locked. With the `takeback` scope it may take control back, as the GUI user can. |
| D5 | Companions can be listed and revoked from every plane. Acting as a companion is possible on the WebAPI and the CLI, the planes a remote device reaches. MCP agents and in-process scripts never act as companions. |
| D6 | Stepping without a pause makes no sense: steps, direct runs and register writes are refused `not_now` while the machine runs. The companion is told what it can do now: `GET /api/v1/companions/me` lists every action with `available` and a `reason`. |
