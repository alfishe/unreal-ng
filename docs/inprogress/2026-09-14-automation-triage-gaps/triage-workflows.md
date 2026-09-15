# Triage Workflows — Before & After

Concrete walkthroughs of the scenarios named in the analysis scope, showing
what an agent must do **today** (master / atm branch, current API) versus
**after** the P0–P2 recommendations. Scenario names map to the original
request: "ATM turbo: ports, screen modes", "Profi", "ZX Evo", "MoonSound",
"kempston mouse on different configs".

Conventions: `WEBAPI` = `curl http://localhost:8090/api/v1/...`,
`MCP` = a `tools/call` via `:8092/mcp` or the stdio bridge. Emulator id
placeholder `{id}`.

---

## Scenario 1 — ATM Turbo black screen after loading a game

*The exact 2026-09-10 session: ATM-Turbo 2+ v7.10, `2048.scl` from TR-DOS,
black screen, CPU running through NOPs.*

### Today (atm branch build; on master the machine cannot be created at all)

1. `POST /emulator/create {"model":"ATM710","ram_size":512}` — on **master**
   this silently returns a 48K machine (gap A-2); the agent only notices
   hours later. On the atm branch it works, but the response does not echo
   the resolved model (A-1/A-2), so the first verification step is:
2. `MCP inspect_state aspects=["machine"]` → returns `id/state/is_running/
   is_paused/is_debug` only (A-1). The agent cannot confirm the model or RAM
   from the snapshot; it must call `/state/screen` for the `model` field.
3. `GET /state/screen` and `/state/screen/mode` → report
   `display_mode:"standard"`, `256×192`, `is_128k:false` (B-1) — actively
   wrong on ATM; the session must know to distrust them.
4. `GET /video/beam` → the *only* endpoint that says `video_mode:"ATM16"`,
   320x200 in a 448x288 frame (this is what the bug-report session used).
5. To find out what the game wrote to the mode latch: start porttrace
   (`POST /profiler/porttrace/start`), replay the load, fetch events — but
   without decode rules (C-2) the events are raw addresses; the agent must
   hand-map `#FF77` writes. `/state/paging` doesn't exist, so the current
   latch value is invisible (B-2).
6. `GET /state/screen/digest` → hashes pages 5/7 (B-4) — on ATM16 the video
   surface follows the paging window, so "did the screen change" polling is
   unreliable.
7. Conclusion requires the agent to hold ATM hardware knowledge in-context
   (FF77 mode bits, memory windows) because no `unreal://machine/atm-turbo`
   resource exists (F-1).

### After P0/P1

1. `POST /emulator/create {"model":"ATM710","ram_size":512}` →
   `{"model":"ATM710","ram_kb":512,...}` or a **400 with the reason**. On
   master: 400 + `models_creatable` from `/emulator/status` shows ATM needs
   the atm build (P0-2, P0-4).
2. `inspect_state aspects=["machine","paging","timing"]` in one call →
   model, `pFF77:"0x00"` (mode: 16c), banks table (P0-1, P1-2).
3. Porttrace with ATM decode rules → events attributed to
   `ATM mode/paging` device with rule index; `/ports` shows the static map
   (P1-4, P1-5).
4. `screen_digest mode=active` → correct surface; OCR/screenshot correlate
   with the mode reported by `/state/screen` (now truthful) (P1-1, P1-3).
5. The session consults `unreal://machine/atm-turbo` for the mode table
   instead of priors (P3-1).

---

## Scenario 2 — Kempston mouse on different configs

*"Does the mouse work on config X?" — the failure fork is: not fitted /
fitted but ports shadowed / fitted and decoded but broken.*

### Today

1. `MCP mouse_input action=status` → `available:true, present:true,
   wheel_enabled:false`, counters, port bytes (FADF/FBDF/FFDF read 255 when
   absent) — but **nothing about routing** (D-1, mouse Q4). If the guest
   program polls the mouse and sees `0xFF` forever, the agent cannot tell:
   - mouse not fitted for this config (`[INPUT] Mouse=` — invisible, A-4),
   - TR-DOS session open so the decoder gates the ports (C-3),
   - a registered peripheral claimed the address (C-3),
   - or the guest never actually reads the ports.
2. Fitting a mouse to a config that lacks it: **no API at all** — requires
   editing `data/configs/<model>/unreal.ini` on the host and restarting (A-4).
3. Cross-config comparison ("mouse on Pentagon vs Scorpion vs ATM"):
   per-config `POST /emulator/create` + status calls, joining model info
   from different endpoints each time (A-1).

### After P1-5/P2-1/P2-3

1. `inspect_state aspects=["machine","mouse"]` → one snapshot: model,
   `fitted/type/wheel` from `/capabilities` (P2-3) + `ports_decoded` and the
   gate reason from the mouse status routing field (P2-1, sourced from the
   `/ports` live block, P1-5).
2. Repro: `mouse_input action=move dx=10` → `run_frames` → the status now
   shows whether the counters moved and whether the guest could have read
   them; with porttrace rules the actual `#FBDF` reads appear attributed.
3. The three-way fork resolves in one round-trip: `fitted:false` (config) vs
   `ports_decoded:false` + `gate:"CF_TRDOS"` (session state) vs both true
   (decoder/device bug → porttrace/TTD).

---

## Scenario 3 — Profi / ZX Evo (TSConf) machine bring-up triage

*"Boot the machine, load software, decide whether a failure is machine
config, paging, or video."*

### Today

- **Profi**: creatable on master (decoder exists), but:
  `/state/memory` reports `ram_bank_3` from `p7FFD` semantics (Profi pages
  via `#DFFD` — invisible, B-2/E-1); `video_mode` endpoints report
  "standard" even if `M_PROFI` were selected (B-1); `DrawProfi` is a stub so
  pixels are black (B-3) and digest pages 5/7 miss the surface (B-4).
  Porttrace: no decode rules, session info says `Profi` (one of the 7 named)
  but without rule tables (C-2).
- **ZX Evo (TSConf / `TSL`)**: not creatable on master (decoder factory
  throws, C-1) — same silent-fallback trap as ATM on create (A-2). On a
  branch with TSConf support the same reporting gaps as ATM apply (TS16/
  TS256/TSTX modes are enum values the API never surfaces).

### After P0/P1

- Create either model → resolved model echoed or a loud 400; `/state/paging`
  shows the model's real latches (`pDFFD` for Profi; TSConf paging windows);
  `/state/screen*` report the actual `VideoModeEnum` name; digest
  `mode=active` follows the surface; decode rules + `/ports` give the port
  map; `/emulator/status` build fingerprint prevents triaging TSConf against
  a master binary.

---

## Scenario 4 — MoonSound bring-up (when it lands)

### Today (projected, if implemented per current TDD)

The integration design wires ports, mixing, TTD and UI — but no state
endpoint, no MCP aspect, no mixer visibility. An agent asked "the demo
produces no music" would have: `/state/audio/channels` (generic), audio
capture (whole-mix RMS), and nothing else. Register-level truth (is the guest
writing #C2/#C3? are timers running? is the YRW801 ROM loaded?) is
invisible — the same blindness as ATM video pre-P1. There is also no way to
check the device is even *enabled* (`[SOUND] MoonSound` is unparsed config,
A-4).

### After P2-2 (design-time) + implementation

- `/capabilities` shows `moonsound.fitted` and its volume (P2-3).
- `/ports` includes the MoonSound port entries (P1-5 rules).
- `/state/audio/moonsound/{fm,pcm}` DeviceState reports + `inspect_state`
  aspects `audio_opl4_fm`/`audio_opl4_pcm` → register/timer/key-on truth in
  one snapshot (P2-2).
- Audio capture per registry source → "FM silent, PCM active" becomes a
  one-call answer instead of a mixing deduction.

---

## Cross-cutting observation

In every scenario the dominant cost today is **discovery and
misattribution** (what machine am I on, is the peripheral fitted, is the mode
active, do the ports route), not the debugging primitives — porttrace, TTD,
digests, profilers are all present and strong. The P0/P1 items convert each
scenario's first 5–10 exploratory round-trips into 1–2 snapshot calls with
authoritative answers.
