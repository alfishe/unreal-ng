# New platform guidelines

Rules for adding a machine (a model, a board variant, a clone) to the emulator. The words MUST and MUST NOT are requirements: a platform that
breaks one is not finished, whatever else works. Review a new platform against section 6 before it lands.

## 1. The principle: one manager per concern, no ad hoc integration

Every concern that has a manager goes through it:

| Concern | Manager | Reference |
|---|---|---|
| Media (floppies, tapes, SD / MMC cards, IDE disks, CD-ROM, flash) | `MediaManager` and `IMediaSlot` (`core/src/emulator/media`) | [docs/features/media.md](../features/media.md), [.recipe/media/use-media-slots.md](../../.recipe/media/use-media-slots.md) |
| Buses, slots, add-on cards (GS, NeoGS, TurboSound FM, MoonSound, Covox, network cards, ...) | the slot manager (`core/src/emulator/slots`: buses, slots, cards, port claims) | [docs/features/slots.md](../features/slots.md), [.recipe/machines/slots.md](../../.recipe/machines/slots.md) |
| Keyboard, mouse, joystick | the keyboard / mouse / joystick managers | [docs/features/keyboard.md](../features/keyboard.md) |
| Time travel (TTD) | the TTD serializers of each device | [docs/emulator/design/debugger/time-travel-debug](../emulator/design/debugger/time-travel-debug) |

The manager is the product. The WebAPI, MCP, the CLI, Lua, Python, the Qt windows, drag and drop, the command line, snapshots, TTD and the
session saves reach a medium, a card or a bus **only** through it. A device wired some other way is invisible to all of them, cannot be
swapped, saved, inspected or recorded, and has to be rewritten later. (The ZX Spectrum Next opened its SD card straight from an `.ini` key
and had no media slot: `media insert sd` answered `unknown-slot`.)

## 2. Media: every medium bearer is a slot

1. **A device that holds a medium MUST register an `IMediaSlot`** with `context->pMediaManager->RegisterSlot()` in its constructor and
   `UnregisterSlot()` in its destructor. One slot per socket, drive or unit. A card socket that exists on the board exists as a slot,
   empty or not.
2. **Slot ids** are `<kind>.<controller>[<n>][.<unit>]`, tied to the controller, never to the machine: `fdd.a`, `sd.zc`, `sd.ngs`, `sd.next0`,
   `ide0.master`, `tape`.
3. **The `SlotDescriptor` MUST be complete**: `kind`, `label`, `removable`, `swapDelayMs`, `acceptsFolder`, `defaultAccess`, `defaultFs`,
   `fsCompatibility` (the FAT flavours the guest's driver really reads, from its source or from a test: not a guess), `folderMbr`
   (partition table or sector-0 volume), `tags`, `aliases` (the primary SD slot has the alias `sd`, the primary hard disk `hd`).
4. **The device MUST NOT open, read or write a medium file itself.** It attaches the manager's medium in `Attach(Medium&)`, drops every
   pointer in `Detach()`, reports `IsBusy()` for a transfer in flight, and reports the guest's writes with `NoteWrite(slotId)`.
5. **Configuration keys** (`[NEXT] SdCard=`, `[HDD] Image=`, command-line media) only name an initial medium. The decoder inserts it with
   `MediaManager::Insert` after the slot is registered, at the power-on and at every reset while the slot is empty. They MUST NOT be a second way to mount a medium:
   a medium the user inserted stays, and the configured one only fills an empty slot.
6. **Helpers for tests and tools** (`InsertSdCard(...)`, `LoadDisk(...)`) MUST call the manager, not the device, so that every test exercises the
   same path as the WebAPI.
7. **A card is the type the board's controller answers as** (an SDHC card on the Next whatever its image size). Set it in `Attach`, not in the
   caller.
8. **Per-model rules go in the descriptor, not in the media code**: a model must never add a branch to the manager or to the format code.

## 3. Buses, slots and cards: every expansion is a slot

1. **A machine declares its buses and slots** (`zxbus`, `edge`, `iobus`, `profi-bus`, `isa`, `cpu-socket`, `ay-socket`, the Next's expansion
   connector) in its board description. A new bus or slot kind is a new row in the slot documentation and the compatibility matrix.
2. **A device that is a card on a real board MUST be a card** of the slot manager: registered by id, with its function, its fit (`real`,
   `adapter`, `unrealistic`), the built-in devices it shadows, and the media slots it brings (the NeoGS brings `sd.ngs`).
3. **Port ownership goes through the port claim table.** A card claims its ports there (decode mask, read / write, priority); a card MUST NOT
   hook the port decoder with a private `if (port == ...)`. Conflicts are the manager's to find (rule D1: two cards with the same function do not
   fit), not a run-time surprise.
4. **A built-in device** (the board's own AY, a Covox, the disk interface) that a card can replace is declared as built-in, so that "shadow"
   and "switch off" are the manager's plan, not a flag in the decoder.
5. **Interrupt, wait and bus-arbitration signals of a card** go through the machine's interrupt source and the CPU's wait-state API, never
   through a direct call into the CPU from a device.

## 4. Every plane sees the new thing, for free

Because media and cards go through the managers, the WebAPI (REST and WebSocket), MCP, CLI, Lua, Python and the Qt windows already have verbs for them. A new platform
MUST NOT add a private endpoint, a private CLI command or a private MCP action for a medium or a card. If a verb is missing, it is added to the
manager and to **every** plane (the rule of `docs/emulator/design/control-interfaces/command-interface.md`; the planes, the
documentation chain and the WebSocket topics: [automation-planes-guidelines.md](automation-planes-guidelines.md)).

New state a platform wants to expose (registers, DMA, layers) is a report: one `DeviceState` function, thin adapters per plane, golden
output ([docs/inprogress/2026-10-07-zx-next/design-automation-coverage.md](../inprogress/2026-10-07-zx-next/design-automation-coverage.md)).

## 5. Time travel and sessions

A device that holds state MUST provide a TTD serializer (`CreateTTDSerializers`), and a medium's session writes follow the manager's
`save` / `export` / `discard` semantics. A device that keeps its own copy of the guest's writes loses them in a TTD seek and in a swap.

## 6. Review checklist (a new platform does not land with a "no")

| # | Check | How to see it |
|---|---|---|
| 1 | Every socket, drive and unit is a media slot | `GET /emulator/{id}/media` lists it; `media insert <alias>` works |
| 2 | No file is opened by a device | `grep` for `open(`, `fopen`, `ifstream` in the new `ports/models` and `io/` code |
| 3 | The `.ini` keys insert through the manager | `Insert` is called after `RegisterSlot`; an eject survives a reset |
| 4 | Every card is a card of the slot manager; ports are claimed | `GET /emulator/{id}/slots`; no `if (port == ...)` for a card in the decoder |
| 5 | The descriptor's filesystem rules come from the guest's driver | a test mounts a FAT16 and a FAT32 card and the guest reads both (or one is refused) |
| 6 | Tests: the slots exist, a folder inserts, a swap works, an eject empties, the alias resolves, a write reaches `NoteWrite` | the machine's `*_test.cpp` |
| 7 | TTD serializers for every stateful device | `ttd/start` + `ttd/seek` round trip |
| 8 | The recipe and the feature docs name the new slots and cards | `.recipe/machines/<machine>.md`, `docs/features/media.md`, `docs/features/slots.md` |
| 9 | No new endpoint, CLI command or MCP action that bypasses a manager | review of the automation diff |

## 7. Examples to copy

- Media slot with a card: `PortDecoder_TSConf::SdSlot` and `PortDecoder_ATM3` (`sd.zc`), `PortDecoder_Next::SdSlot` (`sd.next0`, `sd.next1`).
- Floppy: `FloppyDriveSlot` (`core/src/emulator/io/fdc/floppydriveslot.h`).
- Cards and buses: `core/src/emulator/slots/cards`, the Sprinter's slots ([docs/features/sprinter-slots.md](../features/sprinter-slots.md)).
