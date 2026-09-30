# ZX-Poly as a Model-Agnostic Synchronization Layer

> Design, 2026-09-28. Question answered: *can ZX-Poly be a pure
> synchronization wrapper over any machine model, so that a group can start as
> 4×48K, 4×128K, 4×+3, 4×Pentagon (any variant) and so on? If so, what does it
> need, and how does it integrate?*
>
> It builds on [quad-instance-architecture.md](quad-instance-architecture.md),
> referred to below as QI. QI describes the mechanism with Pentagon as the
> default. This document removes the Pentagon assumption, lists every
> model-specific point, and defines the integration surface. unreal-ng paths
> are repository-relative.

## 1. Answer

**Yes.** The ZX-Poly layer can be a wrapper that never touches a model's
emulation. Every mechanism in QI is either already model-agnostic in the
code, or can be made so with a small hook at a place every model already
passes through:

| QI mechanism | Where it lives | Model-agnostic? |
|:--|:--|:--|
| State replication (CPU, RAM, paging, devices) | TTD checkpoint capture/restore + peripheral registry; models add their own latches through `PortDecoder::GetTTDModelStateIds` / `CreateTTDSerializers` (+3, Scorpion, ATM, Profi already do) | yes, by design |
| Input gating | `Keyboard::OnKeyPressed` target-ID filter + `IsHostInputSuppressed()` | yes; mouse/joystick need the same gate |
| Input replication | TTD input journal (`TTDTimePoint`, `ServiceInput`) | yes |
| Port interception (ZX-Poly ports, floating bus) | **one choke point for all models**: `Z80::in()` / `Z80::out()` in `core/src/emulator/cpu/z80.cpp`, which call the model's `DecodePortIn/Out` and compute the floating bus | yes, with one hook (§4.1) |
| VRAM line capture | `ScreenZX`, the base screen every model's ZX-classic mode renders through (`VideoController::CreateScreen`) | yes for ZX-classic video; other video families need their own capture (§5) |
| Frame driving | `Emulator::RunFrame`, and the master's `MainLoop::OnFrameEnd()` | yes |
| TTD sessions, group seek | per instance | yes |

The only thing that is **not** universal is the *picture*. ZX-Poly
composition is defined for 1-bit-per-pixel + attribute screens. Models whose
current video mode is something else (ATM EGA / hi-res, Profi 512, Pentagon
16-colour, ALCO) run fine as a group, but the composer can only show the
master for those modes (§5).

What "wrapper" means here:

- **Model decides everything the machine does:** timing, contention, paging,
  ROMs, devices, floating bus, video modes.
- **The layer decides only three things:** which instance talks to the host
  (input, audio, display), how the instances are kept in step (replication +
  frame driving), and how the four pictures are combined.

## 2. Group rules (model-independent)

1. **Homogeneous.** All four instances use the same model, the same config
   file, the same ROM set and the same feature flags. The group records a
   fingerprint of the effective config at creation. A settings change from
   the UI or WebAPI is applied to all four at the same frame boundary, or
   refused.
2. **The master is a normal running instance.** Instance 0 runs its own
   `MainLoop`: real-time pacing, audio, display, debugger, exactly as any
   single machine today. The group attaches to its frame end and drives the
   slaves from there (§4.4). Nothing about how the master runs changes.
3. **Slaves are headless members.** Instances 1–3:
   - are never `Start()`ed;
   - have host input suppressed;
   - have audio muted (`SoundManager::mute()`);
   - skip RGBA rendering.

   They are driven frame by frame and hidden from default listings, but
   addressable by ID.
4. **Replication at an entry point, overlay on top** (QI §4), then
   frame-by-frame lockstep.
5. **Group size is a parameter.** ZX-Poly is 4. The mechanism does not care,
   so 2 or 8 instances are possible for other colour schemes (a
   Spec256-style 8-plane experiment, a 2-plane "Gigascreen-like" test), each
   with its own composer rule.

## 3. Model-specific points

### 3.1 Support matrix

| Model | Group runs | Poly picture | Notes |
|:--|:--|:--|:--|
| **48K** | yes | yes (ZX-classic) | No `#7FFD`, so no shadow screen and no `.zxp` (a `.zxp` holds 128K per module). Has a floating bus → master-authoritative value (§3.2). ULA contention identical across instances |
| **128K / +2** | yes | yes | Floating bus → §3.2. Contention identical |
| **+2A / +3** | yes | yes | Gate-array floating bus only on `#0FFD`-type ports (`GetGateArrayFloatingBus`) → §3.2. `#1FFD` special paging: part of replicated paging state. uPD765 FDC: must be shown deterministic by T5 with disk loading |
| **Pentagon 128/512/1024** | yes | ZX-classic modes yes; 16-colour mode no | The ZX-Poly **reference preset** is Pentagon + `intlen=36` + mode 4 (zxpoly's own profile). Floating bus only if enabled in config |
| **Scorpion / ProfScorp** | yes | ZX-classic yes | Attribute-latch floating bus → §3.2. Monitor/shadow ROM paging lives in replicated model state |
| **ATM710 / ATM3 (ZX-Evo)** | yes | ZX-classic modes only | EGA/hi-res/text modes: master shown, no composition until a mode-family composer exists (§5). Hardware turbo is deterministic across instances |
| **Profi 1024** | yes | ZX-classic only | 512×240 mode: master shown |
| Not creatable yet (TSL, GMX, …) | follows creatability | — | Nothing ZX-Poly-specific; when a model becomes creatable it inherits the layer |

**`.zxp` compatibility.** A `.zxp` is four 128K modules with `#7FFD`. It
loads into any group whose model has standard 128K `#7FFD` paging (128K, +3,
Pentagon, Scorpion, ATM/Profi in 128K-compatible mode) and is refused for
48K, with a message.

### 3.2 Values derived from each instance's own video memory

This is the only source of *legitimate* divergence the layer must neutralize
per model. On 48K, 128K, +3 and Scorpion, an `IN` from an undecoded port
returns the byte the ULA is fetching at that moment: the **floating bus**.
In a ZX-Poly group each instance fetches from **its own** video memory, so a
game that syncs to the floating bus (Arkanoid, Cobra, Sidewize, …) would read
different values on master and slaves once the planes differ, and split.

The real ZX-Poly answers the floating bus from module 0 only (MB:743-760). The
layer does the same, generically:

- `Z80::in()` already computes the floating bus in one place for all models
  (`ula->GetFloatingBus()` / `GetGateArrayFloatingBus()`).
- The post-decode hook (§4.1) tells the layer "this value came from the
  floating bus". On the master the layer logs `(tInFrame, port, value)`. On a
  slave it substitutes the master's logged value.
- This needs the frame pipeline's ordering (master before slaves), which QI
  already has.

No other CPU-visible path depends on video memory content: contention
depends on addresses, which are identical, not on data.

### 3.3 Timing, INT, turbo

- Frame length, INT position and length, contention, even-M1 rules: each
  instance uses its model's own values. Identical config means identical
  timing, so nothing is replicated.
- Hardware turbo strobes (Pentagon, Scorpion, ATM) happen inside the program,
  so all four switch at the same T.
- The host speed multiplier is queued and applied at a frame boundary
  (`Z80::BeginFrame`). The group applies it to all four at the same frame.

### 3.4 Devices

Slaves keep full devices (QI §5).

- **Deterministic by construction:** AY/TurboSound, beeper, Covox,
  Kempston, WD1793, uPD765, tape. The same state and the same writes give the
  same reads.
- **Must be pinned per model:**
  - FDC flaky-sector emulation `rand()` (`io/fdc/flakysectoremulator.h`):
    off or identically seeded for group members;
  - the SMUC/NVRAM RTC reads the host clock (`io/rtc/smucnvram.cpp:226`):
    use its existing fixed-time mode for group members.
- **Heavy devices with their own CPU** (General Sound, NeoGS, MoonSound):
  deterministic, but a slave pays the full cost of their CPU. The layer
  offers a `slave_io = replay` mode for such configs (§4.2), in which the
  slave does not need the device at all.

## 4. What needs to be built

### 4.1 Port hook in `Z80::in()` / `Z80::out()` (shared code, one branch each)

Every model's port traffic passes these two functions. The existing
full-decode observer tap (`NotifyFullDecodeIn/Out`) is **not** enough: it
lets the model decode run as well, so `OUT (#3D00)` would still reach the ULA
`#FE` port (A0 = 0) on every model. Add one optional interceptor pointer to
the context:

```cpp
// core/src/emulator/... (new, tiny)
class IPortInterceptor
{
public:
    virtual ~IPortInterceptor() = default;
    // Pre-decode: return true to consume the access (model decode skipped)
    virtual bool InterceptIn(uint16_t port, uint8_t& value) = 0;
    virtual bool InterceptOut(uint16_t port, uint8_t value) = 0;
    // Post-decode: final bus value, and whether it came from the floating bus
    virtual void OnInResult(uint16_t port, uint8_t& value, bool fromFloatingBus) = 0;
};
```

- **Stock instances:** null pointer, so one predictable branch per `IN`/`OUT`.
  Verified by `core-benchmarks` (QI T10).
- **Group members:** `ZXPolyPortInterceptor` handles:
  - the ZX-Poly platform ports `#3D00` and `#x0FF` module registers, for the
    TRD-loader path (QI §4.3) and module-identity reads. These are the same
    for every model;
  - the floating-bus substitution of §3.2;
  - the optional `IN` log check (QI §5.3), and `slave_io = replay` (§4.2).

### 4.2 Slave IO modes

| `slave_io` | Slave `IN` | Slave `OUT` | Use |
|:--|:--|:--|:--|
| `full` (default) | own devices; floating bus from the master | own devices | normal play; the stock-machine semantics |
| `replay` | every value from the master's `IN` log (module-local ports answered locally) | only paging (`#7FFD`, `#1FFD`, model paging latches); the rest is dropped | configs with heavy devices (GS/NeoGS/MoonSound), or proving a device-determinism problem |

Both use the same log produced by the master's interceptor, so `replay` is not
extra machinery. It is `full` with the substitution widened from "floating
bus" to "everything". Which ports count as paging comes from the model's
existing port map (`PortMapEntry::latch`).

### 4.3 VRAM line capture in `ScreenZX` (shared code, null-checked)

- **Hook.** At each paper line's start T, copy that line's 32 bitmap and 32
  attribute bytes from the instance's active screen page into a 192 × 64
  buffer (QI §7). The active page comes from the screen state the model
  already maintains (`SetActiveScreen`). That covers 48K (fixed page), 128K/+3
  (`#7FFD` D3), Pentagon, Scorpion, and the ZX modes of ATM and Profi.
- **Two paths.** With ScreenHQ on, the per-T `Draw()` path triggers the hook.
  With ScreenHQ off (`RenderFrameBatch` at `MainLoop::OnFrameEnd`), capture
  runs once per frame from final RAM. That matches zxpoly's own
  "less resources" option and is the right choice for slaves in turbo.
- **Signalling.** When the active video mode is not ZX-classic, the hook
  reports "not capturable" and the composer shows the master (§5).

### 4.4 Frame-end attachment on the master

`MainLoop::OnFrameEnd()` already runs once per completed frame, and is where
recording is served. The group registers one frame-end callback on the
master:

1. hand the master's applied input events and its `IN` log for frame k to the
   slaves;
2. schedule the slaves' frame k (pipelined, same-frame or sequential, QI
   §5.5);
3. when they finish, compose frame k into the master's framebuffer, then run
   the frame-end divergence check.

Debugger pauses on the master (breakpoints mid-frame): the group advances the
slaves to the master's exact `TTDTimePoint` with `RunTStates`, so all four
can be inspected at the same instant.

### 4.5 New code (no model code touched)

New folder `core/src/emulator/zxpoly/`:

| File | Content |
|:--|:--|
| `zxpolygroup.h/.cpp` | group lifecycle, config fingerprint, replication, frame driving, divergence policy, group TTD control |
| `zxpolyportinterceptor.h/.cpp` | `IPortInterceptor` implementation: platform ports, floating-bus substitution, `IN` log, `replay` mode |
| `zxpolyinputreplicator.h/.cpp` | master applied input → slave journals |
| `zxpolylinecapture.h/.cpp` | per-instance 192 × 64 buffers + capture hook target |
| `zxpolyscreencomposer.h/.cpp` | pure composer: modes 0–7, and later other schemes |
| `zxpolyconfig.h` | group options (§6) |

Tests follow the file names (`zxpolygroup_test.cpp`, …), as the project
naming rule requires.

### 4.6 Shared-code changes (the complete list)

| Change | Size | Risk to other models |
|:--|:--|:--|
| `IPortInterceptor` pointer + two call sites in `Z80::in/out` + the floating-bus flag | ~30 lines | one null check per `IN`/`OUT`; benchmarked |
| Line-capture hook pointer in `ScreenZX` (HQ path + batch path) | ~30 lines | one null check per paper line; benchmarked |
| Input gate for mouse and joystick (the keyboard already has it) | ~20 lines | none when not suppressed |
| `EmulatorManager`: group-member flag, filter in default listings, group lookup | ~60 lines | listing tests |
| Frame-end callback registration on `MainLoop` (if no suitable observer exists) | ~20 lines | none when unregistered |
| Cross-instance checkpoint restore, if the prototype shows `TTDCheckpoint` cannot be restored into another instance's session | small, TBD by prototype | TTD tests |

Everything else is new code in the `zxpoly/` folder.

## 5. The picture across video families

ZX-Poly composition (QI §7) needs, per pixel, one bit per module plus
attributes. That exists in every model's **ZX-classic** mode, which is also
what the whole adapted corpus uses. For other modes:

| Video family | In a group | Path to poly composition |
|:--|:--|:--|
| ZX-classic (all models) | composed, modes 0–7 | done in v1 |
| Pentagon/ATM multicolour-style per-line attributes | composed: per-line capture already samples attributes per line | v1, as a free consequence of per-line capture |
| Pentagon 16-colour, ATM EGA, ALCO | master shown | a family composer is possible (e.g. plane-wise OR/XOR of 4bpp indices), but there is no content and no reference semantics, so not planned |
| ATM hi-res / text, Profi 512 | master shown | same as above |

When [2026-09-27-video-debug-translation](../2026-09-27-video-debug-translation/)
(PLAN #42, `IVideoMapper`) lands, the capture hook can ask the mapper what
the beam fetches in the current mode instead of assuming the ZX layout. That
is the clean way to add a family composer later, with no change to the
layer.

## 6. Integration surface

### 6.1 Starting a group

The model stays an ordinary model; ZX-Poly is a start option, not a new
`MEM_MODEL`.

- **WebAPI.** `POST /api/v1/emulator/start` with
  `{"model": "128k", "zxpoly": {"planes": 4, "video_mode": 4, "slave_io": "full", "divergence": "strict"}}`
  returns the master's ID plus a `group` object with the member IDs.
  Group endpoints under `/api/v1/zxpoly/{groupId}/`:
  - `status` (members, pipeline mode, divergence counters);
  - `video-mode`;
  - `entry-point` (replicate now / at address);
  - `overlay` (apply plane data per module);
  - `seek` (group TTD).
- **MCP / CLI / Python / Lua.** The same operations through
  `emulator_manage` actions (`start` with `zxpoly`, `zxpoly_status`,
  `zxpoly_replicate`, …). The project rule applies: every automation module
  serves the same information from the same source.
- **Model list.** `GET /api/v1/emulator/models` gains a `zxpoly` capability
  per model: `full` (ZX-classic composition), `partial` (group runs, some
  modes master-only) or `no` (not creatable). It is derived from §3.1, so the
  runtime list stays authoritative.
- **Config.** An optional `[ZXPOLY]` section in a model's `unreal.ini` for
  defaults: planes, video mode, `slave_io`, divergence policy, pipeline mode.
  The reference preset is `data/configs/zxpoly/unreal.ini` = Pentagon +
  `intlen=36` + mode 4, the setup that matches the zxpoly golden frames.

### 6.2 Qt

- In the model picker, a "Start as ZX-Poly group" option on any model whose
  capability is not `no`.
- A poly video-mode selector (0–7) and a per-module view (mode 0–3 = one
  module).
- A divergence indicator (counter, and the last report with module, frame,
  T and PC).
- Debugger windows can target any member by ID, as they would any instance.
  The window title shows `ZX-Poly[n]`.

### 6.3 Video wall

A group shows as **one** tile, the master's composed framebuffer. Slaves are
hidden members, so they never take tiles.

## 7. Tests on top of QI §10

QI's T1–T12 are written against Pentagon. Model-agnostic coverage adds:

| # | Test | Proves |
|:--|:--|:--|
| G1 | T4 + T5 (replication, lockstep with gated and replicated input) parameterized over **48K, 128K, +3, Pentagon, Scorpion** (the GTest `AllModels` pattern the suite already uses) | the layer is model-agnostic |
| G2 | floating-bus title (a floating-bus-synced demo or game) on 48K and 128K with differing planes: lockstep holds, and slave floating-bus reads equal the master's | §3.2 |
| G3 | +3 with uPD765 disk loading during the run; Pentagon/128K with Beta128 and flaky sectors pinned | §3.4 device determinism per model |
| G4 | `slave_io = replay` with General Sound enabled on the master only: lockstep holds, and the slaves have no GS instance | §4.2 |
| G5 | `.zxp` refused on a 48K group with a clear message; accepted on 128K, +3, Pentagon | §3.1 |
| G6 | ATM in a hi-res mode: the group runs, and the composer reports "master only" | §5 |
| G7 | stock-model regression: full `core-tests` + `core-benchmarks` with the interceptor and capture hooks compiled in but unset | §4.6 risk |

## 8. Effort delta over QI

| Piece | Effort |
|:--|:--|
| `IPortInterceptor` hook in `Z80::in/out` + floating-bus flag + substitution | 2 d |
| `slave_io = replay` (paging-latch selection from `PortMapEntry`) | 1–2 d |
| Capture hook in both `ScreenZX` paths + "not capturable" signal | 1–2 d |
| Master frame-end attachment instead of a group-owned master thread | 1 d (it replaces QI's thread plumbing; it does not add to it) |
| Per-model device pinning (FDC flaky sectors, RTC fixed time) | 1 d |
| Capability in the model list + start option in WebAPI/MCP/CLI/Qt | 2–3 d |
| G1–G7 | 3–4 d |

That is roughly **+2 weeks** over QI's Pentagon-only estimate. Most of it is
tests and surface, not mechanism.

## 9. Decisions

| Question | Decision |
|:--|:--|
| New `MEM_MODEL` for ZX-Poly? | No. ZX-Poly is a group start option on any model; `configs/zxpoly` is only a reference preset (Pentagon + `intlen=36`) |
| Where does the layer hook in? | `Z80::in/out` (ports + floating bus), `ScreenZX` (line capture), the master's `MainLoop::OnFrameEnd` (frame driving), and existing input gates. No model decoder or model screen is modified |
| Mixed models in one group? | No. Homogeneous config, fingerprinted |
| Floating bus? | Master-authoritative for slaves, on every model that has one |
| Heavy devices on slaves? | `slave_io = replay` makes them unnecessary |
| Non-ZX video modes? | The group runs, and the master is shown. Family composers only once `IVideoMapper` (#42) exists and there is content that needs them |
| `.zxp` on 48K? | Refused |
| Group size? | Parameter; 4 for ZX-Poly |
| Order of work | QI prototype T4–T6 first, directly parameterized over 48K/128K/+3/Pentagon (= G1). If G1 is green, the model-agnostic layer costs no more than the Pentagon-only one, so build it that way from the start |
