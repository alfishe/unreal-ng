# Recipe: ZX-bus Slots (fit a card, plan, apply, model switch)

The cards on a machine's buses - General Sound / NeoGS, TurboSound FM in the AY socket, MoonSound, Covox /
SounDrive, the network cards, the ZX-MultiSound - are **slot cards**: each sits in a slot of a bus (`zxbus.1`,
`edge.1`, `ay-socket`), the emulator checks that they fit together, and a change is planned before it is made. Use
this recipe to see what a machine has, to fit a card for a test, and to read what a change would remove. Verified
2026-10-05 against branch `zx-bus-slots` (MCP, WebAPI, CLI, Lua). User guide:
[docs/features/slots.md](../../docs/features/slots.md).

Ground truth: [slotcontrol.h](../../core/src/emulator/slots/slotcontrol.h) (the one layer every surface calls),
[slotmanager.h](../../core/src/emulator/slots/slotmanager.h) (plan, carry), [slotchange.h](../../core/src/emulator/slots/slotchange.h)
(apply = restart), the reference data in [refdata/](../../core/src/emulator/slots/refdata/); design
[docs/inprogress/2026-10-03-zx-bus-slots/](../../docs/inprogress/2026-10-03-zx-bus-slots/architecture.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side
> pipelines or when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)); the CLI has the
> same verbs ([CLI](#cli)).

**Every change restarts the machine** (owner decision Q6): a new emulator instance with a **new id** (the selection
follows it; MCP `target: "auto"` finds it), the machine state is lost, disks, tapes and cards' media follow into the
slots with the same id. Read the new id from the reply (`restart.emulatorId`) before the next call. A change that
removes a card is **refused with the plan** unless the request says `replace_if_incompatible` (Q1).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PENTAGON"}
inspect_state   {"aspects":["slots"]}
#   [slots] PENTAGON (Pentagon 128 (1991): no expansion connector, ZX-bus retrofitted), cards from [SLOTS]
#     ay-socket = tsfm fit real, active
#     zxbus.1 = neogs [ram=2m] fit real, active
#     zxbus.2 = moonsound [jp1=open] fit real, active
#     zxbus.3 = soundrive [mode=both] fit real, active
#     built-in ay: replaced by tsfm
#     ...

emulator_manage {"action":"slots_catalog"}            # every card: fits / needs-replace / refused here, fit, slot

emulator_manage {"action":"slots_catalog"}
#   Cards for PENTAGON:
#   - multisound (ZX-MultiSound rev.A2): needs-replace in zxbus.4, fit real
#   - zxnetusb (ZXNETUSB): fits in zxbus.4, fit real
#   ...

# Plan only: what would the ZX-MultiSound remove?
emulator_manage {"action":"slots_plug","slot":"zxbus.next","card":"multisound","dry_run":true}
#   slots: refused: needs replaceIfIncompatible: removes `zxbus.1` `neogs`: shares `gs`; ...
#     plug multisound -> zxbus.4: needs replaceIfIncompatible, fit real
#     removes zxbus.1 = neogs [ram=2m] (D1): shares `gs`
#     removes zxbus.3 = soundrive [mode=both] (D1): shares `soundrive`
#     removes ay-socket = tsfm (D3): pointless pair: `zxbus.4` would shadow it (`#FFFD`, `#BFFD`); ...
#     shadows the built-in ay (#FFFD #BFFD)
#     the machine loses: covox-fb
#     releases sd.ngs of zxbus.1 = neogs

# Apply: the machine restarts with the card (options as "name=value", a set as a comma list)
emulator_manage {"action":"slots_plug","slot":"zxbus.next","card":"multisound",
                 "options":"dip=ym,saa,gs,sd gsRam=2m","replace_if_incompatible":true}
#   slots: applied
#     plug multisound -> zxbus.4: allowed, fit real
#     ...
#   restarted: emulator 8618a033-... -> ed55c5ad-... (running)
inspect_state   {"aspects":["slots"]}
#     zxbus.2 = moonsound [jp1=open] fit real, active
#     zxbus.4 = multisound [dip=ym,saa,gs,sd gsRam=2m ctrlMask=pro] fit real, active
#     built-in ay: shadowed by zxbus.4

emulator_manage {"action":"slots_set","slot":"zxbus.4","options":"ctrlMask=classic"}   # options of a fitted card
#   slots: applied ... restarted: emulator ed55c5ad-... -> 3444c8d8-... (running)
emulator_manage {"action":"slots_remove","slot":"zxbus.4"}
emulator_manage {"action":"slots_matrix","table":"card-x-card"}                       # the compatibility table

# The General Sound personality is the same kind of change (Q10): the card in the GS slot replaced, a restart
emulator_manage {"action":"gs_switch_personality","personality":"ngs"}
#   slots: applied ... plug neogs -> zxbus.1: allowed ... removes zxbus.1 = gs [ram=512k rom=1.05] (D1): replaced in its slot

# A machine created with its slot set (the [SLOTS] key form); the report's source says "create slots zxbus.1"
emulator_manage {"action":"create","model":"PENTAGON","slots":{"ay-socket":"tsfm","zxbus.1":"gs","zxbus.1.ram":"512k"}}

# Model switch: the cards go along where the new machine takes them; the rest is named
emulator_manage {"action":"switch_model","model":"48K"}
#   Switched <id> to 48K: new emulator <new id>
#     Slots: zxbus.1 = neogs not carried to ZX-Spectrum 48k: `neogs` is a `zxbus` card; `edge` is `sinclair-edge`;
#            behind `zxbus-to-sinclair-edge` it still lacks /CSROM (fit `unrealistic` with the override)
#     Slots: ay-socket = tsfm carried to ZX-Spectrum 48k
```

The slot ids of a machine: `inspect_state slots` lists its buses; `<bus>.next` means the next free slot of that bus,
`auto` (or no `slot`) lets the planner choose. `slots_catalog` says for each card whether it `fits`, `needs-replace`
(and which cards it would remove) or is `refused` here, and with which fit: `real`, `adapter` (behind a bus adapter),
`unrealistic` (the bus lacks a signal the card needs; allowed with the replace flag, and every report says so).

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' -d '{"model":"PENTAGON"}' | jq -r .id)

curl -s "$BASE/emulator/$EMU_ID/slots" | jq '.slots[] | {slot, card, options, fit, state}'
curl -s "$BASE/emulator/$EMU_ID/slots/catalog" | jq '.cards[] | {id, here: .thisMachine.outcome, slot: .thisMachine.slot}'

# Plan (409 with the plan as the body, nothing changes)
curl -s -X POST "$BASE/emulator/$EMU_ID/slots/zxbus.next/plug" -H 'Content-Type: application/json' \
     -d '{"card":"multisound"}' | jq '{status, message, removed: [.plan.removed[] | {slot, card, undo}]}'

# Apply: read the new id
EMU_ID=$(curl -s -X POST "$BASE/emulator/$EMU_ID/slots/zxbus.next/plug" -H 'Content-Type: application/json' \
     -d '{"card":"multisound","options":{"gsRam":"2m"},"replaceIfIncompatible":true}' | jq -r .restart.emulatorId)

curl -s -X PUT "$BASE/emulator/$EMU_ID/slots/zxbus.4/options" -H 'Content-Type: application/json' \
     -d '{"options":"ctrlMask=classic"}' | jq '{status, emulatorId}'
curl -s "$BASE/emulator/$EMU_ID/slots/matrix?table=cards" | jq -r '.tables[0].markdown'
```

The reply of every change: `status` (`applied`, `dry-run`, `refused`, `recording`, `no-machine`, `failed`,
`bad-request`), `message`, `plan` (`removed[]` with each card's `options` and `undo` - the plug that puts it back -,
`shadowed[]`, `lostFunctions[]`, `media[]`, `resultingSlots[]`, `slotsSection[]`, `lines[]`), `restart`
(`restarted`, `previousEmulatorId`, `emulatorId`, `started`) and `media` (`attached`, `detached`, `closed`).

## CLI

```text
slots                                   buses, slots, built-ins
slots catalog
slots plug zxbus.next multisound gsRam=2m --dry-run
slots plug zxbus.next multisound gsRam=2m --replace
slots set zxbus.4 ctrlMask=classic
slots remove zxbus.4 --media discard
slots gs neogs
slots --json                            the WebAPI's object
```

## Fitting a card for a test without a restart

Create the machine with the card instead of changing a running one: WebAPI / MCP `create` with `"slots"` (the
`[SLOTS]` key form, replacing the configuration's set). Conflicting entries refuse the machine with every pair and its
rule (400; owner decision Q8):

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model":"PENTAGON","slots":{"zxbus.1":"multisound","zxbus.1.dip":["ym","saa"]}}' | jq '{id, model}'
```

## Pitfalls

- **The id changes.** Every applied change (and `gs_switch_personality`) is a new emulator; calls with the old id
  answer 404. Take `restart.emulatorId` from the reply (MCP `target: "auto"` resolves the single instance).
- **Refused while TTD records** (`status: "recording"`: `Cannot change the slot set while TTD is recording session #1,
  started at frame 1: ...`): stop the recording first. A dry run says so too.
- **Conflicting cards refuse a new machine**: `create` with `{"zxbus.1":"gs","zxbus.2":"neogs"}` answers 400 `the
  [SLOTS] cards conflict, the machine is not created (Q8): zxbus.2 = neogs and zxbus.1 = gs: shares gs (D1: ...)`.
- **Unsaved media of a removed card** (the NeoGS SD card `sd.ngs` with writes): the change is refused until it says
  `mediaDisposition` `save` or `discard`.
- **ZX-Poly** machines share one configuration: slot changes are refused there.
- **The legacy INI keys** (`[SOUND] GSType`, `TurboSound`, `MoonSound`, `CovoxFB`, `SD`, `[NETWORK] Card`) still work
  in a configuration without `[SLOTS]`: they become slots at creation (`source` in the report names the key).
- **The network settings** (`network set card=...`, `POST /network/config {"card": ...}`, MCP `network_configure`, Lua /
  Python `network_configure`, the Network window's ZXNETUSB / ZX-WiFi boxes) change the ZX-bus network cards as a slot
  change applied by a restart (owner decision Q11): the same plan, flags and reply as `plug` / `remove` here, then the
  other settings of the request go to the restarted machine. Settings without a card change apply in place (status
  `accepted`, no restart). ATM2IOESP (the ATM Turbo 2+ INTERNAL connector, not a bus slot) still changes in place.
  Every slot restart keeps the running network settings (also those changed at run time); the request's keys win.
- **The runtime feature `network` off** is a power switch: a fitted `zxnetusb` / `zx-wifi` stays in the report with
  `state` = `feature network off`.
