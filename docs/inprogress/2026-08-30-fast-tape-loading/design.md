# Fast Tape Loading (Tape Traps) — Design

| | |
|---|---|
| **Status** | r3 — implemented; control plane migrated to the feature system |
| **Revision** | r0 2026-08-30 initial draft · r1 2026-08-30 post-review: entry contract fixed (`Fc`: 1=LOAD / 0=VERIFY; SAVE is SA-BYTES `$04C2`), LD-FLAG/LD-VERIFY labeling corrected, 128K ROM note verified, image-load idempotency + partial-block cursor semantics defined, differential-test exclusions made concrete · r2 2026-08-30 post-implementation: IFF1 exit corrected to **preserved** (SA/LD-RET `$053F` listing added — its `EI` at `$054F` is the caller-observable exit), lazy arm state replaces the cached `_armed`/`UpdateArmState()` design, consumption cursor is the pre-existing `_currentTapeBlockIndex` (TTD blob **unchanged**), `EnsureImageLoaded()` records **no** external-event marker, exact success exit `A=$00`/`F=$BF` documented, coverage-sample item recorded as a v1 deviation · r3 2026-08-30 feature-system migration: the switch is now the runtime FeatureManager feature **`fasttape`** (alias `ftape`, category performance, default **on**) — `CONFIG.tape_traps` and the `[MISC] TapeTraps` ini key are **removed**; CLI `setting fast_tape`, WebAPI `settings.fast_tape` and the Qt Machine menu toggle all drive `FeatureManager::setFeature` — one control plane, same idiom as `sound`/`screenhq` |
| **Date** | 2026-08-30 |
| **Feature** | Switchable fast tape loading via ROM loader hooks |
| **Affects** | `core/src/emulator/cpu/{z80,core}.{h,cpp}`, `core/src/emulator/io/tape/`, `core/src/base/featuremanager.*`, `core/automation/cli/src/commands/cli-processor-settings.cpp`, `core/automation/webapi/src/api/settings_api.cpp`, `unreal-qt/src/{menumanager,mainwindow}.{h,cpp}`, `core/tests/emulator/io/tape/` |
| **Related** | [TAP format](../../../core/src/loaders/tape/loader_tap.h), [TR-DOS ROM traps](../../../core/src/emulator/spectrumconstants.h) (`ROMSwitch` namespace) |

## 1. Goals

1. **Instant tape loading.** When enabled, vanilla ROM-driven tape loads (`LOAD ""`, `LOAD "name"`, `LOAD CODE`, `LOAD SCREEN$`) complete in near-zero emulated time by bypassing signal decoding entirely: block payloads are copied from the tape image directly into Z80 memory.
2. **Switchable.** A single runtime switch — the FeatureManager feature `fasttape` (persisted in `features.ini`, exposed as `fast_tape` in the CLI/WebAPI settings surfaces and as Machine → Fast Tape Loading in Qt) controls the feature. Default: **on**.
3. **Safe fallback.** Whenever fast loading cannot be applied — custom loaders, non-standard block flags, checksum errors, length mismatches, no tape image — the emulator falls back to the existing full signal emulation path, **without restarting the load** and **without losing tape position**.
4. **TTD-transparent.** Fast loads are deterministic host-side actions. They must remain compatible with Time-Travel Debugging (seek/replay must reproduce identical state) without adding replay barriers.

## 2. Non-goals (v1)

- **Fast VERIFY.** `VERIFY` continues through signal emulation (rarely used; requires byte-compare emulation).
- **Fast SAVE.** Out of scope (tape output is not implemented).
- **TZX fast loading.** The current `LoaderTZX` only validates/parses hardware info; until it produces `TapeBlock` sequences, only TAP images are fast-loadable. The design is deliberately format-agnostic (it consumes `TapeBlock`s), so TZX standard blocks (`$10`/`$11`) plug in later with no trap-side changes.
- **Loading border stripes / loading sounds** in fast mode (inherently skipped — this is the point of the feature).

## 3. Background — current tape pipeline

| Component | Role |
|---|---|
| `core/src/emulator/io/tape/tape.h/.cpp` | `Tape` class: owns parsed `TapeBlock` vector, playback cursor, EAR bitstream generation (`generateBitstream`), port-IN dispatch, TTD cursor serialization |
| `core/src/loaders/tape/loader_tap.h/.cpp` | `LoaderTAP`: parses `.tap` files into `std::vector<TapeBlock>`; block validity via XOR checksum (`isBlockValid`) |
| `Tape::handlePortIn()` | **Auto-start hack**: when tape is not playing and `cpu.pc == 0x0564` (the `RRA` after `IN A,($FE)` at `$0562` inside the ROM loader loop), lazily loads the TAP file and calls `startTape()` |
| `Tape::handleFrameEnd()` | Load-completion watchdogs: `ERR_NR` change and 150-frames-without-EAR-read both stop the tape |
| ~~`CONFIG.tape_traps`~~ (`platform.h`) | **Historical (r0–r2)**: heritage flag, read/written by CLI (`fast_tape`) and WebAPI settings; dormant until r2 wired it into the trap. **Removed in r3** — superseded by the `fasttape` feature (§10) |

Tape playback is bit-accurate: pilot/sync/data pulses are expanded into `edgePulseTimings` and delivered through the EAR bit on port `#FE` reads, so the real ROM `LD-BYTES` routine decodes them at real speed (minutes for large programs).

## 4. ZX ROM analysis — the `LD-BYTES` contract

The hook point is the standard ROM tape load/verify routine at `$0556`. **SAVE never enters it** — the save path is SA-BYTES at `$04C2` (verified in `data/rom/48.rom`: bytes `21 3F 05 E5` = `LD HL,$053F; PUSH HL`); the two routines share only the border-restore return at `$053F` (SA/LD-RET).

Verified in-tree: `data/rom/48.rom` carries the routine at `$0556`; in `data/rom/128.rom` the first 16K bank (128K editor ROM) contains **string data** at `$0556` — no loader — while the second bank (the 48K-compatible ROM) contains the byte-identical routine. Pentagon/Scorpion SOS-derived ROMs are 48K-ROM-based and contain it.

### 4.1 Verified ROM bytes (`data/rom/48.rom`, offset `$053F`)

```asm
      ; SA/LD-RET $053F — shared epilogue of LD-BYTES and SA-BYTES; LD-BYTES
      ; pushes it as a return sentinel at $0561 (r2: listing verified in rom)
053F  F5          PUSH AF                 ; save flags for the restore below
0540  3A 48 5C    LD   A,($5C48)          ; BORDCR
0543  E6 38       AND  $38
0545  0F          RRCA                    ; border bits -> 0-2
0546  0F          RRCA
0547  0F          RRCA
0548  D3 FE       OUT  ($FE),A            ; restore border
054A  3E 7F       LD   A,$7F
054C  DB FE       IN   A,($FE)            ; SPACE row
054E  1F          RRA                     ; SPACE bit -> carry
054F  FB          EI                      ; interrupts re-enabled on EVERY path
0550  38 02       JR   C,$0554            ; SPACE not pressed -> return
0552  CF          RST  $08
0553  0C          ; report code 0Ch ('BREAK - CONT repeats')
0554  F1          POP  AF
0555  C9          RET
0556  14          INC  D                  ; ┐ LD-BYTES entry (LOAD/VERIFY only;
0557  08          EX   AF,AF'             ; │ save entry flags
0558  15          DEC  D                  ; ┘
0559  F3          DI                      ; interrupts stay OFF after return
055A  3E 0F       LD   A,$0F
055C  D3 FE       OUT  ($FE),A            ; border white, MIC off
055E  21 3F 05    LD   HL,$053F
0561  E5          PUSH HL                 ; sentinel: SA/LD-RET $053F (shared with SA-BYTES)
0562  DB FE       IN   A,($FE)            ; ┐ EAR poll (LD-BREAK-1)
0564  1F          RRA                     ; │ — already used by auto-start hack
0565  E6 20       AND  $20                ; ┘
0567  F6 02       OR   $02
0569  4F          LD   C,A
056A  BF          CP   A                  ; clears carry
056B  C0          RET  NZ                 ; never taken (timing padding)
056C  CD E7 05    CALL $05E7              ; BREAK-KEY check
056F  30 FA       JR   NC,$056B
      ...         ; pilot/sync detection, break-out delay loops (elided)
05A9  08          EX   AF,AF'             ; restore stashed flags (Z-relay)
05AA  20 07       JR   NZ,$05B3           ; first byte → flag check (internal)
05AC  30 0F       JR   NC,$05BD           ; Fc=0 → LD-VERIFY: compare path
05AE  DD 75 00    LD   (IX+$00),L         ; Fc=1 → LD: store received byte
05B1  18 0F       JR   $05C2
05B3  CB 11       RL   C                  ; LD-FLAG: the first received byte
05B5  AD          XOR  L                 ; is the flag — compare against the
05B6  C0          RET  NZ                ; expected flag in A; mismatch → Fc=0
05B7  79          LD   A,C
05B8  1F          RRA
05B9  4F          LD   C,A
05BA  13          INC  DE                ; flag byte is outside the DE count
05BB  18 07       JR   $05C4
05BD  DD 7E 00    LD   A,(IX+$00)         ; LD-VERIFY: fetch memory byte
05C0  AD          XOR  L                 ; and compare with received byte;
05C1  C0          RET  NZ                ; mismatch → Fc=0 error return
05C2  DD 23       INC  IX                 ; byte loop tail
05C4  1B          DEC  DE
05C5  08          EX   AF,AF'
05C6  06 B2       LD   B,$B2              ; bit timing
05C8  2E 01       LD   L,$01              ; byte accumulator seed
05CA  CD E3 05    CALL $05E3              ; edge detector (LD-EDGE-2)
05CD  D0          RET  NC                 ; timeout / break → Fc=0 error
05CE  3E CB       LD   A,$CB
05D0  B8          CP   B                  ; edge-time tolerance check
05D1  CB 15       RL   L                  ; received bit → accumulator
05D3  06 B0       LD   B,$B0
05D5  D2 CA 05    JP   NC,$05CA           ; byte incomplete → sample again
05D8  7C          LD   A,H
05D9  AD          XOR  L                  ; running checksum (incl. flag byte)
05DA  67          LD   H,A
05DB  7A          LD   A,D
05DC  B3          OR   E                  ; DE == 0 → last byte received
05DD  20 CA       JR   NZ,$05A9           ; → next byte
05DF  7C          LD   A,H                ; final checksum test
05E0  FE 01       CP   $01                ; (leaves A = checksum accumulator)
05E2  C9          RET                     ; Fc=1 success, Fc=0 error
```

### 4.2 Entry contract (per *The Complete Spectrum ROM Disassembly*, cross-checked against bytes above)

| Item | Meaning |
|---|---|
| Entry address | `$0556` (`LD-BYTES`; SAVE is SA-BYTES at `$04C2` and never enters here) |
| `A` | Expected block flag: `$00` header, `$FF` data (custom callers may use other values) |
| `Fc` | **`1` = LOAD (store bytes), `0` = VERIFY (compare only)** — the sole entry discriminator |
| `Z` | Not part of the entry contract — consumed internally by the Z-relay at `$05AA` to route the flag byte vs. data bytes; entry state is whatever the caller left |
| `IX` | Destination address |
| `DE` | Byte count (17 for headers) |

### 4.3 Exit contract

| Item | Value on success |
|---|---|
| `Fc` | `1` |
| `IX` | Advanced by `DE` (incremented per stored byte at `$05C2`) |
| `DE` | `0` (decremented per byte at `$05C4`) |
| `A`, `F` | On success `A = $00` (the checksum accumulator — total XOR of a valid block) and `F = $BF`: the exact result word of `CP $01` with `A = 0` (`S Z Y H X PV N C = 1 0 1 1 1 1 1 1`); carry set = success (r2) |
| `B`, `C`, `L`, `H` | Clobbered by the routine; callers must not rely on them (r2: the trap leaves them as-is — the differential test §12.2-2 proves the ROM flow does not depend on their values) |
| `IFF1` | **Unchanged (r2 correction)** — the invocation as the caller observes it returns *through* SA/LD-RET, whose `EI` at `$054F` runs on every path after the routine's `DI` at `$0559`; net observable effect: interrupts enabled. The trap skips both instructions and preserves `IFF1` — identical outcome |
| Return | `RET` at `$05E2` pops the stacked `$053F` sentinel; SA/LD-RET's `RET` at `$0555` returns to the caller address |

### 4.4 TAP block layout (as produced by `LoaderTAP`)

```text
TapeBlock.data = [ flag (1) | payload (N) | checksum (1) ]
header: flag=$00, N=17   →  [type(1)][filename(10)][length(2)][param1(2)][param2(2)][checksum(1)]
data:   flag=$FF, N=DE   →  raw bytes
Block is valid iff XOR of ALL data bytes (flag…checksum) == 0
```

## 5. Design overview

```text
                       Z80::Z80Step()  (pre-fetch, emulator thread)
                              │
                              ▼
              pc == $0556 && fastLoad armed?  ──── no ───► normal fetch/execute
                              │ yes                              │
                              ▼                                  ▼
                 TapeFastLoad::HandleLDBytesTrap(cpu)      ROM LD-BYTES runs for real
                       │                        │                 │
              entry contract ok?         decline (no mutation)    │
                block matches?                  │                 ▼
                       │                        └──────► EAR signal fallback starts
                copy payload → memory                  at the SAME tape cursor
                emulate exit state
                advance tape cursor by 1 block
                       │
                       ▼
              return true (Z80Step returns; trap replaced the routine)
```

Core idea: the trap **replaces the entire `LD-BYTES` invocation** at its first opcode fetch, performing the routine's memory side effects and register postconditions directly. A decline is completely inert — the CPU proceeds into the real ROM code and the existing signal pipeline takes over.

## 6. Engagement rules and decline matrix

### 6.1 Arm state (cheap per-step predicate)

The per-step cost must be a single predictable branch when the feature is off. **r2: arm state is evaluated lazily, not cached** — `IsArmed()` runs only when `pc == $0556` is already established (never on the per-step hot path), so feature toggles take effect on the very next invocation with no event plumbing and no cache invalidation:

```
IsArmed() = 'fasttape' feature enabled          (FeatureManager live lookup;
                                               null manager = never armed)
      && CF_TRDOS clear                        (bank 0 is not the DOS ROM)
      && ROM signature check on the bank ACTUALLY mapped at Z80 $0000-$3FFF:
         bytes at $0556      == 14 08 15 F3 3E 0F D3 FE  (INC D/EX AF,AF'/DEC D/DI/LD A,$0F/OUT ($FE),A)
         bytes at $0556 + 12 == DB FE 1F                   (IN A,($FE)/RRA at $0562)
```

Note the second signature's **+12 offset** (r2): `LD HL,$053F / PUSH HL` ($055E-$0561) sits between the prologue and the EAR poll — an implementation slip that compared at +6 never fired on real ROMs and was caught by the §12.1-6 arm-state test. The signature check protects against non-standard ROMs and against the TR-DOS ROM being paged into bank 0 (TR-DOS bytes at `$0556` differ).

**Why byte-signature, not model/page logic (128K case, verified)**: in `data/rom/128.rom` the 128K editor ROM has string data at `$0556` — no loader — while the 48K-compatible bank carries the byte-identical routine. Checking the bytes of whatever bank is *actually mapped* at Z80 `$0000-$3FFF` therefore handles 128K ROM paging, Pentagon/Scorpion SOS derivatives and TR-DOS paging with zero per-model logic: armed ⇔ the mapped bank really contains `LD-BYTES`.

### 6.2 Per-invocation decline matrix

The trap is consulted only when armed and `pc == $0556`. Each row is checked in order; **any failure = decline** (return `false`, zero side effects):

| # | Condition | Outcome |
|---|---|---|
| 1 | `Fc == 0` (VERIFY) | Decline → authentic signal emulation (byte-compare at real speed) |
| 2 | `DE == 0` | Decline (degenerate call; real behavior is pilot-wait) |
| 3 | No tape image loaded and image cannot be loaded (`coreState.tapeFilePath` empty / unreadable) | Decline |
| 4 | Cursor at end of `_tapeBlocks` | Decline |
| 5 | Signal playback currently active (`_tapeStarted`) | Decline (cursor is mid-stream; avoids double consumption) |
| 6 | Next block flag byte != `A` (expected flag) | Decline — covers custom-flag blocks and header/data sequence breaks |
| 7 | Next block checksum invalid (XOR != 0) | Decline → signal path reproduces the authentic "R Tape loading error" |
| 8 | Block payload length != `DE` (17 implied for header flag `$00`) | Decline |

Rows 6–8 implement the **vanilla-tape-only** requirement: pure ROM-loadable images (header/data pairs with standard flags, valid checksums, exact lengths) fast-load; everything else — headerless blocks, custom speeds (TZX turbo blocks once supported), mixed custom loaders — falls back to full emulation automatically.

### 6.3 Consume semantics on success

- One trap invocation consumes **exactly one block** (the header call consumes the header block; the subsequent data call consumes the data block). This mirrors real `LD-BYTES` granularity and keeps `LOAD "name"` header-scanning loops correct: each rejected header consumes one block and the ROM retries — identical to a real tape being skipped.
- Memory side effect: `payload[0..DE)` written to Z80 address space starting at `IX`, wrapping at 64K, through the **debug-visible memory write path** (see §9.3).
- Tape cursor advances past the consumed block.
- **Fallback stickiness**: once a decline starts signal playback (row 5), fast loading stays off until playback ends — subsequent blocks, vanilla or not, load at real speed until a watchdog or explicit stop fires. This is acceptable because post-fallback blocks almost always belong to the custom loader; the trap re-arms for the next load. One deliberate consequence: a tape whose *first* block is non-vanilla plays entirely at real speed even if later blocks are vanilla.

## 7. CPU postcondition emulation (trap success path)

The trap simulates the `RET` at `$05E2` plus the routine's documented effects:

```cpp
// Pseudocode — final C++ in TapeFastLoad::HandleLDBytesTrap()
const uint16_t returnAddr = Pop16();          // RET semantics: pc <- (sp), sp += 2
WritePayloadToMemory(IX, payload, DE);        // hooked write path, Z80 view, 64K wrap
cpu.ix += DE;
cpu.de = 0;
cpu.a = 0x00; cpu.f = 0xBF;                   // exact CP $01 result word (§4.3) — carry set = success
// B, C, H, L: left as-is (ROM clobbers them; the differential test §12.2-2
// proves the ROM flow does not depend on any of them)
// IFF1: untouched — the trap skips both the DI at $0559 and SA/LD-RET's EI
// at $054F; callers observe interrupts enabled either way (§7.1)
cpu.pc = returnAddr;
cpu.m1_pc = 0x0556;                           // attribute the synthetic instruction to the replaced
                                              // routine (TTD write journal + debugger traces stay coherent)
AdvanceTStates(kFastLoadTStates);             // see §8 Timing
```

### 7.1 Why `IFF1` is preserved (r2 correction)

r1 held that the routine returns with interrupts disabled and that the trap should clear `IFF1`. The caller-observable exit is the opposite: `LD-BYTES`' `RET` at `$05E2` pops the stacked `$053F` sentinel, and SA/LD-RET executes `EI` at `$054F` — on every path — before returning to the caller (§4.1 listing). ROM callers therefore always resume with interrupts enabled, including through the `RST $08` report path. The trap skips both the `DI` at `$0559` and the `EI` at `$054F` and leaves `IFF1` untouched: identical observable outcome, no synthetic state. Locked by §12.1-5 (preservation assert) and §12.2-2 (byte-exact differential).

## 8. Timing accounting

The trap skips the routine's real duration (pilot tones included). `t` is advanced by a small constant, `kFastLoadTStates = 128`, covering the emulated `CALL`-balance (entry overhead + `RET`). Consequences:

- A full screen load completes within the current frame; multi-block `LOAD ""` completes within a few frames — "almost instant" as required.
- No zero-time side effects: `Z80FrameCycle`'s `while (t < frameLimit)` loop always observes forward progress.
- `R` register: advanced by 1 (single synthetic M1) — the real routine would advance it by hundreds; `R` only matters for `IM 2` vector fetches and `LD A,R` sampling, neither of which is disturbed in practice by this approximation (documented deviation).

## 9. Integration points

### 9.1 New component — `core/src/emulator/io/tape/tapefastload.h/.cpp`

```cpp
class TapeFastLoad
{
public:
    TapeFastLoad(EmulatorContext* context, Tape& tape);

    void UpdateArmState();                    // REMOVED in r2 — see below
    bool IsArmed() const;                     // lazy, per-invocation: config + CF_TRDOS + signature (§6.1)

    // Returns true if the trap consumed the LD-BYTES invocation (Z80Step must
    // return immediately). Returns false = decline (no state was touched).
    bool HandleLDBytesTrap(Z80& cpu);

private:
    bool CheckEntryContract(const Z80& cpu) const;
    const TapeBlock* PeekNextBlock() const;
    bool BlockMatches(const TapeBlock& block, const Z80& cpu) const;  // §6.2 rows 6-8
    void ApplyLoadEffects(Z80& cpu, const TapeBlock& block);
};
```

Owned by `Emulator` next to `Tape`; pointer `EmulatorContext::pTapeFastLoad` (nullable, same pattern as `pTape`/`pBetaDisk`). A `TapeFastLoadCUT` wrapper (existing `_CODE_UNDER_TEST` pattern) exposes internals for unit tests.

### 9.2 Hook site — `Z80::Z80Step()` (`core/src/emulator/cpu/z80.cpp`)

Inserted **after** the TR-DOS ROM-switch region and **after** the breakpoint-dispatch block (so a user breakpoint at `$0556` still wins and pauses first), **before** the halted/fetch logic:

```cpp
// Fast tape loading trap — design: docs/inprogress/2026-08-30-fast-tape-loading
if (pc == ROMAddresses::LD_BYTES && _context->pTapeFastLoad != nullptr)
{
    if (_context->pTapeFastLoad->HandleLDBytesTrap(*this))
        return;  // trap replaced the whole routine (arm check is step 1 inside)
}
```

`ROMAddresses::LD_BYTES = 0x0556` is added to `spectrumconstants.h` alongside the existing `ROMSwitch` constants. Hot-path cost (r2): one `uint16_t` compare + one null check; arm evaluation (config read, TR-DOS flag, signature memcmp) happens only inside a candidate invocation, i.e. never unless `pc == $0556`.

### 9.3 Memory writes must be observable

Payload writes use the **same write API the CPU store path uses** (the variant routed through debug/TTD hooks — *not* `DirectWriteToZ80Memory`, which bypasses them). This gives us, for free:

- TTD delta capture (`TTDDirtyTracker` sees the writes → checkpoints/frames stay coherent);
- watchpoints / memory-change analyzers fire correctly;
- debugger memory views refresh.

(r2) resolved: payload stores go through `Z80::wd()` — the same dispatch every CPU store uses — and the two `RET` stack reads through `Z80::rd()` (self-accounting 3T each, like the real POPs).

### 9.4 `Tape` refactor — single consumption cursor

Today the playback cursor (`_currentTapeBlockIndex` + friends) is the only tape position concept, and image loading is buried inside the `0x0564` auto-start hack in `handlePortIn()`. The refactor introduces one explicit consumption cursor used by **both** consumers:

```cpp
// New / reshaped Tape surface (minimal additions)
bool   EnsureImageLoaded();          // lazy load via LoaderTAP from coreState.tapeFilePath
size_t GetConsumptionCursor() const; // next block index to deliver (signal or trap)
void   ConsumeBlock(size_t index);   // advance cursor past block (trap path)
void   StartPlaybackAtCursor();      // signal fallback: startTape() honoring the cursor
```

Rules:

- (r2) the consumption cursor is the **pre-existing `_currentTapeBlockIndex`** — no new field. It is the single source of truth: signal playback advances it as blocks finish playing; the trap advances it via `ConsumeBlock()` as blocks are consumed directly. `UINT64_MAX` is the "nothing consumed" sentinel; `GetConsumptionCursor()` normalizes it to `0`.
- **A partially-played block counts as consumed.** The `ERR_NR` / 150-frame watchdogs (`tape.cpp:283-298`) can stop playback mid-block; on any stop with the cursor inside a block, the cursor advances past that block. Rationale: a real tape keeps rolling during a failed load — on retry the ROM resynchronizes on the *next* pilot tone, never mid-block. Tested in §12.1-8.
- `startTape()` initializes the playback position **from the cursor** (default 0 on a fresh image) instead of hardcoded block 0 — this is what makes mid-tape fallback seamless: if the header was fast-loaded and the data block declines (rows 7-8), signal playback begins at the data block, exactly where a real tape head would be.
- `EnsureImageLoaded()` replaces the inline `LoaderTAP` code in `handlePortIn()`. The **hardcoded fallback test file** (`AYtest_v0.2.tap` relative path, `tape.cpp:170-177`) is removed — with no tape file selected the correct behavior is "nothing to load", not a dev-tree path lookup.
- **`EnsureImageLoaded()` is idempotent and path-keyed**: it re-parses only when `coreState.tapeFilePath` differs from `_imageLoadedPath` (the path the live `_tapeBlocks` came from). The current `0x0564` hack re-runs `loadTAP` on every firing while the tape is stopped — safe today only because `startTape()` immediately follows; the refactored helper must never re-parse over live blocks (that would reset the consumption cursor and dangle `_currentTapeBlock`). Only tape-control commands (stop / eject / rewind / new insert) invalidate the image.
- TTD serialization (r2): **no format change** — `_currentTapeBlockIndex` was already part of the cursor-packed serialization blob (offset 9 of the serialized fields), so trap consumption restores with checkpoints out of the box (§12.2-5). The playback-position fields remain as-is.

## 10. Configuration and control surfaces

| Surface | Status | Change |
|---|---|---|
| `fasttape` feature (`featuremanager.*`) | **Implemented (r3)** | Registered id `fasttape`, alias `ftape`, category performance, **default on** — the sole runtime switch; `IsArmed()` does a live `isEnabled()` lookup (a context without a FeatureManager is never armed) |
| ~~`CONFIG.tape_traps` (`platform.h`)~~ | **Removed (r3)** | Heritage field (dormant r0, wired r2) superseded by the feature; the `unreal.ini` `[MISC] TapeTraps` parser entry and the key in all `data/configs/*/unreal.ini` templates are gone |
| CLI `fast_tape` | **Migrated (r3)** | `setting fast_tape on\|off` and the read paths now call `FeatureManager::{isEnabled,setFeature}` — the same switch as `feature fasttape on\|off` (`cli-processor-settings.cpp`) |
| WebAPI `fast_tape` | **Migrated (r3)** | `GET/PUT /settings[/fast_tape]` read/write the feature (`settings_api.cpp`) — in lockstep with the generic feature API |
| Qt GUI | **Migrated (r3)** | Checkable action **Machine → Fast Tape Loading** (`menumanager.cpp`, turbo-mode pattern: signal → `MainWindow::handleTapeTrapsToggled`); checked state synced from the feature in `updateMenuStates()`; toggling calls `setFeature(kFastTape)` — no pause, no reset (lazy arm, §6.1) |
| unreal-videowall | **Implemented (r3)** | **View → Toggle Fast Tape Loading** (`Ctrl+T`) — the ScreenHQ "for all tiles" pattern (`VideoWallWindow::toggleFastTapeForAllTiles`): `setFeature(fasttape)` on every tile's emulator; new tiles inherit the wall-wide state in the tile-creation feature bundle. `unreal-screen-viewer` intentionally has no such toggle — display-only, no emulator control |

Toggle discipline (r3): with lazy arm state there is nothing to refresh — `setFeature(fasttape, …)` takes effect on the next `LD-BYTES` invocation. The write runs the standard `FeatureManager::onFeatureChanged()` control path (feature-cache refresh + optional `features.ini` persistence) — the same path every other runtime feature toggle uses, and therefore no less thread-tolerant than the existing `feature` CLI command.

## 11. TTD (Time-Travel Debugging) interactions

| Aspect | Handling |
|---|---|
| Determinism | Trap outcome depends only on (CPU entry state, tape image content, consumption cursor) — all checkpointed or session-invariant. **No `RecordExternalEvent` barrier is needed** — unlike `startTape()`, which gates wall-clock-driven playback |
| Memory deltas | Payload writes go through the hooked write path (§9.3) → `TTDDirtyTracker` records them → frame/checkpoint deltas are complete |
| Seek/replay | A seek to before the trap restores pre-trap CPU state; re-execution re-fires the trap deterministically → identical writes. Seek to after the trap sees the writes via deltas |
| Cursor state | (r2) no change needed — `_currentTapeBlockIndex` was already serialized in `Tape::TTDSaveState/TTDLoadState`; trap consumption restores with checkpoints and survives seek round-trips (§12.2-5) |
| Image population | First trap fires → `EnsureImageLoaded()` populates `_tapeBlocks` mid-session. (r2) **No external-event marker is recorded**: population is a deterministic function of `coreState.tapeFilePath` + file content, leaves the cursor at its pre-load value, and re-derives identically on replay. Content remains "invariant within a session" per the tape.h §TTD contract; tape-control commands still invalidate it. Verified: the §12.2-5 session records zero tape markers and both boundary seeks report `haltReason == Target` |
| Replay suppression | During `ttdReplayActive` the trap runs normally (it is deterministic and side-effect-safe); no suppression required |
| Coverage | `ttdCoverageActive` samples PC on the fetch path; the trap bypasses fetch, so `$0556` is missed in coverage. (r2 deviation) the manual coverage sample was **not implemented** in v1 — `m1_pc` attribution (§7) keeps write-journal entries anchored to `$0556`, but reverse PC-search across a fast load will not list it; deferred to the v2 item list |

## 12. Test plan

All artifacts written under `scratch/` via `TestPathHelper::GetTestScratchPath()`. New test files follow the `*_test.cpp` / `ClassName_Test` conventions.

### 12.1 Unit tests (`core/tests/emulator/io/tape/tapefastload_test.cpp`)

Tiny TAP images are synthesized in-test (header+data pair builder helper with correct XOR checksums).

1. **Decline matrix** — one test per row of §6.2: verify entry (`Fc=0`), `DE=0`, empty image, end-of-tape, active playback, flag mismatch, bad checksum, length mismatch. Assert: return `false`, cursor unchanged, memory unchanged, registers unchanged. The verify-entry test in particular guards against the memory-corruption failure mode described in §13 (VERIFY mistaken for LOAD).
2. **Header consume** — entry `A=$00, DE=17, IX=$8000`: assert 17 payload bytes at `$8000`, `IX=$8011`, `DE=0`, carry set, `pc` = stacked return address, cursor +1.
3. **Data consume** — entry `A=$FF, DE=N`: payload at `IX`, same postconditions.
4. **64K wrap** — `IX=$FF00`, `DE=$200`: bytes wrap into `$0000-` (ROM area writes are harmless: bank 0 is SOS ROM — writes to ROM pages are ignored by the memory layer, matching real ULA behavior; assert only the RAM portion `$FF00-$FFFF`).
5. **IFF1 preserved** (r2) — set `iff1` before the trap; assert unchanged after (see §7.1).
6. **Arm state** — signature bytes corrupted in bank 0 → `IsArmed()` false; TR-DOS session flag set → false; `fasttape` feature off → false (alias `ftape` resolves to the same feature; the decline is inert).
7. **Fallback positioning** — consume header via trap, then force data-block decline (corrupt length): `StartPlaybackAtCursor()` must begin signal generation at block index 1, and the generated bitstream is the data block's.
8. **Partial-block consumption on stop** — start signal playback, halt mid-block (simulate the watchdog path), assert the consumption cursor advanced past the partially played block and the next trap invocation consumes the *following* block.

### 12.2 Integration / differential tests

1. **Fast `LOAD ""` end-to-end** — 48K model, ROM booted, type `LOAD ""` via keyboard-matrix injection, run ≤ 30 frames: assert BASIC program bytes in RAM, `PROG`/`VARS`/`E_LINE` sysvars moved consistently, signal playback never started, total load time < 1s emulated.
2. **Differential traps-ON vs traps-OFF** — same TAP both ways; traps-OFF runs real signal emulation to completion (bounded frame budget, e.g. 1500 frames for a small file). Comparison procedure with a concrete exclusion list: assert equal final `SP` in both runs, then compare `$4000..SP-1` excluding the timing-dependent sysvars — `FRAMES` (`$5C78-$5C7A`, differs in every run pair), `LAST_K` (`$5C48`) and `KSTATE` (`$5C40-$5C47`, keyboard-injection timing), and anything seeded from `FRAMES`. Stack contents above `SP` (transient garbage from the different execution histories) are excluded by the SP-relative bound. `ERR_NR` is *included* (both runs must end in the same report state); the `R` register lives outside RAM. This is the strongest guard for the §4 contract: if the trap's postconditions were wrong, the ROM flow after `LD-BYTES` would diverge and RAM would differ.
3. **Custom-loader fallback** — TAP with headerless data block (flag `$FF` only, no header pair, or custom flag `$3C`): assert trap declined and playback engaged (auto-start fired), load completes via signal path.
4. **Multi-part hybrid** — TAP with a vanilla header/data pair followed by a custom-flag block: first pair fast-loads, second declines → playback starts at block 2.
5. **TTD round-trip** (r2: implemented as `TapeLoading_Integration_Test.TTDRoundTripAcrossFastLoad`) — record a session across a fast load; `SeekTo({frame, 0})` on both sides of the trap boundary. Checkpoints turned out to be **periodic keyframes, not per-frame** (9 checkpoints over ~108 frames), so checkpoint-index ≠ frame; the state oracle is therefore a *double crossing*: hash the after-load landing (registers + port latches + counters + full RAM digest via `ttd::CaptureSnapshot`), seek back and forth, and require bit-identical reproduction. Also asserts the tape cursor restores with the subsystem blob (0 pre-load, 2 post-load) and that every seek reports `haltReason == Target` — the trap path emits no external-event markers (§11).

### 12.3 Manual verification

- Qt build: insert a large multi-part TAP (e.g. a 128K game), `LOAD ""`, confirm near-instant load and correct execution.
- Toggle Machine → Fast Tape Loading off mid-session → next load runs with stripes at real speed.
- Pentagon model (SOS-derived ROM) → trap still engages; TR-DOS paged (`CF_TRDOS`) → trap does not fire.

## 13. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Carry semantics inverted in implementation (LOAD/VERIFY swapped) | **Memory corruption, not benign**: a VERIFY mistaken for LOAD stores the tape payload over memory — exactly over the differing bytes VERIFY exists to catch — and reports success instead of a mismatch | Contract taken from the documented disassembly and trap prior art (Fuse et al.): `Fc=1` LOAD / `Fc=0` VERIFY (§4.2); Phase 0 confirms via breakpoint trace; the verify-entry decline test (§12.1-1) and differential test (§12.2-2) lock it in |
| Exit-contract subtleties (register clobbers callers actually depend on) | ROM/BASIC flow diverges after trap | Differential test compares full RAM after complete `LOAD` flows; widen compared register set if it ever flags |
| Zero-warning / hot-path cost regression | Build/CI failure, perf regression | One compare + null check; benchmark via `core-benchmarks` Z80 loop before/after |
| Existing users relying on the `0x0564` auto-start hack's hardcoded demo file | Demo file no longer auto-loads when no tape selected | Documented behavior change; the hack only worked from a dev-tree CWD. Keyboard-driven `LOAD` with a selected tape file is unaffected |
| ~~`tape_traps` default flip~~ → `fasttape` default-on | Users who never touched a switch now get fast loads | Default-on matches emulator conventions (UnrealSpeccy heritage, `tape_traps=1`); the switch is one menu click / one `feature fasttape off` away |
| Analyzers reacting to bulk writes (e.g. BASIC analyzer) | Unexpected analyzer output during fast load | Desirable behavior (analyzers see real data); verify no analyzer asserts on write bursts in tests |

## 14. Open questions

1. **VERIFY fast path (v2)** — compare payload with memory in the trap, set `Fc` per result. Trivial once the Z-flag question is settled.
2. **TZX** — once `LoaderTZX` emits `TapeBlock`s, decide whether non-`$10`/`$11` blocks automatically decline (they do per §6.2 rows 6–8 — turbo blocks have non-standard flags/lengths).
3. **Loading-speed emulation option** — some demos detect load duration; a "fast but not instant" pacing mode (advance `t` by real block duration) could be a future middle ground. Not in v1.
4. **`tape_autostart` config field** — exists, unused; either wire it (gate the `0x0564` auto-start) or remove it. Out of scope here, noted for cleanup.

## 15. Implementation phases

| Phase | Content | Exit criteria |
|---|---|---|
| 0 | ROM contract confirmation (breakpoint trace of `LOAD`/`VERIFY` entries at `$0556`) — confirming the corrected §4.2 contract, not discovering it | **Superseded (r2)**: the §12.2-2 differential test byte-compares full RAM between trap and signal paths — an empirical confirmation of the entire §4 exit contract, stronger than a register trace |
| 1 | `TapeFastLoad` component, `Tape` cursor refactor (`EnsureImageLoaded`, `ConsumeBlock`, `StartPlaybackAtCursor`), `Z80Step` hook, arm-state wiring, `ROMAddresses::LD_BYTES` | Unit tests §12.1 green; `ninja -C cmake-build-release` zero warnings |
| 2 | Differential + integration tests §12.2 (incl. TTD round-trip) | All green in `core-tests` |
| 3 | Surfaces: feature registration + CLI/WebAPI/Qt migration | Code complete (r3): all surfaces drive the `fasttape` feature; manual checklist §12.3 pending an interactive run |
| 4 | Docs migration (this folder → permanent `docs/` location once finalized) | Per `docs/inprogress/README.md` lifecycle |

## 16. References

- *The Complete Spectrum ROM Disassembly*, I. Logan & F. O'Hara — `LD-BYTES` (`$0556`), `SA-BYTES` (`$04C2`), `SA/LD-RET` (`$053F`)
- Tape-trap prior art: Fuse, ZEsarUX and UnrealSpeccy implementations (hook at `$0556`, carry-discriminated LOAD/VERIFY)
- [TAP format](https://faqwiki.zxnet.co.uk/wiki/TAP_format) and in-tree notes in `core/src/loaders/tape/loader_tap.h`
- Sinclair tape interface encoding: `core/src/emulator/io/tape/tape.h` header comment
- TR-DOS address-trap precedent: `core/src/emulator/spectrumconstants.h` (`ROMSwitch`), `docs/rom/analysis/trdos-rom-interaction-analysis.md`
- TTD peripheral serialization contract: `tape.h` §TTDSerializable, `docs/inprogress/` TTD design set
