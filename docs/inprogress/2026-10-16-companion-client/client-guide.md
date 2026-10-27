# Writing a companion client

Part of [README.md](README.md). The protocol: [protocol.md](protocol.md).

A companion is a remote pair of hands. Make it feel like one: a button press should do exactly what the same click does
at the GUI, at once, and the device should always show whether it can act.

## 1. Settings the user enters

| Setting | Default |
|:--|:--|
| Host | (none) |
| Port | 8090 |
| Instance | the selected one (`selected: true` in `me`), or a fixed id / index |

Keep the token in the platform's secure storage when there is one. Never log it.

## 2. Pairing

1. `POST /api/v1/companions/pairings` with a name the user recognises ("Desk panel", "Phone in the living room"), a
   device description, and only the scopes the device's buttons use.
2. Show the six-digit `code` large, with "Allow it at the emulator".
3. Poll `GET /api/v1/companions/pairings/{pid}` once a second until a final state.
4. On `approved`, store the token and the approved scopes. Grey out buttons whose scope was not granted.
5. On `403 pairing_disabled`, say that the user turned pairing requests off at the emulator (Tools > Companions…).
6. On `503 pairing_unavailable`, explain that the instance has no GUI and that its owner provisions companions in
   `UNREAL_COMPANION_TOKENS`.

## 3. Buttons to endpoints

All under `/api/v1/emulator/{id}`. Bodies as in the WebAPI reference.

| Button | Request | Scope |
|:--|:--|:--|
| Pause / resume (toggle) | `POST /pause` or `POST /resume`, by `state` from `me` | - / `run` |
| Reset | `POST /reset` | `run` |
| NMI ("magic") | `POST /nmi` | `run` |
| Turbo | `POST /settings/turbo_mode {"value": true}` | `run` |
| Speed ×2 | `POST /settings/speed {"value": 2}` | `run` |
| A key | `POST /keyboard/tap {"key": "SPACE"}` | `input` |
| A held key | `POST /keyboard/press` on down, `/keyboard/release` on up | `input` |
| Type a line | `POST /keyboard/type {"text": "LOAD \"\"\n"}` | `input` |
| Joystick | `POST /joystick/press`, `/joystick/release` per direction or fire change | `input` |
| Mouse | `POST /mouse/move`, `/mouse/click` | `input` |
| Next disk | `POST /disk/A/insert {"path": "..."}` | `media` |
| Play tape | `POST /tape/play` | `media` |
| Quick load | `POST /snapshot/load {"path": "..."}` | `media` |
| Quick save | `POST /snapshot/save {"path": "..."}` | - |
| Screenshot | `GET /capture/screen` | - |
| Mute | `POST /audio/mixer/master {"muted": true}` | - |
| Rewind a frame | `POST /ttd/step-back` (a dial: one request per detent) | `rewind` |
| Jump in history | `POST /ttd/seek {"frame": 1200}` | `rewind` |
| Model | `POST /model {"model": "PENTAGON"}` | `machine` |
| A setting (TR-DOS traps, sound, ...) | `POST /settings/{name} {"value": ...}`, `POST /feature/{name}` | `machine` |
| Step / step over / step out | `POST /step`, `/stepover`, `/stepout` (paused only) | `debug` |
| Run to the cursor | `POST /skip_until {"pc": ...}` (paused only) | `debug` |
| Toggle a breakpoint | `POST /breakpoints`, `DELETE /breakpoints/{bp_id}` | `debug` |
| Debug mode | `PUT /debugmode {"enabled": true}` | `debug` |
| Take back | `POST /control/take-back` | `takeback` |

Paths are on the emulator's host. A companion that offers files lists them from a folder the user configures, or uses
the media endpoints that accept an upload (multipart), as every client may.

## 4. Showing the state

Open the events channel (`/api/v1/companions/events`, [protocol §4.1](protocol.md#41-events)): `hello` is the whole
view, then each `emulator` message replaces that instance's entry. Where a WebSocket is not possible, poll
`GET /api/v1/companions/me` (2 Hz is enough). Either way drive every button from `emulators[i].actions`: lit when
`available`, dark otherwise, with the `reason` as its tooltip or second line. The step keys go dark while the machine
runs and light up at a pause or a breakpoint stop; resume and pause swap; rewind lights once there is history; take-back
lights only while a session holds the machine.

| `me` says | Show |
|:--|:--|
| `actions.<a>.available` | the button lit or dark, `reason` on it |
| `control.held: true` | a label "MCP agent controls the emulator"; the take-back button if `can_take_back` |
| 401 | "unpaired": offer to pair again |
| no answer, or the channel dropped | "offline": keep the token, reconnect with back-off (1, 2, 4 … 30 s) |
| `revoked` on the channel | "unpaired", as a 401 |

Do not hammer actions while held: the server refuses them, `retry: false`.

## 5. Behaviour rules

- **One press, one request.** No automatic retries of actions: a reset sent twice resets twice.
- **Do not take control back automatically.** Take-back ends someone's work; it is a button the user presses.
- **Respect scopes.** A 403 `companion_forbidden` means the user did not grant the scope; say which.
- **Respect the state.** A 409 `not_now` means the button should have been dark (`actions` said so).
- **Several instances.** `me.emulators` lists them all; let the user pick, or follow `selected`.
- **Clock-free.** Every time in the protocol is a server timestamp in ms; show ages, not clock times, when the device's
  clock may differ.
