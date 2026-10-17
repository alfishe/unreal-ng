# Companion clients: design

Part of [README.md](README.md). Requirements: [requirements.md](requirements.md). The automation gate and sessions
it builds on: [execution arbiter design §3.7, §6a](../2026-10-09-execution-arbiter/design.md#6a-automation-sessions-visible-remote-control-x7).

## 1. The model

The gate sorts clients into two kinds today: the user at the GUI, and everybody else, who gets a visible session. A
companion is a third kind. It is the user, reached over the network.

```
                      changes the machine?        a session is held?            result
                      ────────────────────        ──────────────────            ──────
 GUI                  -                           yes: locked, Take back only   acts directly
 companion (token)    no  → passes                -                             acts directly, no session
                      yes → scope granted? ──no─► 403 companion_forbidden
                                 │ yes
                                 ▼
                           held? ──yes──────────► 409 held_by_session (+ take_back allowed?)
                                 │ no
                                 ▼
                           a step while running? ─yes─► 409 not_now (pause first)
                                 │ no
                                 ▼
                           acts directly: no session, no bar, no lock; operation log: companion
 automation client    no  → passes
                      yes → the gate: implicit session (bar, lock), or held_by_other / revoked_by_user
```

A companion behaves like the GUI in all three respects that matter:

| | GUI | Companion | Automation client |
|:--|:--|:--|:--|
| changes the machine | directly | directly, within its scopes | through a session |
| what others see | nothing | nothing (the operation log names it) | the remote control bar, the GUI locked |
| while a session is held | locked; Take back | refused `held_by_session`; take-back with `takeback` | refused `held_by_other` |
| after a take-back | - | - | `revoked_by_user`, cooldown |

## 2. Trust

A companion's power is the user's own, so who becomes one is the user's decision, made where only the user is:

- **Pairing is approved at the GUI only.** The WebAPI, CLI, MCP, Lua and Python can request, list and revoke, never
  approve. An agent cannot make itself a companion.
- **Headless instances** have no GUI. Whoever starts the process sets `UNREAL_COMPANION_TOKENS`; that person is the
  user of a headless instance.
- **Tokens, not addresses.** The peer address is shown at pairing to help the user decide, but it identifies
  nothing: phones roam, many clients share a host. The token is the identity.
- **Agents never pass as companions.** The MCP server never sends a token, and the WebAPI refuses a token on a request
  marked `X-Unreal-Source: mcp`. Lua and Python run inside the process and have no token to give.

The token keeps agents and other LAN clients from passing as the user. It is not protection against an attacker on the
network path: the WebAPI is plain HTTP on the LAN, and a client without a token can already open a session there. The
user who wants more uses the host's firewall.

## 3. Scopes, the pause rule and the action catalogue

A companion may do anything the user may (owner, 2026-10-16): run control, input, media, the machine's configuration,
time travel and the debugger. A scope is a group of actions; every changing action belongs to exactly one. The user
grants scopes at pairing; by default all of them, and may untick some (a panel that only types needs `input`).

| Scope | Actions | WebAPI (`/api/v1/emulator/{id}` + ) | CLI |
|:--|:--|:--|:--|
| `run` | resume, reset, NMI, turbo, speed | `POST /resume`, `/reset`, `/nmi`; `/settings/{speed, turbo_mode, turbo_audio}`; `/feature/{turbomode, turbo}` | `resume`, `reset`, `nmi`, `mni`, `setting speed\|turbo_mode\|turbo_audio …`, `feature turbomode\|turbo …` |
| `input` | keyboard, joystick, mouse | `POST /keyboard/*`, `/joystick/*`, `/mouse/*` | `key`, `keyboard`, `joystick`, `mouse` |
| `media` | tape, disk, snapshot load, media slots, RZX playback, CD audio, fast / turbo loading | `/tape/{load, eject, play, pause, stop, rewind, seek}`; `/disk/{d}/{insert, eject, create}`; `/snapshot/load`; `/media/{slot}/{verb}`; `/rzx/{play, stop, seek}`; `/cdaudio/{verb}`; `/settings/{fast_tape, turbo_tape, fast_disk}`; `/feature/{fasttape, turbotape, fastdisk, ftape, ttape, fdisk}` | `tape load\|eject\|play\|pause\|stop\|rewind\|seek`, `disk insert\|eject\|create`, `snapshot load`, `media …`, `open`, `rzx play\|stop\|seek`, `setting fast_tape\|…`, `feature fasttape\|…` |
| `rewind` | time-travel scrubbing | `/ttd/{seek, step-back, step-forward, resume}` | `ttd back\|forward\|goto\|seek\|sb\|sf\|step-back\|step-forward\|resume` |
| `machine` | the model and its configuration | `/model`, `/switches`, `/slots/{slot}/{verb}`, `/slots/{slot}/options`, every other `/settings/{name}` and `/feature/{name}`, `/network/config`, `/start`, `/stop`, `/snapshot/transfer`, `/state/screen/overscan`, `/memory/rom/protect`, `/sprinter/bios`, `/bus/{bus}/sd-timing` | `model`, `switch`, `slots`, `start`, `zxpoly`, every other `setting` / `feature`, `network set`, `bus … sd-timing` |
| `debug` | the debugger: everything else | memory, registers, ports, breakpoints, steps and direct runs, TTD analysis (`/ttd/{load, dump, reverse-step, …}`, bookmarks), BASIC, the assembler and listings, chip pokes (`/next/nextreg`, `/control/isa`, `/control/audio/*`), RTC cells, disk sectors, tape render / import, debug mode | everything else |
| `takeback` | take control back from a session, allow re-takes | `POST /control/take-back`, `/control/allow-again` | `companion take-back`, `companion allow-again` |

- **Free for everyone** (the gate's no-change list): reads, `/pause`, captures, the audio mixer, audio capture, video
  recording, `/snapshot/save`, TTD start / stop, labels, symbols. No scope needed.
- **The pause rule** (owner, 2026-10-16: stepping without a pause makes no sense). Steps (`step`, `steps`, `stepover`,
  `stepout`), direct runs (`skip_until`, `run_*`), register writes, listing steps and TTD instruction steps
  (`step-instruction`, `reverse-step`, `reverse-continue`) are refused `not_now` while the machine runs: pause first.
  A breakpoint stop is a pause.
- **The catalogue is one place in code** (`automation_gate::CompanionScopeOf`, `CompanionCliScopeOf`,
  `CompanionNeedsPause`): an action no other scope names falls to `debug`. A test reads every route of
  `emulator_api.h` and lists the ones in `debug`, so a new route fails until someone decides where it belongs.

### 3.1 What is possible now

A panel should grey out what it cannot do instead of trying and being refused. `GET /api/v1/companions/me` (and the
CLI `companion`) carries, per instance, the action catalogue (`companionactions.h`): for each button-sized action
whether it is available now and, if not, why.

| Action | Scope | Available when |
|:--|:--|:--|
| `pause` | - | running |
| `resume` | `run` | paused |
| `reset`, `nmi`, `turbo`, `speed` | `run` | always |
| `keyboard`, `joystick`, `mouse` | `input` | always |
| `tape`, `disk`, `snapshot_load`, `rzx` | `media` | always |
| `snapshot_save` | - | always |
| `rewind` | `rewind` | time travel has recorded history |
| `model`, `configuration` | `machine` | always |
| `step`, `step_over`, `step_out`, `run_to`, `registers` | `debug` | paused |
| `memory`, `breakpoints`, `debug_mode` | `debug` | always |
| `take_back` | `takeback` | a session holds the machine |
| `allow_again` | `takeback` | a client waits in a re-take cooldown |

And over all of them: nothing is available on a stopped machine; nothing with a scope is available while a session
holds the machine (but `take_back`, and the free `pause`); a scope not granted makes its actions unavailable. Each
unavailable action carries its `reason` ("the machine runs: pause first", "MCP agent controls the emulator", "the user
did not grant 'media'").

## 4. Sessions and take-back

A companion is checked against the instance's run-control claim, as the GUI is:

- **No claim:** the action runs directly. No `EnterImplicitRunControl`, no `TakeRunControl`, nothing posted on
  `NC_RUN_CONTROL_CHANGED`. The arbiter sees origin `companion`; with no claim nothing refuses it.
- **A claim (any: an implicit or explicit session, GDB, DeZog):** a changing companion request is refused
  `held_by_session`. The answer names the holder (surface, client, peer, since, implicit) and says whether this
  companion may take control back. Requests that change nothing still pass, pause included.
- **Take-back:** `EmulatorContext::RevokeRunControl("companion '<name>'")`, the same call the GUI makes: the claim is
  released, its owner is marked revoked, its direct run aborts at the next instruction, its client's cooldown starts,
  and `NC_RUN_CONTROL_CHANGED` "revoked" is posted with `by` = the companion. The GUI unlocks and tells the user who
  took control back.
- **Allow again:** `AllowRunControlRetake()`, as the GUI's "Allow again".
- **A race** between the gate's check and the handler (an agent takes control in between) is answered by the
  handlers' own claim checks and the arbiter's claim refusal: the companion gets the usual 409.
- **The cooldown never applies to a companion:** it never holds a session, so nothing of it is revoked.

## 5. Components

```
 core/src/emulator/execution/
 ├── companionregistry.{h,cpp}   CompanionRegistry: companions, pairings, tokens, the store, notifications; CheckAction
 ├── companionactions.{h,cpp}    the action catalogue and what is available now (`me`, CLI `companion`)
 └── automationgate.{h,cpp}      + CompanionScope, CompanionScopeOf / CompanionCliScopeOf, CompanionNeedsPause

 core/src/emulator/
 ├── platform.h                  + NC_COMPANION_CHANGED
 ├── notifications.h             + CompanionChangedPayload; RunControlChangedPayload + `by`
 ├── emulatorcontext.cpp         RevokeRunControl posts `by`
 ├── execution/executionarbiter  + Origin::Companion ("companion")
 └── coordinator/machineop       + OpSource::Companion ("companion")

 core/automation/webapi/         the gate: companion branch; /api/v1/companions*; /control/take-back, /control/allow-again
 core/automation/cli/            `companion` command; the connection's companion identity in PassAutomationGate
 core/automation/mcp/            `companion_manage` tool (list, revoke)
 core/automation/lua, python/    companions(), companion_revoke(id)
 unreal-qt/                      store path, approval available, pairing prompt, Companions dialog, toasts
```

### 5.1 CompanionRegistry

One per process (companions are the user's, not an instance's), `CompanionRegistry::Instance()`.

```cpp
namespace companion
{
// Scopes: automation_gate::CompanionScope (automationgate.h): ScopeRun, ScopeInput, ScopeMedia, ScopeMachine,
// ScopeRewind, ScopeTakeBack, ScopeDebug; kCompanionDefaultScopes = all

struct CompanionInfo { std::string id, name, device, source /* paired | environment */; uint32_t scopes;
                       int64_t pairedMs, lastSeenMs; };
struct PairingInfo   { std::string id, name, device, peer; uint32_t scopes; int64_t createdMs; int expiresIn;
                       PairingState state; /* Pending | Approved | Denied | Expired */ };

// The decision every surface asks for a changing action: allowed, companion_forbidden, held_by_session or not_now
ActionCheck CheckAction(const CompanionInfo&, uint32_t required, bool claimed, const std::string& holder,
                        const std::string& takeBackHint, bool needsPause, bool paused);

class CompanionRegistry
{
public:
    static CompanionRegistry& Instance();

    // Pairing: a device asks, the GUI decides, the device collects its token once
    PairingRequestResult RequestPairing(name, device, scopes, peer);  // bad_request | pairing_unavailable | too_many_pairings
    PairingPoll PollPairing(pairingId);       // pending | approved (+ token, once) | denied | expired | unknown
    bool CancelPairing(pairingId);
    std::optional<CompanionInfo> ApprovePairing(pairingId, std::optional<uint32_t> scopes);   // GUI only
    bool DenyPairing(pairingId);                                                              // GUI only
    std::vector<PairingInfo> Pending() const;
    std::string CodeOf(pairingId) const;      // for the GUI prompt

    std::optional<CompanionInfo> Authenticate(const std::string& token);   // notes lastSeen
    std::optional<CompanionInfo> Find(const std::string& idOrName) const;
    size_t Revoke(const std::string& idOrName);                            // "all": every one
    size_t RevokeAll();
    std::vector<CompanionInfo> List() const;
    StateNode Report() const;                 // the listing every surface returns

    // Host wiring
    void SetApprovalAvailable(bool);          // unreal-qt: true
    void SetStorePath(const std::string&);    // loads it; "" = no persistence
    void Flush();                             // at exit: the last-seen times
    size_t LoadFromEnvironment();             // UNREAL_COMPANION_TOKENS, once at automation start
    size_t AddFromSpec(const std::string&);
    void SetClockForTesting(std::function<int64_t()>);
};
}
```

- **Locking.** One mutex; no I/O under it except the store write, which runs after the state change on a copy.
- **Authentication.** `sha256(token)` → companion, in an `unordered_map`: the look-up is by digest, so its timing says
  nothing about a token. `lastSeenMs` is updated in memory and written with the next store write (pairing, revoke) or
  at exit, never per request.
- **Expiry.** Pairings expire lazily at every registry call; the GUI prompt has its own timer.
- **Notifications.** `NC_COMPANION_CHANGED` with `CompanionChangedPayload{event, companionId, pairingId, name}`;
  events `pairing_requested`, `pairing_cancelled`, `pairing_expired`, `paired`, `denied`, `revoked`.

### 5.2 The store

`companions.json` in the application's configuration folder (unreal-qt: next to its settings file, the folder of
`QSettings(IniFormat, UserScope, "Unreal", "Unreal-NG")`):

```json
{
  "version": 1,
  "companions": [
    { "id": "c-5f2a91", "name": "Desk panel", "device": "panel 15 keys", "scopes": ["run", "input", "media"],
      "token_sha256": "9b1c…", "paired_ms": 1792160000000, "last_seen_ms": 1792163600000 }
  ]
}
```

- written atomically: `companions.json.tmp`, then rename; mode 0600 on POSIX;
- a store that fails to parse is renamed `companions.json.bad` and logged; the registry starts empty (the user pairs
  again rather than the emulator refusing to start);
- unknown fields are ignored; a store with a newer `version` is read-only (logged, never overwritten).

### 5.3 The environment

`UNREAL_COMPANION_TOKENS` = entries separated by `;`, each `name:token[:scope,scope…]`:

```
UNREAL_COMPANION_TOKENS='wall:unc_wall-secret-0123456789;panel:unc_panel-secret-0123456789:run,input'
```

- a token has at least 16 characters; shorter entries are skipped with a warning;
- no scopes = every scope; `all` = every scope;
- these companions are `source: environment`, never written to the store; a revoke holds for the process.

## 6. The gate, per plane

### 6.1 WebAPI

In the synchronous pre-routing advice, before the session branch:

1. No `Authorization: Bearer` → unchanged path.
2. `X-Unreal-Source: mcp` → 403 `companion_not_for_agents`.
3. `X-Unreal-Session` present → 400 `companion_session`.
4. `Authenticate(token)` fails → 401 `companion_unknown`.
5. The request is marked (attribute `unreal.companion` = id, name, scopes). The pre-handling advice sets the thread
   source to `OpSource::Companion`; `RequesterOf` returns origin `Companion`.
6. Not an instance path, or `ChangesMachine` false → passes. (The `/control/take-back` and `/control/allow-again`
   routes are on the no-change list; their handlers check the companion and the scope.)
7. `CheckAction(companion, CompanionScopeOf(method, subpath), claimed, …, CompanionNeedsPause(subpath), paused)`:
   a scope not granted → 403 `companion_forbidden`, `required_scope: "<name>"`; a session holds the machine → 409
   `held_by_session`; a step while running → 409 `not_now`.
8. Passes. No session, no headers added.

`/api/v1/companions*` are global routes (not under an instance): the pairing routes need no token; `me` needs one.

### 6.2 CLI

A connection becomes a companion with `companion auth <token>`. `PassCompanionGate` then follows steps 6-8 with
`CompanionCliScopeOf(command, args)` and `CompanionCliNeedsPause`. `session take` on a companion connection is refused. The connection's commands run
with the thread source `companion`. `companion logout` turns it back into an ordinary connection.

### 6.3 MCP, Lua, Python

They never act as companions (§2). They list and revoke (`companion_manage`, `companions()`, `companion_revoke()`).

## 7. GUI

- At start: `SetStorePath(<config>/companions.json)`, `SetApprovalAvailable(true)`; at exit the store is flushed.
- `NC_COMPANION_CHANGED` `pairing_requested` → a non-modal prompt: "Pair companion 'Desk panel' (panel 15 keys) from
  192.168.1.40? Code 418 205", the scopes as check boxes (defaults ticked), Allow / Deny. It closes on
  `pairing_cancelled` / `pairing_expired` and after 120 s.
- **Companions…** in the Tools menu: a table (name, device, scopes, source, last seen), Revoke, Revoke all, and
  **Accept pairing requests from devices** (on by default, kept in the settings as
  `companions/acceptPairingRequests`). Off: `SetPairingAllowed(false)` refuses new requests (`pairing_disabled`, 403)
  and denies the waiting ones (their prompts close); paired companions keep working.
- Toasts: "Companion paired", "Companion revoked", and on `NC_RUN_CONTROL_CHANGED` "revoked" with a `by` other than the
  GUI: "Control taken back by companion 'X'".
- The remote control guard is unchanged: a companion holds no claim, so the guard has nothing to show or lock.

## 8. Edge cases

| Case | Behaviour |
|:--|:--|
| Revoke while a companion request runs | the request finishes; the next one gets 401 |
| Two companions act at once | as two hands at one keyboard: both act, the arbiter orders them |
| The GUI and a companion act at once | the same |
| A companion acts on an instance that is being created or destroyed | the handler's usual answers (404) |
| A companion presses keys while a session is held | refused `held_by_session`, as the GUI blocks input |
| A companion steps while the machine runs | refused `not_now`; `me` showed `step` unavailable with "pause first" |
| A take-back with no session | 409 `nothing_held` (CLI: "nothing to take back") |
| The store's directory is not writable | logged once; pairings live for the process |
| `UNREAL_COMPANION_TOKENS` repeats a name or token | the first entry wins, the rest are logged and skipped |
| A pairing name longer than 64 characters or with control characters | trimmed to 64 printable characters |
