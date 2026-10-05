# Slots: the cards on the machine's buses

A ZX Spectrum clone takes add-on cards: a General Sound or a NeoGS, a TurboSound FM in the AY socket, a MoonSound,
a Covox or SounDrive, a network card, the ZX-MultiSound. Each card sits in a **slot** of a **bus** of the machine. The
emulator keeps a list of the machine's buses and of the cards in them, checks that the cards can work together, and
tells you what a change would do before it does it. The same verbs exist in the Qt window (Machine > Slots), the
WebAPI, the CLI, MCP, Lua and Python. This page is the reference; each surface's own documentation links here.

Design: [ZX-bus slots architecture.md](../inprogress/2026-10-03-zx-bus-slots/architecture.md); the compatibility
tables: [compatibility-matrix.md](../inprogress/2026-10-03-zx-bus-slots/compatibility-matrix.md); step-by-step
automation: [.recipe/machines/slots.md](../../.recipe/machines/slots.md).

These slots are not the **media** slots of [media.md](media.md) (floppy drives, SD sockets): a card can bring media
slots with it (the NeoGS brings `sd.ngs`).

## Words used here

| Word | Meaning |
|---|---|
| **Bus** | A connector family of the machine: `zxbus` (ZX-bus / NemoBus), `edge` (the Sinclair edge connector), `iobus` (ATM Turbo), `profi-bus`, `isa` (Sprinter), `cpu-socket` (a card on an adapter in the Z80 socket), and `ay-socket` (the AY chip's socket) |
| **Slot** | One place on a bus: `zxbus.1`, `zxbus.2`, `edge.1`, `ay-socket`. `zxbus.next` means "the next free one" |
| **Card** | What sits in a slot: `gs`, `gs-lw`, `neogs`, `tsfm`, `ts`, `ay`, `none` (an empty AY socket), `moonsound`, `covox-fb`, `soundrive`, `multisound`, `zxnetusb`, `zx-wifi` |
| **Built-in device** | Something the board itself has: the AY, the Beta 128 disk interface, a Covox. A card can **shadow** it (the card answers its ports, the built-in stays silent) or switch it off |
| **Function** | The job a card does: `gs`, `ay-socket`, `opl4`, `covox-fb`, `soundrive`, `midi`, ... Two cards with the same job do not fit together (rule D1) |
| **Fit** | How a card fits the bus: `real` (as on real hardware), `adapter` (behind a bus adapter), `unrealistic` (it works in the emulator although the bus lacks a signal the card needs) |
| **Plan** | What a change would do: the cards it removes, the devices it shadows, the functions the machine loses, the media it releases |
| **Restart** | How a change is applied: the machine is created again with the new slot set (the old machine's state is lost) |

## Seeing the slots

`slots` (CLI), `GET /emulator/{id}/slots` (WebAPI), `inspect_state` aspect `slots` (MCP), `slots_state()` (Lua,
Python) show the board, its buses, every slot with its card and the built-in devices. A Pentagon with the shipped
configuration:

```
PENTAGON (Pentagon 128 (1991): no expansion connector, ZX-bus retrofitted), cards from [SLOTS]
buses:
  ay-socket  ay-socket, 1 slot(s), arbitration None
  zxbus  zxbus, 0 slot(s), arbitration CardWins - the board has no connector for this bus: its cards are bolted on
slots:
  ay-socket = tsfm  fit real, active
  zxbus.1 = neogs [ram=2m]  fit real, active
  zxbus.2 = moonsound [jp1=open]  fit real, active
  zxbus.3 = soundrive [mode=both]  fit real, active
built-in devices:
  ay (YM2149 / AY-3-8910): replaced by tsfm
  beta128 (Beta-128 (VG93)): active
  kempston-joystick (Kempston joystick): active
```

`slots catalog` lists every card with its options and how it would fit this machine now: where the planner would put
it, the fit, and `fits`, `needs-replace` (it would remove a card; the list says which) or `refused` (the machine
cannot take it at all, with the reason).

## Changing a card

| Verb | CLI | What it does |
|---|---|---|
| plug | `slots plug <slot> <card> [opt=val ...]` | puts a card into a slot (`zxbus.next`, `ay-socket`, `auto` = where the planner puts it) |
| remove | `slots remove <slot>` | takes the card out |
| set | `slots set <slot> opt=val ...` | changes the card's options (merged over the ones it has) |
| gs | `slots gs <gs\|gs-lw\|neogs>` | replaces the General Sound card with another personality |

Every change is **planned first**. Worked example: on that Pentagon, plugging the ZX-MultiSound:

```
slots plug zxbus.next multisound --dry-run
refused: needs replaceIfIncompatible: removes `zxbus.1` `neogs`: shares `gs`; ...
  plug multisound -> zxbus.4: needs replaceIfIncompatible, fit real
  removes zxbus.1 = neogs [ram=2m] (D1): shares `gs`
  removes zxbus.3 = soundrive [mode=both] (D1): shares `soundrive`
  removes ay-socket = tsfm (D3): pointless pair: `zxbus.4` would shadow it (`#FFFD`, `#BFFD`); the socket returns to the machine's chip
  shadows the built-in ay (#FFFD #BFFD)
  the machine loses: covox-fb
  releases sd.ngs of zxbus.1 = neogs
```

The MultiSound carries a TurboSound FM, a General Sound and a SounDrive of its own, so the TSFM, the NeoGS and the
SounDrive card would have nothing to do: the plan removes them, and says that the `#FB` Covox the SounDrive card also
answered goes with it. Automation refuses such a change unless the request says so (`--replace`,
`replaceIfIncompatible: true`; owner decision Q1). The Qt window asks for a confirmation instead and, after the
change, names the cards it removed with an **Undo** button. `--dry-run` (`dryRun`) only shows the plan.

**Options** of a card: `name=value`, a set as a comma list. The MultiSound: `dip=ym,saa,gs,sd` (which of its parts
are switched on; default all), `gsRam=1m|2m`, `ctrlMask=pro|classic` (`classic` is an unofficial firmware patch).
The NeoGS: `ram=2m|4m`. The classic GS: `ram=128k..2m`, `rom=1.04|1.05`. SounDrive: `mode=1|2|both`. `slots catalog`
lists them all.

**The restart.** An allowed change restarts the machine with the new slot set (owner decision Q6): a new emulator
instance with a new id (the selection and the Qt window follow it), the machine state is lost, the media follow into
the slots with the same id on the new machine with their unsaved writes. A removed card's medium with unsaved writes
(the NeoGS SD card `sd.ngs`) refuses the change until the request says what to do with them: `--media save` or
`--media discard`. The reply names the new id (`restart.emulatorId`) and where the media went.

**Refused** without changing anything: while a TTD recording runs (the recording's devices are fixed); when the
machine cannot take the card (a fixed built-in uses its ports, the bus lacks a signal and no adapter connects it);
when the new set would not create a machine (two cards in conflict).

## The General Sound personality

The General Sound card comes in three personalities: `gs` (the classic card with its own Z80), `gs-lw` (a
lightweight built-in player, no firmware needed) and `neogs`. Switching between them (`slots gs neogs`, `gs
switch_personality ngs`, WebAPI `POST /control/audio/gs` `switch_personality`, the Qt audio settings) replaces the card
in its slot and restarts the machine like any slot change (owner decision Q10). Only the `gs_lightweight` feature still
swaps the card while the machine runs.

## The network cards

The ZX-bus network cards (`zxnetusb`, `zx-wifi`) are slots like any card, and the network settings' `card` value
changes them the same way (owner decision Q11): `network set card=zxnetusb hosts=a.test=10.0.2.7`, WebAPI `POST
/network/config`, MCP `emulator_manage` `network_configure`, Lua / Python `network_configure`, or the ZXNETUSB / ZX-WiFi
boxes of the Qt Network window. A card that comes or goes restarts the machine with the new slot set (one restart also
for "ZX-WiFi out, ZXNETUSB in"), and the other settings of the same request are applied to the restarted machine.
The flags and the reply are the slot change's (`--replace`, `--dry-run`, `replaceIfIncompatible`, the plan, the
restart); the reply also has `network` (`cardChange`, `cardsBefore`, `cards`, `settingsApplied`, `note`).

Example: a Pentagon without cards, `network set card=zxnetusb,zxwifi host_access=off` - the reply plans `plug zxnetusb
-> zxbus.1` and `plug zx-wifi -> zxbus.2`, the machine restarts, and the restarted machine has both cards with host
access off. A ZX-WiFi on a ZX-Evo or TS-Conf is refused (the board's own serial port answers `#xxEF`) and nothing
changes; a ZX-bus card behind the ATM Turbo 2+ CPU-socket adapter is an unrealistic fit that needs `--replace`.

Settings without a card change (`hosts`, `com_port`, `zx_wifi`, ...) apply to the running machine, without a restart
(status `accepted`). The ATM2IOESP card sits on the ATM Turbo 2+ INTERNAL connector, not on a bus slot: it still
changes in place. Every restart (a network card change, a plug, a remove, an Undo) keeps the network settings the
machine runs with, also those changed earlier at run time: they are configuration, not machine state. The keys of the
request itself win over them, and the slot set decides which ZX-bus network cards are fitted. Example: `network set
hosts=a.test=10.0.2.77`, then `slots plug zxbus.next gs`: the restarted machine still resolves `a.test`.

The runtime feature `network` is a power switch: off, the fitted ZX-bus network cards stay in their slots without
power, and the slot report shows their state as `feature network off`.

## Model switch

A model switch (`model ATM3`, WebAPI `POST /emulator/{id}/model`, MCP `switch_model`, the Machine menu) carries the
cards: each card keeps its slot where the new machine has that bus, moves behind an adapter where it needs one, and
the new machine's own configured cards fill the slots left free (owner decision Q9). A card the new machine cannot
take is dropped and listed with the reason:

```
Slots: zxbus.1 = neogs not carried to ZX-Spectrum 48k: `neogs` is a `zxbus` card; `edge` is `sinclair-edge`; behind
`zxbus-to-sinclair-edge` it still lacks /CSROM (fit `unrealistic` with the override)
```

Removed cards are not remembered: a card you took out comes back if the new machine's configuration fits it.

## Creating a machine with cards

WebAPI / MCP `create` takes the slot set in the `[SLOTS]` key form, replacing the configuration's:

```json
{"model": "PENTAGON", "slots": {"ay-socket": "none", "zxbus.1": "multisound", "zxbus.1.dip": ["ym", "saa"]}}
```

## In the configuration file

The machine's `unreal.ini` names its cards in `[SLOTS]`:

```ini
[SLOTS]
ay-socket = tsfm              ; ay | ts | tsfm | none
zxbus.1 = neogs
zxbus.1.ram = 4m              ; a card option
zxbus.2 = soundrive
zxbus.2.mode = both
edge.1.fit = unrealistic      ; the card works although the bus lacks a signal it needs
builtin.covox = off           ; a switchable built-in device
```

Entries that conflict (two cards with one job) refuse the machine with every pair and its rule (owner decision Q8).
The older keys still work and are translated into slots when the machine is created (a deprecation line in the log):

| Old key | Becomes |
|---|---|
| `[SOUND] TurboSound=FM` / `TS` / `AY` / `None` | `ay-socket = tsfm` / `ts` / `ay` / `none` |
| `[SOUND] GSType=NGS` / `Z80` / `LW` | a `neogs` / `gs` / `gs-lw` card in the next ZX-bus slot (`GSRamSize`, `[NGS] RamSize` become its `ram`) |
| `[SOUND] MoonSound=1` | a `moonsound` card |
| `[SOUND] SD=1` / `CovoxFB=1` | a `soundrive` / `covox-fb` card, or the board's own Covox on a machine that has one |
| `[NETWORK] Card=ZXNETUSB` / `ZXWIFI` | a `zxnetusb` / `zx-wifi` card |

A configuration with `[SLOTS]` ignores the old keys.

## On each surface

| Surface | Read | Change |
|---|---|---|
| CLI | `slots`, `slots catalog`, `slots matrix [table]` | `slots plug / remove / set / gs` with `--replace`, `--dry-run`, `--media`, `--json` ([command-interface.md section 14](../emulator/design/control-interfaces/command-interface.md#14-zx-bus-slots)) |
| WebAPI | `GET /emulator/{id}/slots`, `/slots/catalog`, `/slots/matrix` | `POST /slots/{slot}/plug`, `/remove`, `PUT /slots/{slot}/options`; refusal = 409 with the plan ([webapi-interface.md](../emulator/design/control-interfaces/webapi-interface.md#zx-bus-slots), OpenAPI tag `Slots`) |
| MCP | `inspect_state` aspect `slots`; `emulator_manage` `slots_catalog`, `slots_matrix` | `emulator_manage` `slots_plug`, `slots_remove`, `slots_set` ([mcp/README.md](mcp/README.md)) |
| Lua | `slots_state()`, `slots_catalog()`, `slots_matrix()` | `slots_plug()`, `slots_remove()`, `slots_set()`, `slots_gs()` ([lua-interface.md](../emulator/design/control-interfaces/lua-interface.md#zx-bus-slots)) |
| Python | `unreal.slots_state()`, `slots_catalog()`, `slots_matrix()` | `unreal.slots_plug()`, `slots_remove()`, `slots_set()`, `slots_gs()` ([python-interface.md](../emulator/design/control-interfaces/python-interface.md#zx-bus-slots)) |
| Qt | Machine > Slots | the same window: plug, remove, options, the plan preview, Undo |
| Network settings | `network`, `/state/network`, `network_state()` | `card=` of `network set`, `POST /network/config`, MCP `network_configure`, Lua / Python `network_configure`, the Network window's card boxes: a slot change (see [The network cards](#the-network-cards)) |

Every surface returns the same reply: `ok`, `status` (`applied`, `accepted` - network settings applied in place -,
`dry-run`, `refused`, `recording`, `no-machine`, `failed`, `bad-request`), `message`, the `plan` (`removed[]` with each card's `undo`, `shadowed[]`, `lostFunctions[]`,
`media[]`, `lines[]`), `restart` (`restarted`, `previousEmulatorId`, `emulatorId`, `started`) and `media`.
