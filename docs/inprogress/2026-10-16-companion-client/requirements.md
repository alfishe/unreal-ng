# Companion clients: requirements

Part of [README.md](README.md).

## 1. Goals

| # | Goal |
|:--|:--|
| G1 | A device the user holds can control a running emulator the way the user does at the GUI, without the GUI treating it as remote control. |
| G2 | Agents, scripts and unknown network clients keep the visible, lockable session of the automation gate. Nothing they do changes because companions exist. |
| G3 | The user decides which devices are companions, what each may do, and can revoke any of them at once. |
| G4 | A companion knows at every moment what it can do now, and why not: held by a session (by whom), not granted, needs a pause, nothing to rewind. |
| G5 | Every automation plane can see and revoke companions; the planes a device reaches can act as one. |

## 2. Actors

| Actor | Who | Trust |
|:--|:--|:--|
| User | the person at the GUI | full |
| Companion | a paired device or program acting for the user | the user's actions, within its scopes |
| Automation client | an agent (MCP), a script (Lua, Python), an HTTP or CLI client without a companion token | the automation gate: a visible session |
| Debugger | GDB, DeZog | an explicit session taken on attach |

## 3. Use cases

| # | Use case |
|:--|:--|
| UC-1 | A button on a panel resumes or pauses the machine. The GUI stays as it is: no bar, no lock, the user can keep typing. |
| UC-2 | A button resets the machine, sends NMI, toggles turbo or sets the speed. |
| UC-3 | A handheld application sends keys, a joystick or a mouse: an on-screen keyboard, a game pad, a text macro ("LOAD \"\"" + Enter). |
| UC-4 | A button inserts the next disk of a set, starts the tape, loads a snapshot from a quick slot, saves a snapshot. |
| UC-5 | A dial scrubs time-travel history backwards and forwards. |
| UC-6 | A button switches the machine model or changes its configuration (settings, slots, network). |
| UC-6a | Debugger keys on a panel: step, step over, step out, run to, toggle a breakpoint - lit while the machine is paused, dark while it runs. |
| UC-7 | An agent drives the emulator. The companion's buttons show "held by MCP agent"; pressing one is refused. A "take back" button ends the agent's session, exactly as the GUI's "Take back control". |
| UC-8 | The user pairs a new device: the device asks, the GUI shows its name and a code, the user compares the code and allows it. |
| UC-9 | The user loses a device, opens the companion list at the GUI or calls any plane, and revokes it. |
| UC-10 | A headless instance (CI, a wall of instances, a server) starts with companions provisioned in its environment. |
| UC-11 | A companion shows a live status: instances, model, running or paused, who holds control. |

## 4. Functional requirements

### 4.1 Identity and pairing

| # | Requirement |
|:--|:--|
| FR-1 | A companion is identified by a bearer token: `Authorization: Bearer <token>` on HTTP, `companion auth <token>` on a CLI connection. |
| FR-2 | A device asks to pair with a name, an optional device description and the scopes it wants. It gets a pairing id and a six-digit code to show. |
| FR-3 | The GUI shows each request with the name, the description, the peer address and the code, and offers Allow and Deny. Allow may narrow the scopes. |
| FR-4 | The device polls its pairing. When allowed, the poll returns the token once; later polls find nothing. Denied and expired pairings say so. |
| FR-5 | A pairing request expires after 120 s. At most 4 requests are pending at once, at most one per peer address. |
| FR-6 | No automation plane can approve a pairing. Without a GUI, pairing is refused `pairing_unavailable`; companions come from `UNREAL_COMPANION_TOKENS`. |
| FR-7 | A paired companion is kept across restarts in the user's configuration (`companions.json`). Only the SHA-256 of its token is stored. |
| FR-8 | Companions from the environment live for the process only and are marked `environment`. |
| FR-9 | Any plane can list companions and pending pairings (never tokens or codes) and revoke a companion. A companion can unpair itself. |

### 4.2 Acting

| # | Requirement |
|:--|:--|
| FR-10 | A companion request that changes the machine opens no session: no `NC_RUN_CONTROL_CHANGED`, no bar, no lock, no input block. |
| FR-11 | A companion may do any action. Each needs its scope: `run`, `input`, `media`, `machine`, `rewind`, `debug` or `takeback` ([design.md §3](design.md#3-scopes-the-pause-rule-and-the-action-catalogue)); all are granted by default. An action of a scope the user did not grant is refused `companion_forbidden` with the scope needed. |
| FR-11a | Steps, direct runs, register writes and TTD instruction steps are refused `not_now` while the machine runs: pause first. |
| FR-11b | `GET /api/v1/companions/me` and the CLI `companion` list, per instance, every action of the catalogue with `available` and, when not, the `reason` (stopped, held, not granted, needs a pause, no history, no session, no cooldown). |
| FR-12 | Requests that change nothing (reads, pause, captures, mixer, snapshot save) are allowed to every companion, as to every client. |
| FR-13 | While any session is held on the instance, a changing companion request is refused `held_by_session`, naming the holder and whether this companion may take control back. |
| FR-14 | With `takeback`, a companion may take control back (`POST .../control/take-back`): the session ends, its direct run stops, the client gets `revoked_by_user` and its cooldown starts, as after the GUI's take-back. It may also lift the cooldowns (`allow-again`). |
| FR-15 | A companion never takes, holds or releases a session. A request that carries both a companion token and a session id is refused `companion_session`. |
| FR-16 | The operation log names the source `companion`; the arbiter's report names the origin `companion`. |
| FR-17 | An MCP agent cannot act as a companion: a request with `X-Unreal-Source: mcp` and a bearer token is refused `companion_not_for_agents`. Lua and Python have no way to authenticate. |
| FR-18 | A request with an unknown or revoked token is refused `companion_unknown` (HTTP 401). It is never treated as an automation client. |
| FR-19 | `GET /api/v1/companions/me` answers the companion's identity, scopes and, per instance, its state and who holds control. |

### 4.3 GUI

| # | Requirement |
|:--|:--|
| FR-20 | A pairing request opens a prompt that closes itself when the request expires or is cancelled. |
| FR-21 | A **Companions** dialog lists paired companions (name, device, scopes, source, last seen) with Revoke and Revoke all. |
| FR-22 | When a companion takes control back, the GUI says so ("Control taken back by companion 'X'") and unlocks. |
| FR-23 | When a companion is paired or revoked, the HUD shows a short toast. |

## 5. Non-functional requirements

| # | Requirement |
|:--|:--|
| NFR-1 | Authentication costs one SHA-256 of the token and a map look-up; no file access per request. |
| NFR-2 | Tokens: 32 random bytes from `std::random_device`, base64url, prefixed `unc_`. Looked up by their SHA-256: the time a look-up takes says nothing about a token. |
| NFR-3 | The store is written atomically (temporary file and rename), user-readable only on POSIX. |
| NFR-4 | Thread-safe: requests arrive on WebAPI, CLI and GUI threads at once. |
| NFR-5 | Zero warnings on clang, gcc, MSVC and MinGW. |
| NFR-6 | No change for clients without a token: the same gate, the same answers. |

## 6. Non-goals

- TLS, accounts or a user database. The WebAPI stays a LAN service; the token keeps agents and strangers from passing
  as the user, it is not internet-grade security.
- Pushing events to a companion. The existing WebSocket (`/api/v1/websocket`) carries emulator events to anyone; a
  companion polls `GET /api/v1/companions/me` for control state.
- Discovery (mDNS). A companion is configured with the host and port.
- Companion-specific actions. A companion uses the same endpoints as every client.

## 7. Acceptance

| # | Accepted when |
|:--|:--|
| A-1 | With unreal-qt running, a companion resumes, resets, types and inserts a disk; `GET .../control/session` stays `held: false` throughout and the remote control bar never shows. |
| A-2 | The same requests without a token open an implicit session, as before. |
| A-3 | A companion writes memory and sets a breakpoint at any time; a step while the machine runs is refused `not_now` and `me` shows `step` unavailable with "pause first"; after a pause the step works. |
| A-4 | A companion without `media` is refused a disk insert with `required_scope: "media"`. |
| A-5 | An agent takes control; the companion's resume is refused `held_by_session` naming the agent; the companion takes control back; the agent's next call gets `revoked_by_user`; the companion's resume then works. |
| A-6 | Pairing: request, approve at the GUI, the token is delivered once, the companion works, survives a restart, and a revoke from the CLI stops it at once. |
| A-7 | Every plane lists and revokes companions: WebAPI, CLI, MCP, Lua, Python. |
| A-8 | Unit tests, Qt tests and the live verification script pass; the build has zero warnings. |
