# Phase 2 — Device state with versions: technical design

Status: **design, not implemented** (2026-10-02). Roadmap and checks: [README.md, Phase 2](README.md#phase-2--device-state-with-versions). Requirements: FR-2, FR-4, FR-7, FR-19, PR-10 ([requirements.md](requirements.md)). Decisions: D18, D23, D26, and D7 / D15 for how a restore result relates to positions and branches ([engine-decisions.md](engine-decisions.md)). Builds on [phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md) (engine skeleton, piece store, regions).

Code references are to master at `8ddaf708e`. Abbreviations:
- `TTM` = `core/src/debugger/ttd/timetravelmanager.cpp`, `TTM.h` = its header;
- `REG` = `core/src/debugger/ttd/ttdperipheralregistry.{h,cpp}`;
- `SER` = `core/src/debugger/ttd/ttdserializable.h`;
- `MP` = `core/src/debugger/ttd/ttdmachineperipherals.cpp`.

The engine is `ttd::TimeTravelEngine`. Parts only the engine has live in `core/src/debugger/ttd/engine/` ([engine-approach-and-naming.md](engine-approach-and-naming.md)). The serializers and the registry are shared with v1 and stay where they are.

## 1. Glossary

| Term | Meaning |
|---|---|
| Device | Anything with state outside the CPU, the chipset struct and memory regions: a sound chip, the disk controller, a paging latch set, a serial port |
| Device state | The bytes a device writes through `TTDSaveState`: registers, latches, counters. v1 calls the stored, wrapped form a *blob* |
| Device type id | A stable 16-bit number naming the kind of device (`SerialPort`, `MoonSound`). Never reused |
| Instance name | Names one device of a type in a machine, as a dotted path: `zifi.uart`, `isa2.uart0`. Two UARTs share a type id and differ by name |
| Layout version | Which arrangement of bytes a device's state uses. A device that adds a field gets a new layout version |
| Device table | The list of device instances in a session: type, name, layout version, size, firmware fingerprint, dependencies |
| Device set | Which devices the machine has. Fixed for a session (D38): changing it starts a new session |
| Version (of a device state) | One stored state of one device. A new version is stored only when the state changed |
| Same as previous | A checkpoint stores nothing for a device whose state did not change; it inherits the previous version |
| Changed ranges | A new version stored as the byte ranges that differ from the previous one: offset, length, new bytes |
| XOR fallback | A new version stored as the compressed XOR (bitwise difference) of the whole state, used when it is smaller than the ranges |
| Time field | A field that advances by itself as time passes: a T-state origin, a sample position, a card CPU's tick counter |
| Prediction | For a time field: "the previous value plus the previous step". A field that moves by the same step every frame matches its prediction and costs nothing |
| Firmware fingerprint | A hash of the ROM or firmware image a device runs (MoonSound wave ROM, ATM2 keyboard controller ROM). It tells whether a replay can be exact |
| Restore result | What a restore reports: exact, degraded (with a reason per device), not bit-exact, damaged (with the damaged range) |
| After-restore call | A call a device gets once every device and memory region is back, to rebuild what it derives from them |
| Frame boundary sync | Devices that run behind the CPU (card CPUs, sound renderers) catch up to the frame end before capture (FR-19) |

## 2. What changes, in one example

Pentagon 128 at the BASIC prompt with its default cards (NeoGS, MoonSound, TurboSound FM), one minute = 3,000 frames. Measured on the E6 one-minute session (`ttd-bench-v1-PENTAGON_idle.ttd`, recorded by [record-real-sessions.sh](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/common/record-real-sessions.sh)); the method is in §10.

| Device (v1 id) | State bytes | v1 per frame (wrapped, compressed) | Frames in which it changed | Changed bytes per change |
|---|---|---|---|---|
| BetaDisk (1) | 254 | 72 | 0 | — |
| Tape (2) | 71 | 34 | 0 | — |
| Covox (3) | 9 | 21 | 104 | 4 |
| TurboSound FM (4) | 2,008 | 155 | 3,000 | 56.5 |
| Kempston mouse (7) | 8 | 20 | 0 | — |
| MoonSound (10) | 4,366 | 333.5 | 3,000 | 28.4 |
| NeoGS (12) | 21,536 | 187.4 | 3,000 | 19.5 |
| IDE board (17) | 4,252 | 50 | 0 | — |
| Kempston joystick (23) | 2 | 14 | 0 | — |
| **All** | | **887 B = 2.66 MB per minute** | | |

| Per frame | v1 | After Phase 2 |
|---|---|---|
| Devices saved (`TTDSaveState`) | 9 | 9 (unchanged: the device code is shared) |
| zstd calls on device state | 9, every frame, whatever changed (REG.cpp:177) | 3–4, only on changed states, and only when the XOR beats the ranges |
| Unchanged devices (6 of 9) | stored whole again | **nothing**: same as previous |
| TSFM, MoonSound, NeoGS | stored whole again | the changed ranges or the XOR, whichever is smaller |
| Time fields (MoonSound T-state origin, NeoGS tick counter, TSFM sample position, ...) | inside the whole state | not stored while they advance by their usual step |
| Bytes per frame | 887 | **128** measured lower bound, 200 without the time-field rule |
| Per minute | 2.66 MB | **0.44 MB** (0.66 MB without the time-field rule) |

The same rules on ZX-Evo at the BASIC prompt: v1 2,244 B per frame (6.7 MB per minute; the 2 KB font RAM blob alone costs 1,015 B per frame and never changes), Phase 2 109–175 B per frame (0.42–0.62 MB per minute).

What remains after Phase 2 is real running state: the NeoGS card's Z80 executes its idle loop (PC, AF, R and MEMPTR change every frame), and the synthesizers' envelope, phase and tone counters move whether a note plays or not. §5.3.4 explains why these are not derived from time.

## 3. How it works today

### 3.1 Identity

- `PeripheralId` is a `uint8_t` enum, values 0–41 taken (SER:44-94). New devices append; values are never reused (SER:39-43).
- One device per id. The 16550 serializer `TTDSerialPort` is registered three times under three ids, because there is no instance concept: `SerialPort` (24), `Atm2IoEsp` (37), `ZiFiLine` (39) (MP:110-133).
- Personalities of one slot take separate ids so that state cannot cross between them: TurboSound / TSFM (0 / 4), GS / GS lightweight / NeoGS (5 / 11 / 12) (MP:55-69).
- The real limit is not the byte. The file header keeps a 64-bit mask of the recorded device ids and drops ids ≥ 64 from it silently (TTM:3631-3640; ttddumpformat.h:145-152). 22 ids are left before that happens.
- Designs already collide on numbers: VDAC2 plans `PeripheralId::Vdac2 = 26` ([vdac2-integration-design.md](../2026-10-01-tsconf-vdac2/vdac2-integration-design.md) §9.1), but 26 is `Atm2Kbc`; the Sprinter network plans blobs 39–43 ([sprinter-network tdd.md](../2026-10-02-sprinter-network/tdd.md), T-NET-10), but 39–41 are `ZiFiLine`, `ZiFi`, `CdDrive`.

### 3.2 Capture (`CaptureAll`, REG.cpp:95-115, called at TTM:1026)

For every registered device, every frame:
1. allocate a vector of `TTDStateSize()` bytes and call `TTDSaveState` (REG.cpp:110-111);
2. wrap it in a 12-byte `PeripheralBlobHeader` (REG.h:31-40: id, `flags` always 0, `reserved`, sizes) and compress the payload with zstd when that is smaller (REG.cpp:166-190).

Nothing is compared with the previous frame. The capture counts the stored bytes in `deviceBlobBytes` (TTM:1027-1028), reported as `bm2_work_device_blobs_bpf`.

### 3.3 Restore (`RestoreAll`, REG.cpp:117-164, called at TTM:1488)

- Driven by the registered devices, in ascending id order (REG.cpp:128-132). That order is the only dependency mechanism: `Wd1793Context` (35) must load after `BetaDisk` (1), because BetaDisk's load empties the command queue that the context then refills (ttdwd1793context.h:17-18).
- A device without a blob keeps its live state, a size mismatch is skipped, a blob nobody claims is ignored. The counts go into `TTDRestoreReport` (REG.h:47-61). `RestoreCheckpoint` only logs a warning when it is incomplete (TTM:1490-1494); no caller sees it.
- Derived state is rebuilt in two different ways: inside each device's `TTDLoadState` (MoonSound resets its render layer and buffers, soundchip_moonsound.cpp:536-543), and by the manager after banking and memory (`UpdateZ80Banks`, `ResyncScreenState`, TTM:1500-1517).

### 3.4 Versions and firmware

- No registry-level layout version: the header's `flags` and `reserved` are unused (REG.h:34-35). Devices that need one carry it inside their own bytes and each checks it differently:
  - TSFM writes `kTsfmStateVersion = 5` (soundchip_turbosoundfm.cpp:797, 824);
  - MoonSound a magic and `kTtdLayoutVersion` (soundchip_moonsound.h:21-34) and refuses a mismatch with a warning;
  - NeoGS a `TTD_LAYOUT` byte, and on a mismatch "card state left as is" (soundchip_neogs.cpp:1406-1412);
  - `Wd1793Context` a version byte (ttdwd1793context.h:20).
- Firmware: MoonSound stores its wave ROM hash in its header and only warns when it differs on restore (soundchip_moonsound.cpp:520-525). No other device records its firmware.

### 3.5 Device set

- The set is fixed for a recording: `RegisterMachinePeripherals` runs at start and load (MP:32-189). The sound devices register directly, outside the port decoder's declare / implement check (MP:53-84); only model state goes through it (MP:150-180).
- The one runtime change, the General Sound personality switch, is refused while recording (`TTDGuardedAction::SwitchGsCard`, TTM.h:335, TTM:878-880; soundmanager.cpp:1627-1633). A stopped session is dropped instead (soundmanager.cpp:1635-1636). `UpdatePeripheral` moves the registration to the new card's id (TTM.h:1564-1569, soundmanager.cpp:1697).
- On load, two slot guards refuse a session recorded with another TurboSound-slot device or another GS personality (TTM:4289-4335, 4337-4378).

### 3.6 Seek result

`TTDSeekResult` carries `reached`, `arrivedAt`, `haltReason` (target, external event, out of range) and the blocking marker (TTM.h:961-979). WebAPI (`ttd_api.cpp:763-766`), CLI (`cli-processor-ttd.cpp:550-557`) and MCP (`mcp-tools.cpp:3386`) report `halt_reason`. Nothing reports how well the state was restored.

### 3.7 Frame boundary sync

FR-19 holds by call order only: `MainLoop::OnFrameEnd` (mainloop.cpp:431, 595) runs the sound frame end before `OnFrameBoundary` (mainloop.cpp:464). No check catches a device that has not caught up.

## 4. Design overview

```
 device table (per session, versioned as the set changes)
 ┌───────┬──────────────┬────────┬───────┬─────────────┬────────────┐
 │ index │ type  name   │ layout │ size  │ firmware    │ after      │
 │ 0     │ 1  betadisk  │ 1      │ 254   │ -           │ -          │
 │ 1     │ 35 betadisk.context │ 1 │ ... │ -           │ 0          │
 │ 2     │ 10 moonsound │ 1      │ 4366  │ 9c1f…       │ -          │
 └───────┴──────────────┴────────┴───────┴─────────────┴────────────┘

 device history (per instance): versions, each Full | Ranges | Xor, chained
 checkpoint N:   changed: [2: Ranges 5 ranges, 57 B]
 checkpoint N+1: changed: []          ← idle frame: 0 entries
```

- **One device table per session.** The device set is fixed for a session (D38).
- **One history per device instance**, like a memory piece in Phase 1: a version is stored only when the state changed, as a difference from the previous version, with a chain length limit.
- **One restore result** for the whole restore: CPU, devices, regions, and later configuration.

## 5. Design

### 5.1 Step 1 — Device registry: type id, instance, version, order, firmware, after-restore (D23)

#### 5.1.1 Identity

```cpp
namespace ttd {

/// Stable device kind, stored in files. Values 0-41 are PeripheralId's, unchanged,
/// so a v1 blob id maps to a type id by value. New kinds append from 42 on.
enum class TTDDeviceType : uint16_t
{
    TurboSound = 0, BetaDisk = 1, /* ... same values as PeripheralId ... */ CdDrive = 41,
};

struct TTDDeviceKey
{
    TTDDeviceType type;
    std::string instance;   // "betadisk", "zifi.uart", "isa2.uart0"; lower case, dots, digits
};
```

- **Mapping from v1.** `TTDDeviceType` has the same values as `PeripheralId` for 0–41; a static assertion per entry keeps them equal. A v1 blob with id `n` becomes type `n` with the device's default instance name. The v1 file reader of Phase 1, Step 1 relies on this.
- **New devices while v1 still runs.** Until Phase 5 users record with v1, so a device that lands before then still needs a `PeripheralId`. Rule: it takes the next `PeripheralId` and the same number as its `TTDDeviceType`. After Phase 5, new devices take only a `TTDDeviceType`.
- **Allocation rule.** A design names a device by its type name, never by a number. The number is assigned when the code lands on master, in the one table. That resolves the existing collisions: VDAC2 becomes `TTDDeviceType::Vdac2` with the next free number at landing; the Sprinter network's five blobs reuse existing types where the device is the same (`SerialPort` for each UART, with instance names) and take new numbers only for new kinds.
- **Instances.** The three registrations of `TTDSerialPort` become one type `SerialPort` with instances `evo.uart` (today 24), `atm2ioesp.uart` (37), `zifi.uart` (39). For v1 compatibility their old ids stay valid as types: the v1 file reader maps 37 and 39 to `SerialPort` plus the instance name, and the engine stores the new form. Whether the old ids 37 and 39 are retired or kept as aliases forever is decided with file versioning (Phase 4, Step 1, V-7 in [integrity-and-versioning.md](integrity-and-versioning.md)).
- **Slot personalities** stay separate types (TSFM is not a TurboSound, NeoGS is not a GS): a state never crosses to a different device kind.

**As built (2026-10-03).** `TTDDeviceType` (u16) lists the 47 `PeripheralId`s with their numbers, one compile-time check each (`ttdserializable.h`). Instances so far: the WD1793 is `betadisk`, its command context `betadisk.context` (restored after it, the key taken from the controller's descriptor); the three 16550 serializers are one type, `SerialPort`, with instances `uart` (the machine's #xxEF port, v1 id 24), `atm2ioesp.uart` (37) and `zifi.uart` (39). Every other device keeps the default: its type is its v1 id, its instance its name in lower case. Restore ties follow v1's ascending blob id (`legacyId` in the descriptor), so the table's order equals v1's on every machine; the shadow model test checks it on the 10 large-memory models.

#### 5.1.2 Descriptor

Each serializer describes itself once, at registration. `TTDSerializable` (shared, SER:96-149) gains one virtual method with a default, so v1 and every existing serializer keep working unchanged:

```cpp
struct TTDTimeField
{
    uint16_t offset;   // byte offset in the state
    uint8_t  width;    // 2, 4 or 8 bytes, little endian, two's complement
};

struct TTDDeviceDescriptor
{
    TTDDeviceType type;
    std::string instance;                     // default: the type's name in lower case
    uint16_t layoutVersion = 1;
    uint32_t stateSize = 0;                   // TTDStateSize() for this layout
    uint64_t firmwareFingerprint = 0;         // 0 = runs no firmware outside the session
    std::vector<TTDDeviceKey> restoreAfter;   // explicit restore-order dependencies
    std::vector<TTDTimeField> timeFields;     // fields predicted from their previous step (§5.2.3)
    bool runsBehindCpu = false;               // must be at the frame boundary before capture (FR-19)
};

class TTDSerializable
{
    // ... existing methods unchanged ...
    virtual TTDDeviceDescriptor TTDDescribe() const;    // default built from TTDPeripheralId(), TTDStateSize()
    virtual void TTDAfterRestore(const TTDRestoreContext&) {}
    virtual uint64_t TTDSyncedTime() const { return kTTDNotTracked; }   // machine time the device has caught up to
};
```

- **Layout version.** `layoutVersion` starts at 1 for every device, whatever version number the device keeps inside its bytes today (TSFM 5, MoonSound, NeoGS). Phase 2 changes no device layout, so every restore stays byte-identical to v1's (D33). A later change to a device's bytes bumps `layoutVersion`.
- **Mismatch on load.** A stored version whose layout this build does not know is not loaded; the device is reported `Degraded` with reason `LayoutUnsupported` (§5.3). Whether a device may supply an upgrade function from an older layout is decided in Phase 4, Step 1 (V-2); the descriptor reserves the place for it: `TTDUpgradeState(fromVersion, src, dst)`, unused in Phase 2.
- **Size.** `stateSize` is fixed per layout version (V-5). A stored state of another size for the same version is `Degraded`, reason `SizeMismatch`.

#### 5.1.3 Restore order

- `restoreAfter` lists the devices that must be loaded first. The engine sorts the table topologically once per device-set version, not per restore.
- Ties are broken by (type id, instance name). For today's devices this gives exactly v1's ascending-id order, so engine and v1 load devices in the same order.
- The first explicit dependency: `Wd1793Context` (instance `betadisk.context`) after `BetaDisk`. It replaces the comment-only rule (ttdwd1793context.h:17-18).
- A cycle, or a dependency on a device the set does not contain, is a registration error: recording is refused with the device names, like a declared id without a serializer today (MP:162-180).

#### 5.1.4 Firmware fingerprint

- 64-bit hash of the firmware image a device runs from, computed at registration (not per frame), with the hash already used for the ROM signature (`ttd::HashBytes`, TTM:1415).
- Only firmware that is **not** recorded in the session needs one: the MoonSound wave ROM (already hashed, soundchip_moonsound.h:33), the GS ROM, the ATM2 keyboard controller ROM, the Sprinter BIOS. Firmware held in a memory region (NeoGS flash, Phase 1, Step 6) is restored with the session and needs none.
- On load, a different fingerprint does not stop the restore: the state comes back exactly. A replay from it may differ, so the restore result is `NotBitExact` with reason `FirmwareDiffers` and the device named. This replaces MoonSound's warning-only check.

#### 5.1.5 After-restore call

- Runs once per device, after the CPU, chipset, all device states, banking and all memory regions are restored, in the same dependency order.
- For state a device derives from other parts of the machine: memory windows cached from region contents, a display list rebuilt from graphics memory (VDAC2), audio buffers flushed. Work that a device does inside `TTDLoadState` today can stay there; Phase 2 moves nothing that already works.
- Phase 1's per-region callback ([phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md)) stays for work tied to one region. The after-restore call is the one place for work that needs everything restored.
- It receives a `TTDRestoreContext` (target position, whether this restore is followed by a replay) and may add an item to the restore result, for example "derived state could not be rebuilt".

#### 5.1.6 Engine side: the device table

```cpp
// core/src/debugger/ttd/engine/ttddevicetable.h
struct TTDDeviceEntry
{
    TTDDeviceDescriptor descriptor;
    TTDSerializable* device;          // live device, null when loaded from a file without a live match
};

class TTDDeviceTable
{
public:
    uint32_t SetVersion() const;                          // changes when the set changes (§5.4)
    const std::vector<uint16_t>& RestoreOrder() const;    // indices, topologically sorted
    const TTDDeviceEntry* Find(const TTDDeviceKey& key) const;
    TTDRegistrationResult Build(const std::vector<TTDSerializable*>& devices);   // checks ids, order, sizes
};
```

- `RegisterMachinePeripherals` (MP) keeps enumerating the machine's devices for both engines and for `MachineStateTransfer`. The engine reads the descriptors from the same registry, so the device set is still defined in one place.

### 5.2 Step 2 — Device state history: shared, changed ranges, time fields (D18)

#### 5.2.1 Data

```cpp
// core/src/debugger/ttd/engine/ttddevicehistory.h
enum class TTDDeviceEncoding : uint8_t { Full, Ranges, Xor };

struct TTDDeviceVersion
{
    TTDDeviceEncoding encoding;
    uint16_t depth;               // 0 for Full; previous version's depth + 1 otherwise
    uint32_t previous;            // version this one is a difference from (none for Full)
    uint32_t payload;             // piece-store handle: the bytes, compressed or not
    uint32_t crc32c;              // of the decoded state, checked on restore
};

class TTDDeviceHistory               // one per device instance
{
public:
    uint32_t Capture(const uint8_t* state, size_t size);   // returns the version, or the previous one if unchanged
    bool Decode(uint32_t version, uint8_t* out) const;     // walks back to Full, applies forward
private:
    std::vector<uint8_t> _base;          // the latest state, raw: the delta base
    std::vector<uint64_t> _timeSteps;    // per time field: its last step
    // versions live in the session's arena (Phase 1, Step 2)
};
```

A checkpoint holds, for devices, only the list of (device index, version) pairs **for devices that changed**.

A checkpoint finds the version of an unchanged device by walking back to the last checkpoint that changed it. To keep a seek constant-time, the engine also keeps a copy-on-write "current versions" array per checkpoint, shared between checkpoints exactly like Phase 1's reference blocks (one pointer per checkpoint when nothing changed).

**As built (2026-10-03), first version.** No new store: each device of the table gets a region of the engine's own (`TTDRegionId::DeviceStateFirst` + index) holding 4 bytes of length, then the state, padded to whole pieces. At each capture every device's state is laid out there and all its pieces are offered; the engine keeps only the pieces whose bytes changed, as Phase 1 does for memory: compressed XOR against the previous version, the chain limit K, change records and full tables every 64 checkpoints. A device without state in a frame stores length 0; state for a device the table lacks is listed in the checkpoint (`unclaimedDevices`). `TimeTravelEngine::DeviceState(index, id)` rebuilds a device's bytes at any checkpoint; `RestoreDevices` and every oracle use it. Live capture hands the engine the raw states the registry serialized (no compress and decompress); the v1 file feeder hands the v1 blobs, which the engine decodes. Measured (600 frames, device bytes per frame, v1 → engine, change records not included): 48K 528 → 165, Pentagon idle 949 → 269, Pentagon game 979 → 312, ZX-Evo 2,370 → 243, TS-Conf 1,839 → 341, Sprinter 12,312 → 183; D33 holds on all 46 configurations. The changed-ranges encoding and the time fields (anchors) are the next steps.

#### 5.2.2 Capture algorithm, per device, per frame

1. `TTDSaveState` into a scratch buffer owned by the history (no allocation; v1 allocates per device per frame, REG.cpp:110).
2. **Time fields:** for each declared field, compute the prediction `base + step`. If the value equals it, the field is "unchanged". Otherwise the new step is `value - base`. The field's bytes are compared like any other byte, but the stored difference (ranges or XOR) uses the **residual** `value - prediction`, so a field that keeps its step adds nothing.
3. **Compare** the scratch with `_base` (time fields excluded). Equal and all residuals zero: return the previous version. Cost: one `memcmp` of the state size.
4. Changed: build the changed ranges. Ranges separated by at most 3 equal bytes are merged (the setting used in the §10 measurement; tuned with BM-3). If the ranges come to more than a threshold (start: 64 B, tuned with BM-3), also compress the XOR and keep the smaller. A version is stored `Full` when its chain would reach the limit K (default 50, as for pieces, D4), or when the layout or size changed.
5. Copy the changed bytes into `_base`, update the steps.

Decoding a version for a restore: walk back to the last `Full` (at most K − 1 links), apply the differences forward, rebuild time fields from the stored residuals and steps. Then `TTDLoadState` gets exactly the bytes `TTDSaveState` produced at that frame.

#### 5.2.3 Time fields: what was measured

The roadmap says (D18, from E6) that the idle cards' blobs change every frame "although nothing plays". Before choosing a mechanism, the changing bytes were located field by field on the E6 sessions (method in §10). Per-frame step of each field over 3,000 frames on Pentagon idle:

| Device | Field | Step per frame | Rule |
|---|---|---|---|
| MoonSound | `tstateOrigin`, `lastChipTime` (soundchip_moonsound.h:31-32) | 71,680 every frame (one Pentagon frame in T-states) | time field: predicted exactly |
| MoonSound | `opl4.hostTicks` (opl4.cpp:688) | 71,680 every frame | time field |
| MoonSound | `opl4.masterPos`, `fmTicks`, `outSteps`, `hostRemainder` (opl4.cpp:687-693) | two values (693,633 / 693,634; 1,014 / 1,015; 903 / 904): a rational clock ratio | time field: residual 0 or ±1 |
| NeoGS | `_runner.now()` (soundchip_neogs.cpp:1343) | 2,457,648 / 2,457,630 alternating (instruction overshoot) | time field: small residual |
| NeoGS | `_timerStrobeAt`, `_nextDacCrystal`, `_nextTimerCrystal` (soundchip_neogs.cpp:1337, 1339, 1378) | constant in 98–99% of frames | time field |
| NeoGS | card Z80 PC, AF, R, MEMPTR | PC ±6, R +61 / −67: the card's idle loop | **real state**: ranges |
| TSFM | `_samplePhase` (soundchip_turbosoundfm.cpp:826) | 588,000 in 83% of frames, then wraps | time field |
| TSFM | render cursor offset (soundchip_turbosoundfm.cpp:862) | 24 different steps | real state: ranges |
| TSFM | AY tone / noise / envelope counters, ymfm counters | change every frame | **real state**: ranges |
| TSFM | `_decimationPhase` (f64) | does not change on idle | — |
| TSFM | the four per-chip decimator phases (f64, blob offsets 18–49) | change every frame | real state: floating point, cannot be predicted exactly; ranges |

What this means:
- Some counters do advance with time, and declaring them as time fields removes them: 233 → 174 B per frame with XOR, 200 → 128 B per frame with the best of ranges and XOR (Pentagon idle).
- Most of what remains is **not** a counter that can be derived from time. It is a CPU running code (NeoGS) and synthesizer counters that wrap at register-defined periods. Deriving them would mean running the device during a restore, which costs seek time (§9, question Q1).
- E6's "storing the XOR of a changed blob saves only a quarter" was computed on the **wrapped, compressed** v1 blobs (`model.py:214-226` XORs `raw`). XOR of the decoded state gives 233 B per frame against E6's 677 B per frame (2.03 MB per minute). The design target below uses the decoded state.

**Measured again with E8 (2026-10-03,** [E8](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e8-device-fields/README.md), 11 one-minute sessions on the current build): storing only changes gives 138–255 B per frame against v1's 528–2,380; deriving the time fields of MoonSound, NeoGS and TSFM gives 82–175 B per frame, 0.25–0.52 MB per minute. Every field of the three cards is mapped there (time / running / float). The ATM710's keyboard controller (73 B per frame) is the one large device not yet analyzed.

**Decided and built (2026-10-03): time fields in the engine, devices unchanged.** Deriving the clocks inside the devices (anchors in MoonSound, NeoGS, TSFM) was set aside: it changes each device's state and v1's format, and it is exact only if every formula always holds (a register write that resets a counter would break it silently). Instead a device declares its clock fields (`TTDDeviceDescriptor::timeFields`) and the engine stores each as its residual from a line through an anchor: value, frame, step per frame, kept at the end of the device's state region. Restoring frame N gives anchor + (N − anchor frame) × step + residual, exact by construction whatever the field does; a field that jumps only starts a new line (costs bytes, never correctness). The step of a new line is the average over the line it replaces. The residual stays on the line within ±32,767 for 4- and 8-byte fields (±2,047 for 2-byte ones, any value for 1-byte ones): clocks quantized to a period that does not divide the frame (the NeoGS timers on a 48K frame step N or N − 1 periods) stay on their line. Declared: MoonSound (tstateOrigin, lastChipTime, libopl4's masterPos, hostTicks, fmTicks, outSteps, FM and PCM envelope counters), NeoGS (timer strobe, next DAC sample, card clock, next timer tick, VS10xx clock), TSFM (ymfm's envelope counter and clock count, per chip). Measured (600 frames, device bytes per frame, before → after): 48K 165 → 153, Pentagon idle 269 → 195, game 312 → 238, ZX-Evo 243 → 199, TS-Conf 341 → 267, Sprinter 183 → 160; no configuration got worse. Less than E8's lower bound because a changed 4 KB piece costs a compressed XOR with its fixed overhead even when four bytes changed: the changed-ranges encoding is next. A device that keeps its own anchor (the Z84C15 CTC) remains the better design for new device code.

**As built (2026-10-03): the ATM2 keyboard controller, and what still changes every frame.** The controller (ATM710, ZX-Evo with KBC) runs its MCU every frame: 73 B per frame on ATM710 idle, a new version every frame. Its clocks are now time fields, with offsets taken from `Atm2Kbc::State` itself (`tBase`, `mcuBase`, `lastNow`, `answerClock`, `reads`, the MCU's clock and instruction count, its six `visibleAt` stamps, timers TL0 / TH0 / TL2 / TH2): 73 → 61 B per frame. A 1-byte time field had no residual limit, so its first line (step 0) was never replaced and it saved nothing; 1-byte fields now re-anchor beyond ±15 (TSFM's clock count benefits too). No configuration got worse; 128K-class, +2/+2A/+3 and ATM710 a little better; D33 on all 46. What remains changing every frame is state the devices really change, not clocks: the AY's and TSFM's noise generator (a shift register, pseudo-random, not on a line), TSFM's decimator phases (floating point), and the controller MCU's RAM and registers. This is the "state that devices running their own code really change" of Q1.

**As built (2026-10-03): changed ranges, in the piece store.** Not a device-only format: the piece store has a fourth encoding, `Ranges`, so memory pieces benefit too. A difference from the previous version is first measured as runs of non-zero XOR bytes (runs closer than 4 zero bytes merged, split at 255 bytes; 3 bytes of header each: u16 offset, u8 length). Up to 64 bytes (`Params::rangesLimit`) the runs are stored as they are, nothing compressed; above that the XOR is compressed and the runs are kept when they are still smaller. Decoding XORs the runs onto the base, like any difference: the chain limit K, references and release are unchanged. Measured (600 frames, bytes per frame of memory + references + devices, v1 → time fields → + ranges): 48K 690 → 350 → 295, Pentagon idle 1,131 → 406 → 343, game 3,191 → 1,820 → 1,729, ZX-Evo 7,096 → 739 → 608, TS-Conf 6,008 → 557 → 466, Sprinter 18,244 → 1,498 → 1,097; device bytes 48K 138, Pentagon idle 150, Sprinter 116. Compressions per frame fall 2-8x (Sprinter 24.2 → 3.0), which shortens capture. No configuration got worse; D33 on all 46.

**Device-side anchors first (2026-10-03, superseded above for existing devices).** Master's Z84C15 CTC (`6f6025e25`, Sprinter) shows the better fix where the device code is ours: the device stores the count at an anchor time and derives the live count from the clock, so its state does not change while it counts. Its blob then compares equal frame after frame with no engine rule at all, and the derivation is checked by the device's own tests. Order of preference: (1) anchors in the device, for devices in this repository (NeoGS timers and runner, MoonSound and TSFM origins, the WD1793 and tape clocks); (2) the engine-side time fields below, for state that comes from vendored libraries we do not change (ymfm, libopl4 internals). The measurement (step 1 of §8) lists which field goes which way.

Each device declares its own time fields in its descriptor (offsets in its own layout). Declaring a field is safe even when the guess is wrong: a wrong prediction only costs bytes, never correctness, because the residual is always stored exactly.

#### 5.2.4 Bytes target and how D33 checks it

| Configuration (E6 one-minute sessions) | v1 | E6 model of v2 | Phase 2 target |
|---|---|---|---|
| Pentagon 128 idle, NeoGS + MoonSound + TSFM | 2.66 MB/min (887 B/frame) | 2.03 MB/min | **≤ 0.5 MB/min** (measured lower bound 0.44) |
| ZX-Evo idle | 6.74 MB/min (2,244 B/frame) | 2.07 MB/min | **≤ 0.5 MB/min** (measured lower bound 0.42) |
| Pentagon, Eye Ache | 2.76 MB/min (919 B/frame) | 2.09 MB/min | ≤ 0.7 MB/min (measured lower bound 0.57) |
| A frame in which no device changed | 309–646 B (fixtures, current-state §8) | — | ≤ 2 B for device state (a zero count; PR-10) |

How it is checked:
- **D33 file size:** `bm3_device_blobs_bpf` not larger than v1 on any matrix case (CI gate, deterministic). Sharing alone makes this hold everywhere.
- **The target itself:** the same metric against the table above, on the E6 sessions fed through the v1 file reader, and on the matrix cases `PENTAGON_idle` and `ATM3_idle`.
- **Capture work:** `bm2_work_device_blobs_bpf` (bytes stored) and `bm2_work_compress_calls_opf` not larger than v1. The engine adds `bm2_work_device_compared_bpf` (bytes compared), reported, not gated.
- **Memory:** `bm4_heap_device_blobs_bpf` plus the delta bases (sum of state sizes: about 35 KB on Pentagon with cards, once per session).
- **Correctness:** the oracle (Phase 1, Step 1) compares, after every restore, each device's `TTDSaveState` bytes from the engine with v1's.

### 5.3 Step 3 — The restore result (FR-7)

#### 5.3.1 Data

```cpp
// core/src/debugger/ttd/engine/ttdrestoreresult.h
enum class TTDRestoreQuality : uint8_t
{
    Exact,         // every item restored; a replay from here repeats the recording
    NotBitExact,   // every item restored, but a replay may differ (firmware, configuration)
    Degraded,      // some item could not be restored; it is named, with what it holds now
    Damaged,       // stored data failed its check; the damaged positions are given
};

enum class TTDRestoreIssueKind : uint8_t
{
    DeviceMissingState,     // the device exists here, the session has no state for it
    DeviceNotPresent,       // the session has state for a device this machine lacks
    LayoutUnsupported,
    SizeMismatch,
    DeviceSetDiffers,       // the target's device set could not be rebuilt (§5.4)
    FirmwareDiffers,        // NotBitExact
    ConfigurationDiffers,   // NotBitExact; filled from Phase 3, Step 4
    DataDamaged,            // CRC failure; Phase 4 adds the file-level checks
    AfterRestoreFailed,
};

enum class TTDLiveStateAction : uint8_t { NotApplicable, KeptLive };

struct TTDRestoreIssue
{
    TTDRestoreIssueKind kind;
    TTDRestoreQuality severity;
    TTDDeviceKey device;                // empty for machine-wide issues
    TTDLiveStateAction action;          // what the device holds now
    std::string detail;                 // "layout 3, this build reads 1-2"
};

struct TTDPositionRange { TTDPosition first; TTDPosition last; };   // D15 positions, with branch

struct TTDRestoreResult
{
    TTDRestoreQuality quality = TTDRestoreQuality::Exact;   // the worst of the issues
    TTDPosition position;                                   // where the machine now is (D15)
    std::vector<TTDRestoreIssue> issues;
    std::optional<TTDPositionRange> damagedRange;           // positions that depend on the damaged item
};
```

- **Ordering:** `Exact < NotBitExact < Degraded < Damaged`; `quality` is the worst issue.
- **What a device without state holds.** It keeps its live state, and the issue says so. Within a session this does not happen: the device set is fixed (D38) and every checkpoint holds every device. It happens only when a session meets a machine whose devices differ, or on damage; both are reported, not repaired.
- **Damaged range.** A damaged device version spoils every later version that chains from it, up to the next `Full`. The engine knows the dependencies (D5), so it reports the exact range. Phase 2 detects damage with the CRC32C per version (§5.2.1); Phase 4 adds the file-level checks and fills the same field.
- **Branches (D7).** Resuming from a position starts a branch, and the branch's first checkpoint is captured from the live machine. If the restore was `Degraded`, that checkpoint would carry the unrestored device into the new history. The result is therefore available before a resume, and a resume from a degraded position is flagged on the branch's first checkpoint (the flag is part of the model now; the branch UI is PLAN #76).

**As built (2026-10-03).** The engine's `TTDRestoreResult` carries the issues (`TTDRestoreIssue`: kind, severity, device key, what the device holds now, detail); its status is the worst. `TimeTravelEngine::RestoreDevices(index, context)` restores every device of the table in restore order after the memory regions, then calls each `TTDAfterRestore`. It reports a device without state (kept live), state for a device this machine lacks, a state that does not fit, and a different firmware (`NotBitExact`). A device whose state the engine keeps without its region memory (General Sound, Sprinter video and fast RAM, the VDAC2 memory) loads through `ITTDRegionSource::TTDLoadStateWithoutRegions`. The oracle: on the 10 large-memory models, v1 restores a checkpoint, the machine runs on, the engine restores the same checkpoint, and every device saves the same bytes as after v1's restore. Writing it found an engine bug: a region smaller than a piece (the SMUC EEPROM, 2 KB) was restored as a whole 4 KB piece, writing past the device's memory; the last partial piece is now decoded aside. Damage and `CheckSession` come with the device history (Step 2); the surfaces in Phase 5.

**Q2 decided (2026-10-03), then withdrawn the same day:** resetting a device without state to its power-on state was dropped. The emulator has no true power-on per device (its hard reset leaves GS RAM, MoonSound wave RAM and the keyboard controller's RAM, never resets the mouse, joystick, ZiFi or the RTC, and several resets reach outside their device), and the case does not arise within a session (D38). A machine reset stops the recording (`Emulator::Reset`); recording again starts a new session. `TTDResetToPowerOn` is removed; a device without state keeps its live state and is reported.

#### 5.3.2 Engine interface

```cpp
class TimeTravelEngine
{
public:
    TTDRestoreResult SeekTo(const TTDPosition& target);
    const TTDRestoreResult& LastRestoreResult() const;
    TTDRestoreResult CheckSession() const;   // every device version decoded and checked, no machine change
};
```

`CheckSession` lets a loaded file report its problems up front, as target-architecture §4 asks for the device table, without seeking.

#### 5.3.3 Surfaces (wired in Phase 5)

Phase 2 builds and tests the engine API. Users are on v1 until Phase 5 ([phase-5-switchover-tdd.md](phase-5-switchover-tdd.md)), which connects each surface. Every surface carries the same fields with the same names; changes are additive (QR-8):

| Surface | Where | What it shows |
|---|---|---|
| C++ | `TimeTravelEngine::SeekTo` | `TTDRestoreResult` |
| WebAPI | `POST .../ttd/seek` response, `GET .../ttd/status` (`last_restore`) | `restore: {quality, issues: [{kind, severity, device: {type, instance}, action, detail}], damaged_range}`, next to `halt_reason` |
| MCP | `time_travel` seek / step / reverse results; `inspect_state` aspect `ttd` | the same object; a one-line summary for agents: `restore: degraded (moonsound: layout 3 unsupported, kept live)` |
| CLI | `ttd seek`, `ttd status` | the summary line, then one line per issue |
| Lua / Python | `ttd_seek` and status functions | a table / dict with the same keys |
| Qt | TTD toolbar widget | a warning mark on the position when not exact, issues in the tooltip and the TTD panel |
| DeZog | reverse-debugging responses (`dezogdebugadapter.cpp`) | not exact → a warning in the debug console with the summary line |

The automation contract test (`ttdautomationcontract_test.cpp`) gains a case per surface.

**As built (2026-10-03), damage and the session check.** Every stored version (pieces and device states alike) carries its CRC32C in the piece store. A version that fails it is a `DataDamaged` issue with `firstFrame` / `lastFrame`: the checkpoints around the failing one whose version of that piece fails too (the same version, or differences built on it), so the range ends at the piece's next change that does not depend on it. A device state that fails is reported as `DataDamaged` with the device named (before, it read as "no state"), and the device keeps its live state, as for a missing state. `TimeTravelEngine::CheckSession()` checks the whole session without touching the machine: every version decoded once (walking the change records, not every checkpoint), damage ranges, frames in which a device had no state (`DeviceMissingState` with its frames), state for devices this machine lacks, and each live device's firmware; at most 64 issues listed, the rest counted. Tests: `TimeTravelEngine_Damage_Test` (a damaged piece and the frames it reaches; a device's damage and missing frames). Mutation: dropping the forward extension of the range fails them.

### 5.4 Step 4 — Sound devices on the contract (FR-19); the device set fixed for a session (D38, FR-4)

#### 5.4.1 Every device under the declare / implement check

Today the sound devices (TurboSound slot, Covox, GS / NeoGS, MoonSound) register directly (MP:53-84); only model state passes the check that a declared id has a serializer (MP:162-180). Step 4:
- every device registers through its descriptor, and the device table checks the descriptor against the live device (size, layout, dependencies) for all of them;
- the descriptors of TSFM, MoonSound and NeoGS declare their time fields (§5.2.3) and `runsBehindCpu = true`;
- the slot guards on load (TTM:4289-4378) become one generic rule: a stored state whose type differs from the live device in the same slot is `DeviceSetDiffers`, unless the set can be rebuilt (§5.4.3).

#### 5.4.2 Frame boundary sync (FR-19)

- A device with `runsBehindCpu` reports `TTDSyncedTime()`, the machine time (D20) it has caught up to.
- At capture the engine checks, in debug builds and in the contract test, that this equals the frame boundary. In release builds the check is one comparison per such device per frame (three on a Pentagon with cards).
- After a restore, the same check runs after the after-restore calls: a device left behind or ahead is an `AfterRestoreFailed` issue.
- This turns the order in `MainLoop` (mainloop.cpp:431, 464) into a tested rule.

**As built (2026-10-03), contract and sync.**
- *Registration.* `RegisterMachinePeripherals` ends with `TTDPeripheralRegistry::CheckDeviceTable`: each registered device must name the id it is registered under, describe itself under that id and describe the state size it saves; then the engine's device table is built from the same entries (`DeviceEntries`, which the shadow session now uses too), so a missing dependency, a cycle or a time field outside the state refuses recording with the device named. Before, such a table made the engine refuse its session with only a log line (the WD1793 context's dependency name was such a case). The slot guards on load (TTM:4289-4378) stay with v1 until Phase 5.
- *Sync (FR-19).* `TTDSerializable::TTDSyncedTime(offset)` answers whether a device's own clock stands where a frame boundary needs it; the offset is in the device's own units, for the report, so no device converts its clock into CPU T-states. TSFM: the core at the CPU's T-state (or adopting it at the next sync). MoonSound: the chip at or after the frame's start on its axis and not past the CPU. GS and NeoGS: the card at or after its frame base and less than a frame past it. Their descriptors set `runsBehindCpu`. The engine asks at every capture (a miss is counted in `SyncMissCount` / `SyncMisses`, the frame still recorded) and after `RestoreDevices`' after-restore calls (`AfterRestoreFailed`, device named). The capture already sees the next frame's start (the checkpoint convention), so the rule checked is "after the frame end and the next frame's start" rather than "at the frame end".
- *Tests.* `TTDModelStateContract_Test.EveryDeviceMatchesItsDescriptorOnEveryModel` (16 models x GS / NeoGS, MoonSound, TSFM where the helper fits it); `ADeviceNotMatchingItsDescriptorIsRefusedByName`; `TimeTravelManager_ShadowCards_Test` (Pentagon with TSFM, MoonSound and GS or NeoGS: no miss at any capture, every frame as v1, restores exact); `TimeTravelEngine_Sync_Test`. Mutations: capturing before the cards' frame start fails the cards test (TSFM 71,683 T-states from the frame start); dropping the table build from the registration check fails the refusal test.

#### 5.4.3 The device set is fixed for a session (D38)

*Changed 2026-10-03, owner decision.* This section first made a device-set change an event on the timeline (`DeviceSetChanged`, device-set versions, `ITTDDeviceSetProvider::ApplyDeviceSet`), with the GS personality switch as its first user. Dropped: with machine bus slots the machine declares its slots and the cards in them, and no one swaps a card while a session records. The set is therefore part of the machine:
- a change of the set while recording is refused, as v1 does today (`TTDGuardedAction::SwitchGsCard`);
- outside a recording it starts a new session linked to its parent, as a model switch does (Phase 5);
- a session restored on a machine whose set differs reports `DeviceSetDiffers` or `DeviceNotPresent` per device (the generic rule of §5.4.1 replaces v1's two slot guards on load).

### 5.5 Built in now, used later

| Built in Phase 2 | Used by |
|---|---|
| `TTDUpgradeState` slot in the descriptor | Phase 4, Step 1 decides whether layouts are upgraded or refused |
| `ConfigurationDiffers`, `DataDamaged`, `damagedRange` | Phase 3, Step 4 (configuration fingerprint), Phase 4 (file integrity) |
| Device-history versions with dependencies | Phase 4 eviction rebuilds a chain that reaches past the new start as `Full` (D5), as for pieces |
| `TTDSyncedTime` in machine time | Phase 3, Step 3: several CPUs, a position on a card CPU (D20) |
| Instance names | Several IDE channels, UARTs, ISA cards (Sprinter ISA, network) |

### 5.6 File-format consequences (serialized in Phase 4)

Phase 2 changes the in-memory model only. [phase-4-session-file-tdd.md](phase-4-session-file-tdd.md) must serialize:
- **The device table**, one per session: per entry type u16, instance (u8 length + bytes), layout version u16, state size u32, firmware fingerprint u64, dependency count u8 + indices u16, time-field count u8 + (offset u16, width u8), flags u8 (`runsBehindCpu`). Every width checked against its largest value before the format is fixed.
- **Per checkpoint:** a count of changed devices (u16, zero on an idle frame) and per changed device: table index u16, encoding u8, depth u16, payload length u32, payload, CRC32C.
- **No 64-bit mask** of device ids: the device table replaces it, so the limit of 64 ids goes away.
- `ttd.ksy` and the Python analyzer gain these structures in Phase 4, Step 6.

v1's format does not change in Phases 1–4.

## 6. Performance

| Work | When | Cost |
|---|---|---|
| `TTDSaveState` per device | per frame while recording | as v1 (shared code); no allocation (v1 allocates a vector per device per frame) |
| `memcmp` with the delta base | per frame, per device | state size: about 35 KB per frame on Pentagon with cards, mostly NeoGS's 21.5 KB |
| Time-field prediction | per frame, per declared field | a few integer additions (about a dozen fields on Pentagon with cards) |
| Ranges / XOR + zstd | per frame, per **changed** device | 3–4 devices on idle cards; v1 compresses all 9 |
| Topological sort | per session | once |
| Restore: decode a version | per seek, per device | at most K − 1 range applications; restore time `bm6_restore_devices_us_p50` within PR-5 |
| After-restore calls, sync check | per seek | one call per device |

- **Zero cost when off.** When nothing records, none of this runs: the engine's frame hook returns on its one per-frame check, as in Phase 1. Nothing is added to a per-event path (port access, memory write, card CPU instruction). The sync check and the time-field prediction run per frame, never per event.
- **Measured with:** `bm2_work_device_blobs_bpf`, `bm2_work_compress_calls_opf`, the new `bm2_work_device_compared_bpf`, `bm2_capture_us_*`, `bm3_device_blobs_bpf`, `bm4_heap_device_blobs_bpf`, `bm6_restore_devices_us_p50`, BM-5 seek times. Timings on an idle host (load < 12), run twice.
- **Possible later saving, not in v1 of the design:** a device that knows it was not touched since the last save could skip `TTDSaveState` and the comparison. Measured first: on Pentagon idle, 6 of 9 devices are unchanged, but their states are small (BetaDisk 254 B, IDE 4,252 B); NeoGS, the largest, changes every frame.

## 7. Tests

Every new test is checked by mutation: it must fail when the mechanism it guards is removed.

| Step | Test | Proves |
|---|---|---|
| 1 | Every `PeripheralId` 0–41 maps to the same `TTDDeviceType` value; a v1 blob loads into the type of the same number | v1 files map by value |
| 1 | Three serial ports register as one type with three instances; capture and restore each one | instances, not ids per use |
| 1 | Restore order of today's devices equals v1's ascending-id order; mutation: drop the `Wd1793Context` dependency and sort by name → restore inside a WD1793 command fails (`ttdwd1793serializer_test.cpp` case) | explicit dependencies carry the order |
| 1 | A dependency cycle, or a dependency on an absent device, refuses recording and names both devices | registration check |
| 1 | A stored layout version the device does not know → `Degraded / LayoutUnsupported`, device named; size mismatch → `SizeMismatch` | versions are checked in release builds |
| 1 | Different firmware fingerprint → restore exact, result `NotBitExact / FirmwareDiffers` (MoonSound wave ROM) | firmware is reported, not only logged |
| 1 | After-restore runs once per device, after every region, in dependency order | the call contract |
| 2 | A device whose state does not change for 100 frames stores 1 version; its checkpoints store no entry | same as previous |
| 2 | Every stored version decodes to exactly the bytes `TTDSaveState` produced, on every frame of the E6 and fixture sessions | ranges, XOR and time fields are lossless |
| 2 | A time field with a constant step stores nothing; a changed step stores the residual; mutation: drop the residual → decode fails | time fields are exact |
| 2 | A chain reaching K is stored `Full` | chain limit for devices |
| 2 | Bytes per frame on `PENTAGON_idle` and `ATM3_idle` within the §5.2.4 target | the bytes target |
| 3 | Corrupt one stored version (bit flip): result `Damaged`, `damagedRange` ends at the next `Full`, device named | damage is reported with its range |
| 3 | A session with a device this machine lacks → `Degraded / DeviceNotPresent`; a device without state → `DeviceMissingState` and the action taken | no silent live state |
| 3 | `CheckSession` reports the same issues as a seek, without changing the machine | up-front report |
| 4 | Every creatable model: every registered device has a descriptor that matches the live device (extends `ttdmodelstatecontract_test.cpp`) | all devices under the contract |
| 4 | Every `runsBehindCpu` device reports the frame boundary at capture; mutation: capture before the sound frame end → test fails | FR-19 is a tested rule |
| All | **D33 oracle:** every frame of the fixture corpus, the matrix sessions and the E6 sessions restores each device byte for byte as v1 | correctness against v1 |
| All | **D33 matrix:** `bm3_device_blobs_bpf`, `bm2_work_device_blobs_bpf`, `bm2_work_compress_calls_opf`, `bm4_heap_device_blobs_bpf` not larger than v1 on any case; seek within PR-5 | the quality bar |

## 8. Order of work

Each item lands as its own commits, and each commit passes the full gate (build with zero warnings, `core-tests`, the oracle).

1. **Measurement first:** add the field-level device measurement (§10) as experiment E8 next to E1–E7, so the numbers in §5.2.3 can be rerun. No engine code. *Done 2026-10-03.*
2. **Step 1, identity and descriptors:** `TTDDeviceType`, `TTDDescribe` with defaults, the device table, restore order equal to v1's. The engine still stores every state whole. The oracle passes. *Done 2026-10-03.*
3. **Step 3, restore result:** `TTDRestoreResult` from the device table's checks (missing, not present, layout, size, firmware). Tests with damaged and mismatched sessions. *Done 2026-10-03, damage with Step 2.*
4. **Step 2, history:** same as previous, then ranges / XOR, then the chain limit. Bytes drop; the oracle passes after each.
5. **Step 2, time fields:** declared per device, one device per commit (MoonSound, NeoGS, TSFM, then the rest the measurement finds).
6. **Step 4, contract and sync:** all devices through descriptors; `TTDSyncedTime` and its check.
7. ~~**Step 4, device-set events**~~ — dropped 2026-10-03: the device set is fixed for a session (D38).
8. Run the full matrix, store it as the Phase 2 baseline, write the results document.

Step 3 comes before Step 2 so that every history change after it is checked by the same result reporting.

## 9. Risks and open questions

| # | Risk / question | Plan | Needs the user's decision |
|---|---|---|---|
| Q1 | **PR-10 and running cards.** PR-10 asks ≤ 64 B for "a frame in which nothing changed, regardless of configuration". With NeoGS fitted, its Z80 runs its idle loop and something changes every frame (§5.2.3). Phase 2 meets PR-10 for device state only where no device runs by itself. Deriving a card's state by re-running it during a restore would remove the cost but add up to K frames of card emulation to a seek | Read PR-10 as "≤ 64 B plus the state that devices running their own code really change", and report that part per configuration | **yes** (recommendation: accept this reading) |
| Q2 | A device without state at the target: reset it to power-on or keep the live state? | Reset when the device supports it (repeatable), keep live otherwise; both reported | *Decided 2026-10-03: no reset (withdrawn, see §5.3)* |
| Q3 | Old ids 37 (`Atm2IoEsp`) and 39 (`ZiFiLine`) become instances of `SerialPort`: retire the numbers or keep them as aliases forever? | Decided with V-7 in Phase 4, Step 1 | yes, in Phase 4 |
| Q4 | Store small firmware images (GS ROM 32 KB, keyboard controller ROM) in the session, so its replay is exact anywhere? | Phase 2 records the fingerprint only; storing images is a region question for Phase 4 | yes, in Phase 4 |
| R1 | Enforcing the restore result exposes silent failures that exist today as visible errors | Land with the contract tests; triage each new report (migration-trajectory risk table) | — |
| R2 | A declared time field that is not one costs bytes | Only bytes, never correctness; E8 lists the fields with their measured steps | — |
| R3 | The time-field gain is a lower bound: residuals are not always zero (rational clocks: 72–504 of 3,000 frames) | The target in §5.2.4 has a margin above the lower bound; BM-3 decides | — |
| R4 (moot, D38) | Two personalities of a slot both register when the set changes inside a frame | A set change applies only at a frame boundary (§5.4.3); test with the GS switch | — |
| R5 | Device-history numbers come from v1 files fed to a model, not from the engine | The engine reproduces them through the v1 file reader; the matrix checks them | — |

**Conflict with the decisions document, for the record:** D18's reason "idle cards cost 2 MB per minute" is the E6 model, which XORed the compressed v1 blobs. On the decoded state the same sessions give 0.66 MB per minute before any time-field rule. D18's conclusion stands, but the "2 MB" figure overstated what Phase 2 starts from. *Resolved 2026-10-02:* E6 now models the decoded state (0.8–0.9 MB per minute with a compressed XOR of the whole blob, the method of the model; this design's changed byte ranges give the 0.66 above), and D18 cites the corrected figures. D23's "`PeripheralId` is 0–41 full" means "taken": the hard limit is the 64-bit device mask in v1's file header (ids ≥ 64 dropped), not the byte.

## 10. Sources

- Code: master `8ddaf708e`, references above.
- [E5](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e5-heap-split/README.md): device state 2.7 MB per minute at the BASIC prompt, 6.7 on ZX-Evo. [E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md) and its [results](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/results.md): v1 2.66 / 6.74 MB per minute, v2 model 2.03–2.07; the model's device rule in `e6-v1-v2-model/model.py:208-230`.
- **Field-level measurement (2026-10-02, for this TDD; becomes E8).** Input: the E6 one-minute sessions `ttd-bench-v1-PENTAGON_idle.ttd`, `ttd-bench-v1-ATM3_idle.ttd`, `ttd-bench-v1-PENTAGON_demo-eyeache.ttd` in `scratch/ttd-experiments/real/1min/`. Method, with the project's analyzer (`tools/verification/ttd-analyzer`, `decode_peripheral_blob`):
  - per device and frame, decode the state (the GS state cut to its 95 fixed bytes, as E6 does), compare with the previous frame, record the changed byte offsets;
  - map the offsets to the serializers cited in §5.2.3 and record each field's per-frame step;
  - size each change three ways: zstd level 1 of the XOR; changed ranges (3 B header + bytes, ranges closer than 3 equal bytes merged); the smaller of the two; plus 2 B per device per frame for the version reference;
  - repeat with the time fields of §5.2.3 zeroed, which is the lower bound of the time-field rule.

  Results: Pentagon idle 233 / 224 / 200 B per frame (XOR / ranges / best), 174 / 150 / 128 with time fields; ZX-Evo idle 209 / 186 / 175, 155 / 120 / 109; Eye Ache 274 / 255 / 240, 217 / 183 / 171.
- Requirements and decisions: [requirements.md](requirements.md), [engine-decisions.md](engine-decisions.md); earlier device-state design: [target-architecture.md](target-architecture.md) §4, [migration-trajectory.md](migration-trajectory.md) Phase 2; versioning questions: [integrity-and-versioning.md](integrity-and-versioning.md) V-1–V-7; today's behavior: [current-state.md](current-state.md) §6.
- Id plans that collide: [vdac2-integration-design.md](../2026-10-01-tsconf-vdac2/vdac2-integration-design.md) §9.1 (id 26), [sprinter-network tdd.md](../2026-10-02-sprinter-network/tdd.md) T-NET-10 (blobs 39–43).
