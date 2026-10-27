# D23 completed: devices are keys, not v1 ids

Owner decision 2026-10-12: the one-byte `PeripheralId` ran out at 63 (`NextSystemArea`), so D23
([engine-decisions.md](engine-decisions.md) #23) is carried through now, before v1 is deleted. Worktree
`scratch/wt-ttd-devicekey`; it lands only after the tests pass on every platform.

## 1. Why now: what id 64 would break

Not only a display mask. On master `235552be7`:

| Where | Limit | A device with id 64 |
|---|---|---|
| `TTDPeripheralRegistry::_lastStates`, `_lastStateBytes` (`std::array<..., 64>`) | 64 | `CaptureStates` lists it, `LastCaptureState(64)` is empty: **the engine records no state for it** and restores it stale |
| `TTDSessionFacts::peripheralMask`, `TTDFileInfo`, v1 header | 64 | dropped from file-info and `peripheral_mask` (`id < 64`) |
| `notRecordedMask` (registry, `SlotManager::TtdDeviceSet`, facts) | 64 | `uint64_t(1) << id` with no guard: undefined behaviour |
| `PeripheralId : uint8_t`, `PeripheralBlobHeader::peripheralId`, `_deviceRegionOf[256]`, `TTDDeviceStateInput::id`, `DeviceState(index, uint8_t)`, `legacy_id` u1 in the session file | 256 | the next wall |
| one state per id in the registry | — | two instances of one device need two ids (the 16550 took 24, 37, 39, 46-49) |

## 2. The rule after this change

- **Identity is `TTDDeviceKey` = {`TTDDeviceType` (u16), instance name}.** Registry, engine, controller, session
  facts, file-info, slot checks, machine state transfer and ZX-Poly name devices by key (or by type, where a
  machine has one device of a kind).
- **`PeripheralId` is v1's blob id, frozen** (at 63 when this was written; at 65 after the merge of section 7). Values 0-63 stay what they are (v1 files and checkpoints use them);
  no new value is added. `PeripheralId::None = 0xFF` marks a device v1 cannot record. Only v1
  (`TimeTravelManager`, its file format `ttddumpformat.h`, its checkpoint blobs) and the v1 file reader
  (`ttdv1feeder`, `ttdfileinfo` for v1 files) read it.
- **A new device** declares `TTDDeviceType` only: it overrides `TTDType()` (new virtual; default: the type of
  its v1 id) and leaves `TTDPeripheralId()` at `None`. A second instance of an existing kind takes no number at
  all, only an instance name (`next.uart0`).
- **v1 records only devices with a v1 id.** A machine with a device without one (or two devices sharing one)
  cannot record on v1: `TimeTravelManager` refuses with the device named. The engine is the default since
  2026-10-05, v1 goes after 2026-10-22.

## 3. Changes

### 3.1 Device interface (`ttdserializable.h`)
- `PeripheralId::None = 0xFF`; `Count` stays (one past the last v1 id) for v1's tables.
- `virtual TTDDeviceType TTDType() const` (default `TTDDeviceType(TTDPeripheralId())`); the default
  `TTDDescribe` takes `type` from it. The 58 existing serializers do not change.
- `TTDDeviceTypeName(TTDDeviceType)` (for file-info) next to `PeripheralIdName`.

### 3.2 Registry (`ttdperipheralregistry`)
- Entries in registration order: {key, legacy id, device, instance given}. `Register(device, instance = {})`:
  the key comes from the device (`TTDDescribe`), the instance argument overrides its name. A key registered twice
  is refused and reported by `CheckDeviceTable` (as today for a duplicate id).
- Lookups: `Find(key)`, `FindType(type)` (the first device of a kind), `IsRegistered(type)`, `Unregister(key)`,
  `InstanceOf(type)`.
- Not recorded: `MarkNotRecorded(key)`, `NotRecorded()` → keys.
- Engine capture: states per entry (`LastCaptureState(entry)`), no fixed table.
- v1: `CaptureAll` / `RestoreAll` keep their `uint8_t`-keyed blob maps over entries that have a v1 id;
  `V1Recordable(why)` says whether every device has a distinct one.
- Divergence hash: unchanged for devices with a v1 id (rotation by the id); a device without one rotates by its
  type.
- Region sources: `ITTDRegionSource::TTDStateWithoutRegions(state)` loses its id out-parameter; the registry
  pairs a source with the registered device that is the same object.

### 3.3 Engine
- `TTDDeviceStateInput::id` → `device`: the index in the session's device table. v1-fed input
  (`deviceBlobs`, `uint8_t` keyed) is mapped by the table's `legacyId`.
- `_deviceRegionOf[256]` → the table index plus the first device region; `DeviceState(index, key)`.
- Device-state regions' `ownerType` is the device's type (equal to its v1 id for every existing device, so files
  do not change).
- Session file: `legacy_id` stays u1, `0xFF` = none. No format change.

### 3.4 Controller, facts, file-info, API
- `TTDSessionFacts` version 2: no `peripheralMask` (the session's device table lists the devices); `notRecorded`
  as keys. Version 1 still reads (mask bits → types).
- `TTDRecordedMachine`: `devices` and `notRecorded` as keys; `peripherals` / `not_recorded` names from them;
  General Sound / TurboSound from types. `peripheral_mask` was to leave the WebAPI / MCP answer; it stays (section 7).
- `UpdatePeripheral` (the General Sound switch) takes types.

### 3.5 Slots, cards, port decoders, transfer, ZX-Poly
- `SlotManager::TtdDeviceSet`: keys and not-recorded types; the slot table names its devices by type.
  `TtdSessionMatches` and the cards' checks take device states by key (`TTDDeviceStates` =
  `std::map<TTDDeviceKey, std::vector<uint8_t>>`, raw), not v1 blobs; a v1 file's blobs are converted once.
- `CardTtdDevice` loses its id (the device describes itself).
- `PortDecoder::GetTTDModelStateIds` → `GetTTDModelStateTypes` (`TTDDeviceType`).
- `MachineStateTransfer` walks the source registry's entries and finds the target's by key.
- ZX-Poly's device collection is keyed by type.

## 4. Compatibility

| File | Effect |
|---|---|
| Engine session files (schema 2) | unchanged bytes for every existing device; facts version 2 written, version 1 read |
| v1 files | unchanged; read by the v1 reader as before |
| WebAPI / MCP `file-info` / status | unchanged: `peripheral_mask` (+ `peripheral_mask_words`) name the devices that have a v1 id, `peripherals` every device (section 7) |

## 5. Tests
- Every existing TTD test, on the engine and v1.
- New: a test device with a type ≥ 64 and no v1 id records and restores on the engine, shows in file-info, is
  refused by v1 with its name; two instances of one type record side by side; a v1 facts stream still loads.
- Platforms before landing: macOS (clang), Linux (gcc, docker), Windows.

## 6. As built (2026-10-13)

Beyond §3, found on the way:

- **Region ids per instance.** Device memory regions have one fixed `TTDRegionId` per kind (`MultiSoundGsRam` = 17):
  two cards of one kind listed it twice and `BeginSession` refused. `TTDPeripheralRegistry::CollectRegions` gathers
  every source's regions, names each by its device's instance (`ownerInstance`) and gives a kind's region met again
  `InstanceRegionFirst` (0x4000) + n x 0x100 + the kind's id. The first instance keeps its id: the corpus loads
  unchanged. Every place that listed regions (controller, v1, its shadow engine) goes through it.
- **The registry entry -> engine table index map follows the bound device.** A session loaded from a file has the
  file's device table; its instance names may differ from the live ones (a recording from before the socket's board
  names). The controller maps each registry entry to the table entry bound to the same live device (`BindLive`), else
  by key, at every capture. Mapping by key alone dropped the TurboSound's state from the checkpoints of a resumed
  Sprinter session (`TimeTravelControllerCorpus_Test`).
- **Tool edits.** An edit record names a device by its index in the session's device table (kind 8); kind 3 (by v1
  id) still applies.
- **Live snapshot.** `LiveStateSnapshot` keeps every device's state by registry entry (`SaveLive` / `LoadLive`), not
  as v1 blobs, which would have left out a device without a v1 id.
- **Region sources pair by object.** `TTDStateWithoutRegions(state)` lost its id out-parameter; the registry pairs a
  source with the registered device that is the same object.
- **Machine state transfer** walks the source registry's entries (ascending v1 id, then the rest) and finds the
  target's device by key, else the one device of that type both machines have (a card in another slot).
- **Binding a loaded session** (`BindLive`): by key, else by v1 id (a session fed from a v1 file names its devices
  `id<N>`).

Tests added: the registry (a device past the v1 ids, two instances of one type, instance naming, not recorded,
snapshot, region pairing, v1 blobs by live key); the engine (two type-64 instances through capture, file, load and
restore); file-info names; facts versions 1 and 2; two MultiSound cards recorded and restored side by side.

Not built by default: the POC 011 benchmarks (`-DBUILD_POC=ON`) had their registrations updated, not compiled.

## 7. Merged with the wide mask (2026-10-13)

While this branch was built, master took another answer to the same limit (`c616568a6`): `ttd::PeripheralMask` (any
u8 id), v1's file schema 3 (the header's masks as word lists), `peripheral_mask_words` on the automation surfaces, and
two new ids, `MachineSerialPeer2` = 64 and `JoystickSerialPeer` = 65 (the Next's second UART peer and the joystick
connector's). Both stand; the merge:

- **The freeze is at 65**, not 63: ids 64 and 65 are v1 ids and device types as landed (sessions may hold them). A
  new device takes a type from 66 on and no v1 id. (By D23 the two peers would have been instances of
  `MachineSerialPeer`; they keep their own types, as the files say.)
- **`PeripheralMask` is v1's**: the v1 file header (schema 3, schema 1 read), `TTDPeripheralRegistry::V1MaskOf` /
  `KeysOfV1Mask`, `TTDRecordedMachine::V1Mask()`. Nothing between the live devices and the engine is keyed by it.
- **Facts are version 3** (keys). Version 2 (master's word-list masks) and version 1 (u64 masks) still read: the
  recorded mask is dropped (the device table lists the devices), the not-recorded one becomes keys.
- **`peripheral_mask` and `peripheral_mask_words` stay** on the automation surfaces (section 3.4 above said the
  first would go): the set of v1 ids of the recorded devices, as master documents them. A device without a v1 id is
  only in `peripherals`.
