# ZX-bus slots: requirements

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft for owner review |
| **PLAN** | row #82 "Machine buses and slots, one model for every bus" |
| **Decisions** | [open-questions.md](open-questions.md) Q1-Q7 (all decided 2026-10-03) |
| **Design** | [architecture.md](architecture.md), [compatibility-matrix.md](compatibility-matrix.md), [tdd.md](tdd.md) |
| **First new card** | [ZX-MultiSound](../2026-10-03-zx-multisound/requirements.md) |

## 1. Why

Today every extension card is switched on by its own INI key in its own section, and each one reaches the Z80 ports
by one of two different mechanisms. Nothing stops a user from fitting a TurboSound FM together with a card that
already carries one, or a General Sound together with a NeoGS. Nothing says which cards a machine can physically
take. The owner's direction (2026-10-01/02):

> machine -> bus extension slots -> devices plugged into the slots. On real hardware the number is limited; for us it
> is unlimited until port conflicts, and even then the device in that slot is simply disabled.

The Sprinter's ISA slots ([2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/tdd.md)) are the first bus built this
way. This design does the same for every Spectrum-family machine and adds a compatibility matrix.

## 2. Glossary

| Term | Meaning |
|---|---|
| **Bus** | A connector family on the machine that cards plug into. Kinds: the **ZX-bus** (also called NemoBus: Pentagon, ZX-Evo, ATM, Profi, ...), the **Sinclair edge connector** (48K, 128K, +2, +3), the **Scorpion** connector, the Sprinter's **ISA-8** bus (existing design), and the **AY socket** (the sound chip's own socket, where a TurboSound board replaces the chip). |
| **Slot** | One place on a bus that holds one card. Named `ay-socket`, `zxbus.1`, `zxbus.2`, ... |
| **Card** | A device that plugs into a slot: General Sound, NeoGS, MoonSound, TurboSound FM, ZX-MultiSound, a network card, ... |
| **Function** | Something a card provides that two cards cannot both provide: `ay-socket` (the AY / TurboSound role), `gs`, `saa`, `soundrive`, `midi`, `opl4`, `network.<kind>`, ... Two cards with a common function are incompatible. |
| **Built-in device** | Part of the machine's board, not a card: the 128K's AY, the ZX-Evo's TurboSound in the FPGA, the Sprinter's Covox-Blaster. Never removed. |
| **IORQGE** | A ZX-bus signal. A card pulls it when it answers a port, and the machine's own decoder then stays silent on that cycle. This is how a card *shadows* a built-in device. |
| **Shadowed** | A built-in device that is still fitted but does not answer or sound, because a card drives IORQGE on its ports. |
| **Adapter** | A slot entity that lets a card sit on a bus it was not made for (for example ZX-bus card on a Sinclair edge connector). |
| **Plan** | The complete list of changes one request causes (cards added, removed, shadowed, functions lost). Computed first, applied at once or not at all. |
| **Fit** | Whether a card's required bus signals exist on the bus: `real` (they do), `adapter` (through an adapter), `unrealistic` (an override forced it). |

## 3. Functional requirements

### 3.1 Machines declare buses

- **R-BUS-1.** Every machine model declares its buses and, per bus, the signals present (IORQGE, /IODOS, /DOS, /WAIT,
  +12 V, /RESET, /M1, /RFSH, /CSROM, /RDROM, /BUSRQ), the number of physical slots (informational; our limit is
  unlimited, owner rule), the **arbitration mode** (`CardWins`, `BoardWins`, `UlaOnly`, `None`; research-machines.md
  §1), the board ports hidden from the slots (`BoardWins`) and the read-conflict rule.
- **R-BUS-2.** Every machine declares its built-in devices with the functions they provide and the ports they answer,
  and whether a built-in function is switchable (by a machine setting) or fixed.
- **R-BUS-3.** The machine declaration is the only place model knowledge lives. Shared code never tests a model id to
  decide a card's fate (the `TsConfIsolation_Test` rule).
- **R-BUS-4.** Every Spectrum-family machine that is creatable today gets its declaration (the list in `AGENTS.md`).
  The Sprinter's ISA bus keeps its own design; its ZX-bus adapter card becomes a bus host for ZX-bus cards (ISA phase
  I5 is folded into this work).

### 3.2 Cards declare what they need and what they occupy

- **R-CARD-1.** A card type declares: its id and display name; the bus kinds it plugs into; the signals it needs; the
  functions it occupies, as a function of its current options (DIP switches); the ports it claims, each with "drives
  IORQGE" or not; its options with defaults; its media slots; its mixer rows; its TTD blob id.
- **R-CARD-2.** The card catalog is one table in core. Every surface (INI, Qt, five automation surfaces, docs) reads
  it; nothing duplicates it.
- **R-CARD-3.** A card is built from shared chip modules (YM2203 engine, GS, SAA1099, Covox, ...). A card never forks
  a chip module; a chip that needs card-specific behavior gets a parameter in its config or an isolated copy (owner
  rule "isolate odd chips").

### 3.3 Compatibility

- **R-COMP-1. Function clash.** Two cards with a common function are incompatible. The matrix
  ([compatibility-matrix.md](compatibility-matrix.md)) is generated from the declarations and shown on every surface.
- **R-COMP-2. Options change claims.** A card's functions follow its options: a ZX-MultiSound with its GS switched off
  does not occupy `gs`. Changing an option goes through the same check as plugging a card in.
- **R-COMP-3. Bus fit.** A card whose required signals are missing on the slot's bus does not fit (open-questions Q5).
  An adapter can make it fit. An override makes it work logically, marked `unrealistic`.
- **R-COMP-4. Shadowing.** On a `CardWins` bus, a card that drives IORQGE on the ports of a built-in device shadows
  that device (Q2). On a `BoardWins` bus a card cannot shadow: its claims on board ports are dead for `Iorq` cards
  (an incompatibility) and a read bus fight for `RdWr` cards, resolved by taking a socketed built-in chip out (Q7). If the
  slot that is shadowed holds a card (a TSFM in the AY socket), that combination is listed in the matrix as pointless
  and treated as incompatible.
- **R-COMP-5. Built-in devices are never removed.** A request that would need a fixed built-in function removed is
  refused even with the override flag. A switchable built-in function is switched off, and the reply says so.
- **R-COMP-6. Accidental port clash.** A port clash between two cards that the function matrix does not cover leaves
  both plugged in; the later card (by slot order) is disabled with the reason. Adding more cards stays possible.

### 3.4 Operations

- **R-OP-1.** Operations: list buses and slots; list the card catalog (with the matrix); plug a card into a slot;
  remove a card; replace a card; change a card's options; describe one slot; dry-run any change.
- **R-OP-2. Plan first.** Every change computes its plan: every installed card that clashes with any function of the new
  card (the union), shadowed devices, lost functions, media to release, bus fit. One new card can push out several
  installed cards at once (example: plugging a ZX-MultiSound removes a TSFM, a GS and a SounDrive card in one step).
- **R-OP-3. Applied by a restart.** An allowed plan is written into the machine's configuration and the machine is
  restarted with it (owner decision Q6). The whole plan takes effect or none of it does; there is no hot plug.
- **R-OP-4. Qt UI.** An incompatible card replaces the cards it clashes with, and a warning lists what was removed,
  shadowed and lost, with an "Undo" action. A bus-fit problem asks the user to confirm the override, explaining that
  real hardware cannot do it.
- **R-OP-5. Automation.** By default an incompatible or non-fitting request is refused with the plan as the reason.
  With `replaceIfIncompatible: true` the plan is applied; the reply lists removed cards (with their full
  configuration, enough to put them back), shadowed devices, lost functions, released media and the fit of the new
  card. `dryRun: true` returns the plan without applying it.
- **R-OP-6. Media.** Removing a card releases its media slots by the media manager's eject rules. A medium with unsaved
  changes blocks the plan unless the request carries a disposition (`save` / `discard`).
- **R-OP-7. TTD.** No slot change while TTD records (the device set is fixed per session: TTD v2 decision D38, branch `ttd-engine`, `phase-2-device-state-tdd.md` §5.4.3). The
  refusal names the recording session.
- **R-OP-8. Always a restart (Q6).** Every slot change (plug, remove, replace, options) restarts the machine with the
  new configuration, the same way a model switch rebuilds the emulator. Media follow the restart as they follow a
  model switch. No card is ever created or destroyed inside a running machine. This includes the General Sound
  personality switch, which becomes a slot replace.
- **R-OP-9. Model switch.** On a model switch, cards that fit the new machine's buses stay in their slots; the others
  are removed and reported, by the same plan and reply as R-OP-5.

### 3.5 Configuration

- **R-CFG-1.** One `[SLOTS]` section in the machine INI is the single source of truth (Q4). Key form, for example:
  `ay-socket = tsfm`, `zxbus.1 = multisound`, `zxbus.1.dip = ym,saa,gs,sd`, `zxbus.1.gsRam = 2M`.
- **R-CFG-2.** Old keys (`[SOUND]` TurboSound / TSFM / GS type, `[GS]`, `[NGS]`, MoonSound, Covox, `[NETWORK] Card=`)
  are read at load, translated into slots and logged as deprecated. Shipped configs in `data/configs` are converted
  at once. The old keys are removed after a few releases.
- **R-CFG-3.** An INI with an incompatible set is loaded by the same plan as an automation request without the
  override: the first card in slot order wins, the rest are disabled with reasons, and the machine starts.

### 3.6 Reports

- **R-REP-1.** One slot report (core) feeds every surface: per slot the card, its options, functions, fit, state
  (`active` / `disabled: <reason>`), its ports with the IORQGE flag, media slots, mixer rows; per built-in device its
  state (`active` / `shadowed by <slot>` / `switched off`).
- **R-REP-2.** The compatibility matrix is available as data (pairs and reasons) on every surface.

## 4. Non-functional requirements

- **R-NF-1. Zero cost.** A port that no card claims costs nothing more than today; a machine without cards costs
  nothing more than today. A/B benchmark on the port hot path (`docs/guidelines/performance-guidelines.md`) for every
  migrated card.
- **R-NF-2. Determinism.** The slot set and every card's options are part of the TTD session header and of snapshots
  that carry device state. Replay is bit-exact (sealed replay principle).
- **R-NF-3. Automation parity.** CLI, WebAPI + OpenAPI, MCP, Lua, Python and Qt, each with its docs, plus a verified
  recipe in `.recipe/`.
- **R-NF-4. Cross-platform, zero warnings** (gcc, clang, mingw, msvc).
- **R-NF-5. No regressions.** Every existing card behaves exactly as before its migration: its tests, the TTD corpus
  and the machine boot tests stay green without re-recording, unless a recorded format change is the point.

## 5. Out of scope

- Electrical simulation of the bus (bus fights are resolved by documented rules, not voltages).
- Slot changes inside a TTD recording.
- Machines that are not Spectrum-family (none today besides the Sprinter's native mode, which keeps its ISA design).
