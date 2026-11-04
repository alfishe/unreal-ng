# Device access watch: what a program uses, from the machine's own decode

| | |
|---|---|
| **Date** | 2026-11-02 |
| **Status** | landed 2026-11-03 (`2c34ef033`); follow-ups in Known gaps |
| **Replaces** | `PortReadWatch` (`core/src/emulator/io/controllers/portreadwatch.h`) and its register probe |
| **Feeds** | C6 auto connector ([../2026-10-17-controller-manager/TODO.md](../2026-10-17-controller-manager/TODO.md)), the signature hook (D16), the debugger's device views ([../2026-10-31-debugger-device-io](../2026-10-31-debugger-device-io/README.md)) |

## Why

The current watch classifies the bus address with the ports it knows: `#1F` is Kempston, `#7F` is Fuller, the
three `#xxDF` are the mouse, A0 = 0 is the keyboard, and anything else goes to a probe that knows two registers
(AY R14, NR #B2). That is wrong as soon as the machine differs from the 48K picture:

- the port is not the device. `#1F` is Beta-128 in DOS mode, the Next's DAC on OUT, the AMX mouse's PIO with its
  card fitted; `#7F` is the WD1793 data register under TR-DOS; a Pentagon decodes Kempston on the full low byte,
  a Scorpion its own way, a ZX-Evo through its AVR;
- a card can shadow a built-in (an IORQGE claim), so what answers changes with the configuration;
- every new device (AMX, an AY mouse, the 128 keypad, a light gun, ULA+, TS-Conf's own ports) is one more `if`.

The machine already knows the answer. The slot manager's registry declares, per machine and as configured, which
device answers each access (`slots::PortOwners`: the built-ins' and the fitted cards' `PortClaim`s with mask, match,
direction, DOS gate, IORQGE shadowing, card order). `PortHistories` already attributes every IN / OUT by it. The
watch is to use the same source and know no port at all.

## Goals

1. One mechanism on the IN / OUT path, for every device of the registry: inputs (keyboard, joysticks, mice, pads,
   guns) and every other peripheral (disk, sound, network, storage), on every machine.
2. Attribution by the machine's configuration: the registry's claims, as the slot manager built them; a card fitted
   or an option changed rebuilds it.
3. Below the device, the **channel** the access means, declared as data next to the claims, not coded per chip:
   the half-row of a ULA read, the register of an AY / NextREG / ULA+ data access.
4. What the code does with an input byte: the bits it tests after the read (today's row bits, for every input
   read, not only the keyboard).
5. Where the code reads from: the reader sites (PCs), the hook the signature library attaches to.
6. Zero cost off (the flag the CPU and the decoder already test), one table load and a counter increment on.
7. The interpretation (which connector, which device) is a table of the connector kinds over generic evidence; the
   watch never names a connector.

Not goals: memory-mapped I/O (none of the input devices is memory-mapped; a device that is gets a claim kind when it
needs one), the second CPU of a card (GS, Sprinter's accelerator): main-CPU accesses only.

## Design

### 1. One owner table, shared

`PortHistories::BuildTable` already turns the claims snapshot into a table `(out << 17 | dos << 16 | port) ->
device`. It becomes `slots::PortClaimTable`: the same key, the value the **claim index** (uint8; 255 = the board's
own ports), built from `PortOwners` on the control path when the slot manager publishes a result, swapped in on the
machine's thread at the next access. `PortHistories` and the watch both read it (claim -> owner is one more array
load). Claims with a `when` condition (an option) are resolved when the table is built; the rebuild follows every
slot manager change, so the table always matches the decode.

The hook stays where the histories hook: `PortDecoder`'s IN and OUT after the decode applied (`portdecoder.cpp`,
`_portHistories.AnyOn()`), behind one combined "an access observer is on" flag. The CPU's IN-result hook loses
the watch (`Z80::portReadWatch` goes); the tested-bits scan needs the PC after the IN, which the decoder already gets.

### 2. Channels: declared beside the claim

A `PortClaim` gets an optional channel rule (`slots::ChannelRule`), data in `refdata`:

| Rule | Channel of an access | Declared on |
|---|---|---|
| `None` (default) | 0: the claim is one channel | Kempston `#1F` / `#37`, Fuller `#7F`, each Kempston mouse port, Covox |
| `AddressBits{mask}` | the bus bits under `mask`, one channel per low bit (a read with several low is a multi read) | ULA `#FE` (mask `#FF00`: the half-rows) |
| `Select` | the access writes the device's index: the watch keeps the value per device | AY `#FFFD` OUT, NextREG `#243B`, ULA+ `#BF3B`, TS-Conf / Sprinter register selects |
| `Indexed` | the device's current index (kept from its `Select` claim) | AY `#FFFD` IN / `#BFFD` OUT, NextREG `#253B`, ULA+ `#FF3B` |

The watch tracks the select writes itself from the OUT traffic it sees anyway: no chip pays on the port path, and
a new indexed device is two lines of refdata. On `Select` / `SelectIndexed` claims `channelMask` is the set of
index bits: a written value with other bits set is not a register select and keeps the last one (the AY's `#0F`,
so a TurboSound's chip select `#FFFD` = `#FE` / `#FF` and a TSFM's FM writes leave the register alone; 0 = any
byte).

The selection a program made before the watch started (a snapshot, a frame of polling before the window opened)
is not in the traffic: at every start the watch asks the decoder once (`PortDecoder::SelectedIndex(role)`: the
TurboSound's bus register for `AyIo`, `NextBoard::SelectedRegister()` for `NextReg`). Until a select is seen or
seeded, reads of the indexed claim are the channel `<prefix>.unknown` (`ay.unknown`).

A channel is up to 256 per claim; the watch's tables are per (claim, channel) with a small fixed index: channels
are allocated on first sight (at most 64 per machine, more count as "other" of their claim).

### 3. Evidence per (device, channel)

Per frame, folded into a window of 50 frames like now, per (claim, channel) and direction:

- accesses in the frame, frames of the window with one;
- for reads: the bits the code tests after the read (the existing `TestedBitsAfterIn` scan, once per code address,
  for every read of a claim marked as input in refdata; others skip the scan);
- the reader sites: up to 8 distinct PCs of the IN per (claim, channel), with their hit counts.

Plus per frame: the multi-row keyboard reads (the ROM's scan) as their own channel of the ULA claim.

`DeviceAccessEvidence` reports it keyed by the registry id and the channel name (`ula.row.fe`, `ay.r14`,
`nextreg.b2`, `kempston-joystick.1f`): the channel name comes from the rule (half-row by its port's high byte,
register by its number, a plain claim by its documented port).

### 4. Interpretation: connector signatures over the evidence

The controllers' decision (`DecideFromPortReads` today) becomes a table, one row per connector kind and device:

| Connector / device | Wants | Evidence that counts |
|---|---|---|
| Kempston (board or card) | kempston-joystick | reads of the connector's own device, every frame |
| Next DB9 | md6-pad (6 buttons) | `kempston-joystick` every frame and `nextreg.b2` |
| Fuller card | fuller-joystick | reads of the card's device |
| Keyboard joystick | Sinclair 1 / 2, Cursor | the ULA channels of the layout's half-rows with the layout's bits tested (Sinclair 1 = row `#EF` bits 0-4, Sinclair 2 = row `#F7` bits 0-4, Cursor = `#F7` bit 4 and `#EF` bits 0, 2-4) |
| Kempston mouse | kempston-mouse | the mouse device's X / Y channels |
| AMX card | amx-mouse | the card's PIO channels |
| (hint) AY port A | Timex stick, 128 keypad, AY mouse | `ay.r14` reads |
| keyboard | the whole keyboard | multi-row reads or most half-rows every frame |

A connector row names a **device id and channels**, which the machine's connector list binds: the Kempston row
matches whatever device the machine's Kempston connector is (`kempston-joystick` built-in, a `kempston` card, the
Evo's AVR), so no row knows a port. A device the machine has not got never shows up in the evidence, so no row can
fire for it. The keyboard layouts come from the overlay tables the connectors already use (`zxkeys.h`), not a
copy.

### 5. Signature hook

`IProgramSignatureMatcher` (controllers library, no implementation yet): given the evidence's reader sites (PC, the
bytes around the IN read through the same side-effect-free reader) and the loaded media's identity, returns a
decision with a confidence or nothing. The auto decision asks it first and falls back to the table. The swsignature
library (D16) implements it later; a test fake proves the order now.

### 6. Surfaces

The evidence is generic, so it is a device view, not a controllers one:

- `ControllerControl` `auto` keeps its answer (the decision and its reason, now with device ids);
- the whole evidence on every plane, started / stopped explicitly as well as by a media load. Built as the
  monitoring name `devices.access` instead of a route of its own: the monitoring module gives the REST route, the
  WebSocket call, topic and readout, CLI, Python, Lua and MCP from one name; starting and stopping stay with
  `controllers auto apply` and the feature `deviceaccess`;
- the debugger's device tabs show a device's channels and reader sites from it.

## Cost

- Off: nothing new; the decoder's existing observer flag (now "histories or watch").
- On: one load from the claim table (shared with the histories), a switch on the claim's rule, an increment; the
  tested-bits scan once per code address. A/B on the step benchmark: off vs on vs on with every history on
  ([performance-guidelines.md](../../guidelines/performance-guidelines.md)); the watch runs only during its window
  (a load + 30 s, or on request).

### Measured (2026-11-02)

`BM_DeviceAccess` (`core/benchmarks/emulator/deviceaccess_benchmark.cpp`) runs 1000 `IN A,(C)` or `OUT (C),A`
through `Z80Step` on a Pentagon, with the watch folded once per block. Modes:

- **off**;
- **watch**: on, as the feature keeps it;
- **watch + histories**: the watch and all 8 port histories in ring mode.

The block has 1000 distinct code sites, so past the 8 kept per channel every access walks the full site list: the
watch's worst case. A polling loop has one or two sites.

Two runs, 6 and 8 repetitions, random interleaving, machine load 60-72. Median ns per access (run 2; run 1 within
±4 ns):

| Access | Port | Off | Watch | Watch + histories | Watch vs off | + histories vs off |
|---|---|---|---|---|---|---|
| IN | `#EFFE` (half-row) | 42.8 | 55.1 | 70.3 | +12.3 ns | +27.5 ns |
| IN | `#001F` (Kempston) | 28.2 | 37.7 | 50.6 | +9.5 ns | +22.5 ns |
| IN | `#FFFD` (AY register) | 37.4 | 46.3 | 58.3 | +9.0 ns | +20.9 ns |
| OUT | `#001F` | 42.6 | 53.7 | 68.2 | +11.1 ns | +25.7 ns |
| OUT | `#FFFD` (AY select) | 35.2 | 47.7 | 59.3 | +12.5 ns | +24.1 ns |

`OUT #FE` is left out: about 20 µs per access in every mode. That cost is the border write rendering the screen up
to the rewound T-state; the watch is within noise there.

Reading:

- **Watch on:** about 9-13 ns per port access, 25-35 % of the port path. Port accesses are a small part of a frame's
  instructions, so a program polling a few hundred ports per frame pays a few microseconds per frame.
- **Watch off:** the decoder tests one more flag next to the histories' flag (the CPU's old `portReadWatch` test is
  gone).

Not measured: the off path against master directly (a master benchmark build).

Default: stays off; the load window and the feature turn it on.

## Migration

1. `PortClaimTable` out of `PortHistories`, shared; histories unchanged in behavior (their tests).
2. `ChannelRule` in `PortClaim`; refdata for the ULA, the AY classes (128, Scorpion, Profi, ATM, Next, TS-Conf,
   Sprinter, the AY / TurboSound cards), NextREG, ULA+, the mouse; a refdata test that every built-in or card with
   a select port declares the pair.
3. `DeviceAccessWatch` on the decoder's path; `PortReadWatch`, its register probe and `Z80::portReadWatch` removed.
4. The decision table over `DeviceAccessEvidence`; the existing `AutoDecision_Test` cases rewritten on it, plus the
   cases the old watch got wrong: TR-DOS `#1F` / `#7F` under DOS, a Next DAC OUT on `#1F`, an AMX card's PIO,
   Kempston on a Scorpion / Evo decode.
5. The signature hook with a fake, the surfaces, the docs (`retro-devices.md` §6, `mapping-and-profiles.md` §6.1,
   command-interface chain).

## Decided

- Off by default (owner 2026-11-02: no always-on analyzer without a hot-path benchmark). Feature `deviceaccess`
  (FeatureManager, every automation plane) turns the continuous watch on; a media load still opens the bounded
  30 s window of the auto connector (C6) as before. The A/B of off / on / on with every history on is measured
  with the first version and recorded here; the default is revisited on it.
- The load window covers snapshots too (owner 2026-11-02): `LoadSnapshotStaged` asks for it as the tape and disk
  loads do. Only a program started without a load (a reset into a ROM cartridge game) is not watched unless the
  feature is on.

## Built (2026-11-02)

- **Claims.** `slots::PortClaim` has `channel`, `channelMask`, `input`, `channelName` (`slottypes.h`):
  - `Channel::AddressBits`: the ULA's half-rows (mask `#FF00`);
  - `Select`, `Indexed`, `SelectIndexed` (one port both selects on OUT and reads the selected register on IN: the
    AY's `#FFFD`);
  - `InputRole`: KeyMatrix, KempstonJoystick, KempstonJoystick2, FullerJoystick, KempstonMouse, AmxMouse, AyIo,
    NextReg.

  Refdata declares them on:
  - every AY claim set, MultiSound's YM, the Fuller card's AY;
  - NextREG, ULA+;
  - the Kempston joysticks and mice, the AMX card;
  - the Kempston mouse the emulator fits by `[INPUT] Mouse=` as `MachineDef::fitted` on 48K, 128K, +2, +2A, +3,
    Pentagon, Scorpion, ProfScorpion, GMX, Profi, Profi 3 (`A9 = 1`, `A5-A0 = #1F`, off in TR-DOS: the `#xxDF` family
    and its mirrors; `#xx1F` stays the joystick's, whose decode is checked first) and ATM 450 / 710 (exact low byte
    `#DF`). Not on the board, so the slot planner and the matrix do not see it (an AMX card on the edge connector
    is no bus fight); the attribution takes it after the cards, before the built-ins.

  The machines whose ULA had no built-in (Pentagon, Scorpion, GMX, ATM 450 / 710 / 3, TS-Conf, Profi, Profi 3,
  Sprinter) got a `ula` built-in with the board's own keyboard decode (Scorpion `#xxFE` with A1 and A5 high: mask
  `#0023`; ATM 710 `#0007`; Evo `#00FF`).
- **`PortClaimIndex`** (`ports/portclaimindex.h`): the claim table `(out << 17 | dos << 16 | port) -> claim`, out of
  `PortHistories`. Shared by the histories and the watch, built in `PortDecoder::BuildAccessObservers`.
- **`DeviceAccessWatch`** (`ports/deviceaccesswatch.h`):
  - the hook: `PortDecoder::OnPortInComplete` / `OnPortOutComplete` behind `On()`;
  - the frame boundary: in `MainLoop` before the controllers' frame;
  - requests (any thread): `RequestWindow`, `CloseWindow`, `SetContinuous` (the feature `deviceaccess`);
  - the old `PortReadWatch`, its register probe and `Z80::portReadWatch` are removed.
- **Decision.** `controllers::DecideFromAccess` over input roles; `ISignatureMatcher` / `DecideAuto` (matcher
  first).
- **Surfaces.** The `auto` evidence lists `channels[]` (device, channel, dir, port, input, frames,
  reads_per_frame, tested_bits, sites). The whole evidence is the monitoring name `devices.access` (2026-11-03,
  `deviceaccessnode.h`, one builder for both): `GET /api/v1/monitor/devices.access` (so the WebSocket `call`), the
  WebSocket topic and readout of the same name (a new value per folded frame, by the watch's `Revision()`), CLI
  `monitor devices.access`, Python / Lua `monitor("devices.access")`, MCP `inspect_state` aspect `monitor` (the
  summary: `channel_count`; the channels through `controllers auto`). Started and stopped by what starts it already:
  a media load, `controllers auto apply` (a window), the feature `deviceaccess` (continuous); no verb of its own.
- **Connectors by input role (2026-11-03).** The decision names the input role of the reads it rests on and the
  registry device that answered (`AutoDecision::input`, `source`), not a connector; each machine finds its own
  (`ControllerManager::FindForInput`; `Connector::input`, `source`): `#1F` is `kempston` on a Pentagon and
  `next.left` on a Next, `#37` `next.right`, an AMX card's PIO that card's `amx` socket. A fixed connector carries
  the board's device (the Next's MD pad). No connector for the role: the auto plug of the device, whose refusal
  names what is missing. `HostInputMachine_Test.AutoConnectorByInputRole`.
- **Card candidates (2026-11-03).** A board access carries the catalog cards that would answer it on the machine
  as it is (`slots::CardCandidates`, `slots/cardcandidates.h`): a card whose claims with the default options take the
  port in that direction (not a DOS-only claim), not fitted already, which the planner would plug (dry run, at the
  slot `SuggestSlot` gives) without a hard refusal. Several cards may answer one port (`#1F` is a Kempston
  interface's and an AMX mouse's PIO data A), so `RankCardCandidates` orders them by the whole window, the same rule
  for every card: input claims first, then the most board accesses the card answers, then the largest share of its
  own claims they touch (a program that only reads `#1F` gets the one-port Kempston first; one that also writes the
  PIO's `#5F` / `#7F` gets the AMX mouse). `ControllerManager::PortReads` fills them (the per-port lookup cached
  until `Rebuild`, the ranking per call); the watch's own evidence and `devices.access` stay without them.
  `controllers auto` lists them per channel (`candidates` [{`card`, `name`, `slot`, `input`,
  `needs_confirmation`}]). A board read in nearly every frame with an input candidate, when nothing else decides,
  is a hint naming the card (`AutoDecision::card`, `cardSlot`; `card`, `card_slot` in the `auto` decision): `#1F`
  on a bare 48K is "a Kempston joystick interface (kempston on edge.1) would". The Qt host says it once as a notice;
  fitting a card restarts the machine, so nothing is fitted. `CardCandidates_Test`,
  `AutoDecision_Test.Decide_BoardReadsNameTheCard`, `HostInputMachine_Test.BoardReadsCarryTheCardCandidate`.
- **Tests.** `DeviceAccessWatch_Test` (the mouse by each board's decode, the chip select keeping the register, the
  seeded selection), `AutoDecision_Test`, `HostInputMachine_Test.AccessWatch*`: a TurboSound Pentagon whose program
  selected R14 before the watch started; and every machine of the refdata running `IN A,(C)` / `OUT (C),A` on
  `#EFFE` through its real decoder, each seen as the keyboard.

## Known gaps

- **Undeclared ports.** A port the registry does not declare goes to `board` by its exact address: a bare 48K /
  128K has no Kempston joystick, it is a card there, so a `#1F` read with no card fitted is `board.001f`, with the
  `kempston` card as its candidate (Built: card candidates). A card is proposed, never fitted on its own.
- **Test time.** `HostInputMachine_Test.AccessWatchSeesTheKeyboardOnEveryMachine` creates every machine of the
  refdata: 1.3-1.5 s under a machine load of about 150, over the 1 s integration budget; shorten it or move the
  full sweep behind `TestTier::Long()` if it stays there under normal load.
