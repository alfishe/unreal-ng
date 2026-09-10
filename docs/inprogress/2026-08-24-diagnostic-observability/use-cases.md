# Port Access Trace: Use Cases and Data Requirements

## Core Principle

Every I/O operation in the emulator should produce a **single structured trace record** containing everything needed to diagnose the operation in isolation — without requiring cross-referencing with log files, without recompilation, and without the performance penalty of `ModuleLogger` string formatting.

---

## Trace Record Definition

> [!NOTE]
> This struct is the **authoritative** event definition (the earlier 20-byte sketch in `findings.md` is superseded). The whole recorder is gated at runtime by the FeatureManager feature `porttrace` (no compile-time flags) — see `implementation_plan.md` § "Feature Gate".

```cpp
struct PortTraceEvent
{
    // ── Timing ──
    uint64_t timestamp;         // Absolute T-state: (frame_counter * tStatesPerFrame) + cpu.t
                                // Computed at capture from the recorder's cached tStatesPerFrame.
                                // The session header records tStatesPerFrame; if the timing mode
                                // changes mid-session (turbo), the recorder refreshes its cache and
                                // notes the change in session metadata — (frameNumber, cpu.t) remain
                                // the reliable ordering key in that case.
    uint32_t frameNumber;       // Emulator frame counter

    // ── Port Identity ──
    uint16_t rawPort;           // Full 16-bit address bus value (what Z80 put on the bus)
    uint16_t decodedPort;       // Port after model-specific decoding (0x0000 = unmapped)
    uint8_t  decodeRuleIndex;   // Which rule in portMasksMatches[] fired (0xFF = none/BDI fallback)

    // ── Data ──
    uint8_t  value;             // Data byte: IN result or OUT value

    // ── Context ──
    uint16_t pc;                // Program counter of the IN/OUT instruction (m1_pc)

    // ── Device Attribution ──
    uint8_t  deviceId;          // Enum identifying which peripheral handled this (see DeviceId below)

    // ── Disposition Flags ──
    uint8_t  flags;             // Packed bitfield:
    //   bit 0:    direction          (0=IN, 1=OUT)
    //   bit 1:    wasDecoded         (_lastPortDecoded was set)
    //   bit 2:    hadHandler         (PortDevice* existed in _portDevices map)
    //   bit 3:    wasBeta128Gated    (port was in Beta128 set but CF_TRDOS was clear)
    //   bit 4:    wasHandledInline   (decoder handled it directly, not via PeripheralPortIn/Out)
    //   bit 5:    cfTrdosActive      (CF_TRDOS state at event time — lets gate events be judged
    //                                 without a separate machine-state snapshot; see use case 1.5)
    //   bit 6:    viaLegacyBasePath  (event captured on the legacy base-class DecodePortIn/Out
    //                                 path, if that path survives the audit — see implementation_plan.md;
    //                                 distinguishes the two reads of a Ghost-Byte pair)
    //   bit 7:    reserved
};
// sizeof = 22 bytes (padded to 24 for alignment)
// Default capacity 1,048,576 events = 24 MB (configurable; overflow mode ring / stop-when-full)
```

### Device ID Enumeration

```cpp
enum class PortDeviceId : uint8_t
{
    None           = 0x00,  // No handler found (unmapped port, returned 0xFF)
    ULA_FE         = 0x01,  // Port #FE — keyboard, border, beeper, tape
    Memory_7FFD    = 0x02,  // Port #7FFD — memory paging
    Memory_1FFD    = 0x03,  // Port #1FFD — +3 extended paging
    AY_FFFD        = 0x04,  // Port #FFFD — AY register select / TurboSound chip-select
    AY_BFFD        = 0x05,  // Port #BFFD — AY data write
    WD1793_Status  = 0x06,  // Port #1F — FDC Status/Command register
    WD1793_Track   = 0x07,  // Port #3F — FDC Track register
    WD1793_Sector  = 0x08,  // Port #5F — FDC Sector register
    WD1793_Data    = 0x09,  // Port #7F — FDC Data register
    Beta128_System = 0x0A,  // Port #FF — Beta128 system (DRQ/INTRQ, drive select)
    Covox          = 0x0B,  // Port #FB — COVOX/SOUNDRIVE DAC
    Kempston       = 0x0C,  // Kempston joystick
    Mouse          = 0x0D,  // Kempston mouse
    Custom         = 0x0E,  // Other registered PortDevice
    InlineDecoder  = 0x0F,  // Handled directly by decoder (e.g. 7FFD read-back)
    Gated          = 0x10,  // Port matched a device but was blocked (e.g. Beta128 gate)
};
```

The `deviceId` is resolved at capture time from the `_portDevices` map + the decoder's inline switch. This is the key field the user asked for — "tagged what device it should belong to."

---

## Use-Case Catalog

### Category 1: Port Decode Errors

These bugs are caused by the `decodePort()` mask/match table resolving a raw port address to the **wrong** canonical port, or failing to resolve it at all.

| # | Use Case | Concrete Example | Root Cause Pattern | Trace Fields That Diagnose It |
|---|----------|------------------|--------------------|-------------------------------|
| 1.1 | **Mask collision — port claimed by wrong device** | SOUNDRIVE ports `#F1`/`#F9` (bit2=0) were matched by the 7FFD rule (A15=0, A1=0) before the SOUNDRIVE-specific `A2=1` exclusion was added | Two rules in `portMasksMatches[]` overlap; the first-match-wins linear scan picks the wrong one | `rawPort`, `decodedPort`, `decodeRuleIndex` — trace shows `rawPort=0x00F1` decoded to `0x7FFD` via rule #2 instead of `0x00FB` via rule #4 |
| 1.2 | **Missing mask rule — port falls to unmapped** | New peripheral port added to hardware config but no entry in `portMasksMatches[]` | `decodePort()` returns `0x0000` | `rawPort`, `decodedPort=0x0000`, `hadHandler=false`, `deviceId=None` — immediate red flag |
| 1.3 | **Alias resolution failure** | `OUT (0xFEFD), 0xFE` should decode as `#FFFD` but a model's `decodePort()` doesn't recognize this alias | Missing or incorrect mask bits for the aliased high byte | `rawPort=0xFEFD`, `decodedPort=???` — compare expected vs actual |
| 1.4 | **Cross-model decode divergence** | Pentagon128 adds COVOX rule that Spectrum128 lacks; switching model breaks SOUNDRIVE | Model A's `decodePort()` returns different `decodedPort` than Model B for same `rawPort` | Capture traces from both models for same program; diff `decodedPort` column |
| 1.5 | **Gate logic error — wrong state test** | Beta128 gate blocks FDC ports when `CF_TRDOS` is clear, but a code change inverted the flag test | `IsBeta128Port()` returns true, flag test wrong | `rawPort=0x001F`, `decodedPort=0x0000`, `wasBeta128Gated=true`, `deviceId=Gated`, plus `cfTrdosActive` (flags bit 5) recorded at event time — the trace alone shows both that the gate fired *and* whether the flag state justified it |
| 1.6 | **Rule ordering — priority inversion** | Port `#FF` matches both Beta128 system register rule AND a broader catch-all | Linear scan picks first match; reordering rules changes behavior | `decodeRuleIndex` — shows which rule was chosen; should be the Beta128-specific one (rule #5), not a generic one |

### Category 2: Over-Strict Decode vs Relaxed Software Addressing

These bugs are the inverse of mask collisions: the decoder's mask checks **more bits than the real hardware does**, so software that uses "sloppy" but perfectly valid partial addressing fails — the port falls to unmapped (0x0000) or matches the wrong rule, while on real hardware the device would respond.

This is a fundamental property of ZX Spectrum hardware: the ULA, AY, and memory paging ports are **partially decoded** — they respond to wide ranges of addresses because the address comparator ignores certain bus lines. Software authors know this and routinely use non-canonical addresses (e.g., `OUT (C),A` with whatever value `BC` happens to hold, or the Z80's `OUT (n),A` which puts `A` on the high byte).

#### Background: How partial decoding works on real hardware

On real Spectrum 128K hardware, port `#FFFD` (AY control) responds when:
- A15=1, A14=1, A1=0 (3-bit mask: `0xC002`, match `0xC000`)
- **All other 13 address lines are ignored**

So `OUT (0xFFFD)`, `OUT (0xC001)`, `OUT (0xC005)`, and `OUT (0xFFF5)` are **all identical** to the AY chip. Software that uses any of these addresses is correct on real hardware but may break if the emulator checks extra bits.

#### Concrete mask analysis

Current Pentagon128 decode table masks, annotated with strictness:

| Port | Mask | Bits Checked | Real HW Bits | Extra Bits in Emulator |
|------|------|-------------|-------------|----------------------|
| `#FFFD` | `0xC002` (A15,A14,A1) | 3 | 3 (A15,A14,A1) | None — correct ✓ |
| `#BFFD` | `0xC002` (A15,A14,A1) | 3 | 3 (A15,A14,A1) | None — correct ✓ |
| `#7FFD` | `0x8006` (A15,A2,A1) | 3 | 2 (A15,A1) | **A2 is extra** — added to exclude SOUNDRIVE |
| `#FE` | `0x0001` (A0) | 1 | 1 (A0) | None — correct ✓ |
| `COVOX` | `0x00F5` (A7-A4,A2,A0) | 6 | varies by clone | Potentially too strict for some clones |
| `#FF` (Beta128) | `0x0083` (A7,A1,A0) | 3 | 3 | None — correct ✓ |
| BDI `#1F-#7F` | `0x83` (A7,A1,A0) | 3 | 3 | None — correct ✓ |

The `#7FFD` case is the most instructive: we intentionally added an **extra bit** (A2) to the mask to prevent SOUNDRIVE collision (use case 1.1). This is a deliberate trade-off — it fixes SOUNDRIVE but could theoretically break software that writes to `#7FFD` via an address with A2=0 (e.g., `OUT (0x7FF9),A`).

| # | Use Case | Concrete Example | Root Cause Pattern | Trace Fields That Diagnose It |
|---|----------|------------------|--------------------|-------------------------------|
| 2.1 | **Mask too strict — valid software address rejected** | Software writes `OUT (C),A` with `BC=0x7FF9` (A2=0) intending to page memory via `#7FFD`. Pentagon128's mask requires A2=1, so the write falls to unmapped | Decoder mask checks bits the real hardware ignores; software uses a non-canonical but valid alias | `rawPort=0x7FF9`, `decodedPort=0x0000`, `deviceId=None`, `hadHandler=false`. The critical clue: `rawPort` has A15=0 and A1=0 (matches real 7FFD) but A2=0 (fails emulator's extra check). The `decodeRuleIndex=0xFF` (no rule matched) confirms the rejection |
| 2.2 | **`OUT (n),A` high-byte mismatch** | Z80 `OUT (n),A` places register `A` on address lines A8-A15. If A=0x3F, the full port address is `0x3FFD` for `#FFFD`. Mask `0xC002` requires A15=1,A14=1 — but `0x3F` has A15=0,A14=0. Port goes unmapped | Software uses `OUT (n),A` form where `A` value doesn't set the required high-byte bits. Very common in old/compact code | `rawPort=0x3FFD`, `decodedPort=0x0000`. The `rawPort` shows A1=0 (like FFFD) but high byte comes from `A` register, not from the canonical `0xFF`. On real hardware A15/A14 may be don't-cares for this port on some clone boards |
| 2.3 | **Cross-clone decode strictness** | Demo runs on real Pentagon (which doesn't decode A2 for 7FFD) but fails on emulator (which does). Same code runs fine on Spectrum128 model because that model's `IsPort_7FFD()` uses the same strict mask | Real hardware has fewer comparator gates and ignores more bits than the emulator's mask table | Capture traces on two models for the same program. `rawPort` values that decode on Model A but go to `decodedPort=0x0000` on Model B reveal strictness divergence. The `decodeRuleIndex` shows which rule caught it on the working model |
| 2.4 | **COVOX mask too strict for clone variant** | COVOX port `#FB` mask is `0x00F5` (checks bits 7-4, 2, 0). A ZX-Profi clone variant uses a simpler 2-bit decode for COVOX. Software targeting that clone sends data to `0x00BB` (bits 7-4 = `1011`, not `1111`) — doesn't match | Different hardware clones use different decode strictness for the same logical port | `rawPort=0x00BB`, `decodedPort=0x0000`, `deviceId=None`. Compare against expected device `Covox`. The `rawPort` bit pattern shows it would match a 2-bit mask but not the 6-bit mask |
| 2.5 | **Intentional sloppy addressing in size-optimized code** | A 256-byte intro uses `LD BC,0xFEFE; OUT (C),A` intending to set the border via port `#FE` (A0=0, correct) but then falls through to use the same `BC` for `OUT (C),D` targeting `#FFFD`. The `0xFE` low byte has A1=1, which fails the AY mask (requires A1=0) | Size-optimized code reuses register values across multiple port writes, relying on partial decoding to hit different devices | Trace shows `rawPort=0xFEFE`, `decodedPort=0x00FE` for the border write (A0=0, correct). Then `rawPort=0xFEFE`, `decodedPort=0x0000` for the intended AY write — A1=1 fails the FFFD mask `0xC002`. On real hardware the AY might still respond if its comparator only checks A15,A14 |
| 2.6 | **Relaxed addressing works on one model, fails on another** | Program uses `OUT (0xFFF9),A` for memory paging. Works on Spectrum128 model (if Spectrum128 `IsPort_7FFD()` doesn't check A2). Switch to Pentagon128 model — paging breaks because Pentagon128 added the A2 guard | Models have different mask strictness for the same logical port, and software exercises the difference | Run same program on both models. Trace on Pentagon shows `rawPort=0xFFF9`, `decodedPort=0x0000`; trace on Spectrum128 shows `rawPort=0xFFF9`, `decodedPort=0x7FFD`. The cross-model diff on `decodedPort` for same `rawPort` is the smoking gun |

#### Diagnostic workflow: "Software works on real hardware but not in emulator"

1. **Arm trace**: `preset all` (capture everything, no filters)
2. **Run**: Load the failing program, let it execute until the failure point
3. **Dump trace**: Filter `decodedPort=0x0000` (unmapped)
4. **For each unmapped event**:
   - Examine `rawPort`: does the bit pattern suggest it was *intended* for a known device?
   - Test manually: `(rawPort & known_device_mask) == known_device_match` — would a **less strict** mask accept it?
   - Check `pc`: is this address in the main program, or in a well-known ROM/library routine?
5. **If unmapped events show addresses that "almost match" a known port**: the mask is too strict for this software
6. **Fix**: either relax the mask (risk: may introduce collision 1.1) or add a per-model strictness flag

#### The porttrace_convert.py can automate this analysis:

```bash
# Find all unmapped port accesses and test them against relaxed masks
python tools/porttrace/porttrace_convert.py trace.json --filter-unmapped --to text

# Future enhancement: --analyze-strictness flag
python tools/porttrace/porttrace_convert.py trace.json --analyze-strictness
```

Output:
```
Strictness Analysis: 16 unmapped events
═══════════════════════════════════════════
WARNING: 8 unmapped events may be strict-decode rejects:

  rawPort=0x7FF9 (×4) — near-miss for #7FFD
    Bits checked by mask 0x8006: A15=0 ✓, A1=0 ✓, A2=0 ✗ (mask requires 1)
    → Would decode if A2 bit removed from mask
    → Likely intent: memory paging

  rawPort=0xFEFE (×4) — near-miss for #FFFD
    Bits checked by mask 0xC002: A15=1 ✓, A14=1 ✓, A1=1 ✗ (mask requires 0)
    → Would decode if A1 bit removed from mask
    → Likely intent: AY register select

  8 remaining unmapped events appear genuinely unmapped (no near-miss)
```

### Category 3: Peripheral Behavior Errors (Sound)

These bugs are caused by the right port being decoded but the peripheral device receiving incorrect or misordered data.

| # | Use Case | Concrete Example | Root Cause Pattern | Trace Fields That Diagnose It |
|---|----------|------------------|--------------------|-------------------------------|
| 3.1 | **TurboSound AY1 silent — chip-select dropped** | `OUT #FFFD, 0xFE` should select AY1 (secondary) but decoder routes to wrong port or skips it | Port alias (e.g., `0xFEFD`) not decoded as `#FFFD`; chip-select write never reaches TurboSound | Trace shows no `(decodedPort=0xFFFD, value=0xFE, direction=OUT)` event; or shows it decoded as something else. `deviceId` confirms whether TurboSound instance received it |
| 3.2 | **TurboSound data goes to wrong chip** | After chip-select, `OUT #BFFD, data` programs the wrong AY | Chip-select is a TurboSound-internal state; but if an intervening `OUT #FFFD, 0xFF` resets to chip0 before the data write, data goes to wrong chip | Ordered sequence: look for `(FFFD, 0xFE)` → `(FFFD, 0x08)` → `(BFFD, 0x20)`. If there's an unexpected `(FFFD, 0xFF)` between steps 2 and 3, chip got re-selected |
| 3.3 | **AY register readback broken** | `IN #FFFD` returns `0xFF` instead of the programmed register value | Model-specific `DecodePortIn` doesn't recognize `#FFFD` mirrors (Spectrum128 had this: port `0xFF05` wasn't decoded as FFFD for IN) | `rawPort=0xFF05`, `decodedPort` — should show `0xFFFD` but if it shows `0x0000` or `0x00FE`, the IN path doesn't resolve mirrors |
| 3.4 | **COVOX/SOUNDRIVE silent on one channel** | SOUNDRIVE channel `#F1` works but `#F9` is silent | `#F9` decodes to COVOX handler but the handler receives wrong port argument | `rawPort=0x00F9`, `decodedPort=0x00FB`, `deviceId=Covox` — if `decodedPort` is wrong or `deviceId=None`, the channel is broken |
| 3.5 | **AY tone frequency wrong** | Music plays at wrong pitch | Register write sequence to fine/coarse tune registers (R0/R1) is misordered or a write is lost | Ordered trace filtered on `decodedPort ∈ {0xFFFD, 0xBFFD}` — verify `(FFFD, regNum)` precedes each `(BFFD, data)` and no writes are missing |

### Category 4: Peripheral Behavior Errors (Disk)

| # | Use Case | Concrete Example | Root Cause Pattern | Trace Fields That Diagnose It |
|---|----------|------------------|--------------------|-------------------------------|
| 4.1 | **Ghost Byte — double read on stateful register** | WD1793 Data Register `#7F` read twice per Z80 instruction; FDC buffer drains at 2× speed | Base class `DecodePortIn` performs a hardware read before subclass does its own | Two consecutive trace events with same `decodedPort=0x007F`, `direction=IN`, T-state gap ≈ 11 cycles. `timestamp` diff reveals the double-read. ⚠️ **Prerequisite**: the legacy base-class path calls `PeripheralPortIn` without the completion hook, so the second read is invisible unless that path is retired or instrumented (`viaLegacyBasePath`, flags bit 6) — see `implementation_plan.md` |
| 4.2 | **BetaDisk stops working after model change** | Switch from Pentagon128 to Spectrum128; FDC ports no longer respond | New model's `decodePort()` table doesn't include BDI port entries, or doesn't call `PeripheralPortIn` for those ports | Trace for `rawPort ∈ {0x001F..0x00FF}` shows `deviceId=None` or `hadHandler=false` on the new model |
| 4.3 | **FDC command not accepted** | `OUT #1F, 0x88` (Read Sector) goes to FDC but disk doesn't spin | Port correctly decoded and handler called, but the value doesn't reach the FDC's command register | `decodedPort=0x001F`, `value=0x88`, `deviceId=WD1793_Status`, `hadHandler=true` — if all correct, bug is inside FDC not in port layer |
| 4.4 | **DRQ/INTRQ polling returns wrong status** | `IN #FF` always reads 0xFF; TR-DOS hangs in polling loop | Beta128 system register `#FF` gate is incorrectly blocking reads, or handler isn't registered | Trace shows `rawPort=0x00FF`, `wasBeta128Gated=true` when it shouldn't be; or `deviceId=None` meaning handler not registered |
| 4.5 | **FDC port mirrors collide with other devices** | `IN 0x01FF` should resolve to Beta128 `#FF` but decodes to something else | Partial decoding alias `0x01FF` matches a broader rule first | `rawPort=0x01FF`, `decodeRuleIndex` — shows which rule caught it |
| 4.6 | **Sequencing hazard — state advances before read** | FDC data register returns "next byte" not "current byte" | `portDeviceInMethod` calls `process()` before returning register value | Two consecutive reads of `decodedPort=0x007F` show value sequence that's off-by-one vs expected sector data. Combined with `timestamp` to verify timing |

### Category 5: Memory Paging / System Ports

| # | Use Case | Concrete Example | Root Cause Pattern | Trace Fields That Diagnose It |
|---|----------|------------------|--------------------|-------------------------------|
| 5.1 | **Paging port locked when it shouldn't be** | Program writes to `#7FFD` but memory doesn't switch | `_7FFD_Locked` latch set from previous write with bit 5; not cleared on reset | Trace shows `decodedPort=0x7FFD`, `direction=OUT`, `value=0x04`, `wasHandledInline=true` — value was delivered, so bug is in the handler (lock state). Trace combined with emulatorState snapshot |
| 5.2 | **Paging port responds on wrong addresses** | `OUT (0x00F1), value` inadvertently triggers 7FFD handler because mask doesn't exclude bit 2 | Mask collision (identical to 1.1 but with system impact) | `rawPort=0x00F1`, `decodedPort=0x7FFD`, `decodeRuleIndex=2` — proves the SOUNDRIVE port triggered the paging logic |
| 5.3 | **Border color stuck** | Writing to `#FE` has no visual effect | Port decoded correctly but value not reaching ULA/Screen | `decodedPort=0x00FE`, `value`, `deviceId=ULA_FE`, `wasHandledInline=true` — confirms port layer is working; bug is downstream |
| 5.4 | **ROM page readback mismatch** | `IN #7FFD` returns wrong value after snapshot load | Snapshot loader wrote `p7FFD` in emulatorState but didn't call `Port_7FFD_Out` to apply it | `decodedPort=0x7FFD`, `direction=IN`, `value` — compare with `emulatorState.p7FFD` |

### Category 6: Architectural / Integration Issues

| # | Use Case | Concrete Example | Root Cause Pattern | Trace Fields That Diagnose It |
|---|----------|------------------|--------------------|-------------------------------|
| 6.1 | **Handler registration race** | TurboSound `attachToPorts()` fails silently; AY ports have no handler | `RegisterPortHandler` returns false (port already registered by a previous AY instance) | Trace shows `deviceId=None` for `decodedPort=0xFFFD`. Combined with startup log checking `attachToPorts()` return value |
| 6.2 | **Handler for wrong port** | Covox registered on `PORT_RIGHT_B` (0xFB) but SOUNDRIVE sends to `0xF1` which decodes to `0xFB` | Correct by design — all 4 SOUNDRIVE channels decode to single handler port | Trace confirms: `rawPort=0x00F1`, `decodedPort=0x00FB`, `deviceId=Covox`, `hadHandler=true` — working as intended |
| 6.3 | **OnPortInComplete receives raw vs decoded port** | Port breakpoints set on raw address `0x00` don't fire because `OnPortInComplete` receives decoded `0xFE` | API contract mismatch: user sets breakpoint on raw; handler receives decoded | `rawPort` vs `decodedPort` in trace — reveals which was passed to breakpoint system |
| 6.4 | **Inline vs peripheral handler inconsistency** | Spectrum128 handles `#BFFD` inline via `Port_BFFD_Out()` calling `PeripheralPortOut(0xBFFD, value)`, but Pentagon128 routes it entirely through `PeripheralPortOut` via the `default:` case | Different code paths for same hardware port across models | `wasHandledInline` flag differs between models for same `decodedPort` |
| 6.5 | **TTD journal missing IN events** | Time-travel replay of IO shows writes but not reads | `OnPortOutComplete` has TTD hook; `OnPortInComplete` doesn't | Trace shows IN events in PDR but they're absent from TTD journal — confirms the asymmetry |

---

## Field-to-Use-Case Coverage Matrix

This matrix shows which trace fields are essential (●), useful (○), or not needed (·) for each use-case category:

| Field | 1. Decode Errors | 2. Strict Decode | 3. Sound Periph | 4. Disk Periph | 5. System Ports | 6. Integration |
|-------|:---:|:---:|:---:|:---:|:---:|:---:|
| `timestamp` | ○ | ○ | ● | ● | ○ | ○ |
| `frameNumber` | · | · | ○ | ○ | · | · |
| `rawPort` | ● | ● | ● | ● | ● | ● |
| `decodedPort` | ● | ● | ● | ● | ● | ● |
| `decodeRuleIndex` | ● | ● | ○ | ○ | ● | · |
| `value` | · | ○ | ● | ● | ● | ○ |
| `pc` | ○ | ● | ○ | ● | ○ | ○ |
| `deviceId` | ● | ● | ● | ● | ● | ● |
| `direction` | ○ | ○ | ● | ● | ● | ● |
| `wasDecoded` | ● | ● | ○ | ● | ○ | ○ |
| `hadHandler` | ● | ● | ● | ● | ○ | ● |
| `wasBeta128Gated` | ● | · | · | ● | · | · |
| `wasHandledInline` | ○ | · | ○ | · | ● | ● |

**All 13 fields are essential for at least one use-case category.** No field can be removed without losing coverage for a real diagnostic scenario.

> **Note on Category 2 (Strict Decode)**: `rawPort` and `decodeRuleIndex` are the most critical fields here. The raw port reveals what the software actually put on the bus, while `decodeRuleIndex=0xFF` (no match) combined with `decodedPort=0x0000` is the signature of a strict-decode reject. The `pc` field is essential for Category 2 because it identifies whether the "sloppy" addressing comes from the program itself or from a known ROM routine — ROM routines always use canonical addresses, so a strict-decode reject at a ROM PC means the mask is almost certainly wrong.

---

## Diagnostic Workflows (Step-by-Step)

### Workflow A: "Why is TurboSound AY1 silent?"

1. **Arm trace**: `watchPort(0xFFFD)`, `watchPort(0xBFFD)`
2. **Run**: Play music that uses both AY chips for ~2 seconds
3. **Dump trace**: Filter events ordered by `timestamp`
4. **Check chip-select sequence**: Look for `(decodedPort=0xFFFD, direction=OUT, value=0xFE)` — this selects AY1
5. **If missing**: The music player never sent the chip-select. Not an emulator bug.
6. **If present**: Check that `deviceId=AY_FFFD` (TurboSound received it). Look at subsequent `(decodedPort=0xBFFD, direction=OUT)` events — verify `deviceId=AY_BFFD`.
7. **If `deviceId=None` for BFFD**: Handler not registered. Check `attachToPorts()` return value.
8. **If both correct but AY1 still silent**: Bug is inside TurboSound's internal `_currentChip` routing — trace proves port layer is clean.

### Workflow B: "BetaDisk broke after changing port decoder"

1. **Arm trace**: `watchPortRange(0x0000, 0x00FF)` to capture all low-byte ports
2. **Run**: Boot into TR-DOS (or attempt disk operation)
3. **Dump trace**: Filter `decodedPort ∈ {0x001F, 0x003F, 0x005F, 0x007F, 0x00FF}`
4. **If zero events**: No FDC ports are being decoded. Check `wasBeta128Gated` — if true everywhere, the `CF_TRDOS` flag isn't being set.
5. **If events exist but `hadHandler=false`**: WD1793 didn't register its ports. Check `attachToPorts()`.
6. **If events exist with `hadHandler=true` but disk doesn't work**: Bug is inside WD1793 handler, not in the port layer.

### Workflow C: "Ghost Byte — is the FDC being double-read?"

1. **Arm trace**: `watchPort(0x007F)` (FDC data register)
2. **Run**: Execute a sector read (256 bytes)
3. **Dump trace**: Count `direction=IN` events
4. **If count > 256**: Ghost reads detected. Examine `timestamp` gaps — two reads within ~11 T-states of each other are architectural reentrancy.
5. **Look at `pc`**: If both reads come from the same PC, it's a single-instruction double-dispatch. If different PCs, it's a legitimate software double-read (e.g., CRC drain).

### Workflow D: "SOUNDRIVE port F1 triggers memory paging"

1. **Arm trace**: `watchPort(0x00F1)`
2. **Run**: Play SOUNDRIVE music
3. **Dump trace**: Check `decodedPort`
4. **If `decodedPort=0x7FFD`**: Mask collision. Check `decodeRuleIndex` — it'll show rule #2 (7FFD rule) fired instead of rule #4 (COVOX). The mask needs a `bit2=1` requirement.
5. **If `decodedPort=0x00FB`**: Correct decode. Check `deviceId=Covox`. If `deviceId=None`, handler registration failed.

---

## Relationship to Existing Logging

```
                ┌──────────────────────────────────────────────────────────────────┐
                │                   What answers what                              │
                ├──────────────────┬───────────────────────────┬───────────────────┤
                │  ModuleLogger    │  Port Access Trace (PDR)  │  MemAccessTracker │
                │                  │                           │                   │
  "What         │  ✗ Text only,    │  ● rawPort, decodedPort,  │  ✗ No per-event   │
  happened?"    │    needs grep    │    value, deviceId, flags │    detail         │
                │                  │    - structured, ordered  │                   │
                │                  │                           │                   │
  "When?"       │  ✗ No timestamp  │  ● T-state precision      │  ✗ No timestamp   │
                │    (wall-clock   │    (cycle-accurate)       │                   │
                │     at best)     │                           │                   │
                │                  │                           │                   │
  "Who?"        │  ○ Submodule tag │  ● deviceId enum          │  ✗ Not tracked    │
                │    (human text)  │    (machine-parseable)    │                   │
                │                  │                           │                   │
  "Why?"        │  ○ Debug strings │  ● decodeRuleIndex,       │  ✗ Counters only  │
                │    (verbose)     │    wasBeta128Gated,       │                   │
                │                  │    wasHandledInline       │                   │
                │                  │                           │                   │
  "How many?"   │  ✗ Unbounded     │  ○ Ring buffer (finite)   │  ● Aggregate      │
                │    output        │                           │    counters       │
                │                  │                           │                   │
  Performance   │  SLOW            │  FAST                     │  FASTEST          │
  impact        │  (string fmt)    │  (POD struct copy)        │  (counter incr)   │
                └──────────────────┴───────────────────────────┴───────────────────┘
```

The Port Access Trace is the **missing middle layer**: structured enough to answer "what/when/who/why" without parsing text, fast enough to leave on during interactive debugging sessions, and finite enough (ring buffer) to not fill memory.
