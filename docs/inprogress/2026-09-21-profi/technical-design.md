# ZX Profi 1024 — Technical Design

| | |
|---|---|
| **Status** | Design / draft for review |
| **Date** | 2026-09-21 |
| **Baseline** | master `09f55bf1` |
| **Inputs** | [karabas-pro-hardware-analysis.md](karabas-pro-hardware-analysis.md) (VHDL, MIT), [existing-emulators-review.md](existing-emulators-review.md) (UnrealSpeccy, ZXMAK2, Xpeccy), [unreal-ng-integration-audit.md](unreal-ng-integration-audit.md) (our code) |
| **Scope** | Emulate Profi 1024 fully: paging, ROM/DOS logic, all extended ports, both video modes, peripherals, TTD, automation, tests |

Notation used throughout: **[CONS]** agreed by UnrealSpeccy + ZXMAK2 + Xpeccy (the primary evidence for original Profi behaviour), **[KP]** seen in the Karabas-Pro RTL only. **Karabas-Pro is a clone board and is *not* an authoritative source**; a [KP] fact is a hint that needs corroboration, and where it contradicts the emulators the emulators win. **[HW]** below means "present in the RTL *and* consistent with the emulators". **[?]** unverified, needs a decision or a real-hardware check (see §12). Timing evidence is in [frame-timing-investigation.md](frame-timing-investigation.md).

---

## 1. Goals and non-goals

**Goals**

1. `PROFI` becomes a creatable model (`GET /api/v1/emulator/models` → `creatable: true`) that boots the real Profi ROM to the SYS menu and into TR-DOS, 128K BASIC and 48K BASIC.
2. Every documented Profi paging latch, DOS-ROM rule and port set behaves per §3–§6.
3. Both video modes render: standard 256×192 and hi-res 512×240 with attribute-per-8×1, 16-entry palette, both bright bits.
4. Complete TTD support (checkpoint, restore, seek, hash), registered through the model-state contract.
5. Automated tests at every layer (§10), using the Karabas test ROMs and synthetic fixtures.

**Non-goals (phase 1)**: Karabas-specific extensions (DivMMC, ZiFi, flash-tool registers `#008B/#018B/#028B`, turbo 7/14 MHz, VGA scandoubler, AVR soft switches) — except where a cheap, clearly-flagged hook helps testing (§11). 6 MB RAM boards. The 28 MHz mode.

---

## 2. Current state (verified against master)

`PortDecoder_Profi` is a 128K clone with a stub `Port_DFFD`; nothing Profi-specific runs. Concrete defects (all confirmed by two independent reviews, file: `core/src/emulator/ports/models/portdecoder_profi.cpp`):

| # | Defect | Effect |
|---|---|---|
| B1 | ROM select `isROM0 ? RM_128 : RM_SOS` is inverted vs hardware (`7FFD.4=1` selects the 48K/DOS side) | Wrong ROM after every write |
| B2 | `state.p7FFD` never written | TTD/automation see stale latch |
| B3 | `IsPort_7FFD` requires A2=1; hardware decodes A15=0 & A1=0 | Misses valid writes, breaks `OUT (C)` forms with A2=0 |
| B4 | `IsPort_DFFD` lacks A15=1, so `#5FFD`/`#1FFD` fire both handlers | Double dispatch |
| B5 | `Port_DFFD` empty; `pDFFD` never set | 512×240 unreachable, no extended RAM |
| B6 | Screen bit applied while 7FFD locked | Shadow screen flips after lock |
| B7 | No DFFD.4 lock override | Lock semantics wrong |
| B8 | Reset boots 48K ROM; hardware boots SYS (page 0) | Wrong boot path |
| B9 | No `#FE` OUT handler, no FDC/RTC/IDE/Covox/palette ports | Most of the machine missing |
| B10 | `GetTTDModelStateIds`/`CreateTTDSerializers` not overridden | (Latent) once DFFD is live, restore silently loses it |

Also: `Screen` has an `M_PROFI` mode, `R_512_240`, and `DetectModeProfi` (DFFD.7) but `DrawProfi` is a no-op and the geometry row is the 256×192 one. `data/rom/profi.rom` (64K) is present. There is no `data/configs/profi/unreal.ini`, which is the sole reason the model is not creatable. Memory already scales (`MAX_RAM_PAGES=256`, `MAX_ROM_PAGES=128`).

**Correction to the roadmap doc**: `2026-09-21-roadmap/01` claims Profi TTD serializers exist. They do not (B10). That document has been amended.

**ROM order (verified by string inspection of `data/rom/profi.rom`)**: page 0 = SYS menu ("TR-DOS 48K / TR-DOS 128K / Sinclair 48 / Sinclair128 / CP/M"), page 1 = TR-DOS 6.08, page 2 = 128K BASIC + STS monitor, page 3 = 48K BASIC. This equals the hardware order `{not dos_act, rom14}` → 0 SYS, 1 DOS, 2 128K, 3 48K. The audit's suspicion that pages 0/2 were swapped was wrong; only `rom.cpp:172-177` must be checked against this mapping (§5.3).

---

## 3. Latches and memory map

### 3.1 State

```cpp
// Profi latches (EmulatorState already has p7FFD, pDFFD, pFE)
p7FFD  bit 2:0 RAM page low          bit 3 screen (5|7 std, 4|6 hires)
       bit 4 ROM14 (1 = 48K/DOS side)  bit 5 lock (ignored while DFFD.4)
pDFFD  bit 2:0 RAM page high         bit 3 SCO  (window swap)
       bit 4 WOROM (RAM page 0 at #0000, DOS latch off, 7FFD lock lifted)
       bit 5 CPM   bit 6 SCR (#8000 = page 6)  bit 7 DS80 (hi-res + palette write)
dosAct (DOS latch)  reset = 1        // NOT a port latch; needs TTD capture
paletteRam[16]      hi-res palette    // TTD capture
```

RAM page number `B = ((pDFFD & 7) << 3) | (p7FFD & 7)` (0–63, 16 KB each = 1 MB) **[HW][CONS]**. The 512K Profi variant uses only DFFD.1:0; we model 1024K and mask with `GetRamMask()` for smaller `RAMSize` configs.

### 3.2 CPU windows [HW][CONS]

| Window | SCO=0 | SCO=1 |
|---|---|---|
| 0000-3FFF | ROM (§4) or RAM page 0 when WOROM | same |
| 4000-7FFF | page 5 | page B |
| 8000-BFFF | page 2, or page 6 if SCR | same |
| C000-FFFF | page B | page 7 |

Writes to the ROM window are dropped while WOROM=0. SCR is unconditional [HW]; Xpeccy's extra `7FFD.3` gate is treated as a deviation (§12 Q4).

### 3.3 Port write rules

* **7FFD**: decode `A15=0 & A1=0` (A2 not constrained) [HW][CONS]. Write accepted iff `!(p7FFD.5) || pDFFD.4`. When rejected, **nothing** changes, including the screen bit (fixes B6). The exact-`#7FFD` full-decode form (Karabas `fd_port`) additionally exposes bits 7:6, which are ignored on 1 MB boards, so we ignore them.
* **DFFD**: decode `A15=1 & A13=0 & A1=0` [CONS]; the Karabas full decode `#DFFD` is a subset. Never fires for A15=0 (fixes B4). Bit-change side effects: re-run bank mapping; when DS80 or SCR/screen-relevant bits change, call `Screen::InitRaster()` (ATM3/Pentagon-1024 pattern, `portdecoder_pentagon1024.cpp` `Port_EFF7_Out`).  Switching DS80 changes only the draw routine, not CPU clock or frame timing (§7.4).
* Handlers are applied in a fixed order per OUT: FE → 7FFD → DFFD, and the decoder must not fall through, so `#5FFD`-style ports hit exactly one handler.

### 3.4 Implementation

Add a Profi latch-to-bank translation using the ATM `UpdateModelMemoryBanks` pattern (`memory.cpp` ~L823) rather than a `Memory` subclass: the only structural need is bank 0 as RAM/ROM, bank 1 fixed to 5 or B, bank 2 to 2/6, bank 3 to B/7. Base `UpdateZ80Banks` must stay byte-identical for other models. `memory.cpp:764` (DFFD bit 4 clear) needs review so it does not clear WOROM for Profi.

---

## 4. ROM selection and DOS latch

### 4.1 ROM image

64 KB, four 16 KB pages; index `romIndex = ((dosAct ? 0 : 2) | rom14)` [HW][CONS]:

| dosAct | rom14 | page | content |
|---|---|---|---|
| 1 | 0 | 0 | SYS / CP-M menu (**boots here**) |
| 1 | 1 | 1 | TR-DOS |
| 0 | 0 | 2 | 128K BASIC (+STS) |
| 0 | 1 | 3 | 48K BASIC |

Karabas additionally switches among four 64K images through AVR soft switches / `#008B` — out of scope; one image per config (`profi.rom`).

### 4.2 DOS latch rules [HW], refined by the emulator review

* **Set** on an M1 fetch at `3D00-3DFF` while `rom14=1` (Unreal/ZXMAK2 do not gate on `pDFFD.4`; Karabas blocks it — not adopted, Q7). *[?]* Karabas also sets it on NMI when DS80=0 and via `#008B.6`/`.7`; phase 1 implements only the M1 trap.
* **Clear** on the first M1 fetch with `A15:14 != 00`, or whenever `pDFFD.4=1`.
* **Reset value 1** (SYS ROM boots). This is what makes reset land in the menu (fixes B8).
* Set wins over clear when both would fire.

The base infrastructure already has TR-DOS trap flags (`CF_TRDOS`, `CF_LEAVEDOSADR`, `CF_DOSPORTS`, used by Pentagon128/Scorpion). We reuse the M1 hook that drives them and parameterise the entry rule to `rom14`-conditioned `3Dxx`. Exact integration point is decided in implementation step 6 (§9) after reading the Pentagon128 path; the requirement is only the observable behaviour above.

Rule from our own memory notes still applies: **only the Beta Disk answers in TR-DOS**; a latch alone is not selection. The Profi FDC ports are gated by `dosAct` as in §6.3.

---

## 5. Configuration and boot

### 5.1 `data/configs/profi/unreal.ini`
Copy the Scorpion `[ROM.profi]` block: `HIMEM=PROFI`, `RAMSize=1024`, `Beta128=1`, `PROFI=rom/profi.rom`, ROMSET mapping (below). This alone flips the `creatable` flag.

### 5.2 Model flags
`config.mem_model = MM_PROFI`, `ula_type = ULA_DISCRETE_LOGIC` path already exists (`ulacontention.cpp:138`). Frame/INT timing per §7.

### 5.3 ROM role mapping
Automation surfaces name the pages through `ROM::GetROMPageRole` (SYS/Menu ROM, TR-DOS ROM, 128K Editor + STS Monitor ROM, 48K BASIC ROM).

`rom.cpp:172-177` assigns roles `sys=0, dos=1, 128=2, sos=3` for `MM_PROFI` — **consistent with the verified image**. The audit's suggested `128=0, sys=2` mapping is wrong and must not be applied. A boot test (§10.4) proves it: SYS menu text on page 0 after reset.

---

## 6. Port map

Decode is written as a mask/match **table** (with `PortTraceRule` entries so `decodeRuleIndex` is meaningful in port traces), replacing the if-chain. The qualifiers:

```
EXT     = (cpm && rom14) || (dosAct && !rom14)      // "extended ports"   [HW]
NORMAL  = !cpm && !dosAct                            // joystick/mouse/covox/...  [CONS]
```

*[?]* UnrealSpeccy/ZXMAK2/Xpeccy define EXT only as `cpm && rom14`; Karabas additionally exposes extended ports to the SYS ROM. Default: **the emulator rule** (EXT = `cpm && rom14`), with a config switch `ProfiSysExtPorts` (default off; on = the Karabas variant) so a boot test with `profi_v450.ROM` can settle it (§12 Q2).

### 6.1 Consolidated table

| Port | Decode | Dir | Meaning | Condition | Src |
|---|---|---|---|---|---|
| `#xxFE` | A0=0 | R/W | ULA: beeper b4, MIC b3, border b2:0; read: keys, b7 = palette-present flag *[?]* | always | HW |
| `#xx7E` | A7=0 & A0=0, `OUT` | W | palette entry (§7.3) | DS80=1 | HW/CONS |
| `#7FFD` | A15=0, A1=0 | W | §3.3 | | CONS |
| `#DFFD` | A15=1, A13=0, A1=0 | W (R) | §3.3 | | CONS |
| `#FFFD`/`#BFFD` | A15=1, A1=0; A14 selects reg/data | R/W | AY ×2 / TurboSound | | HW/CONS |
| `#1F,#3F,#5F,#7F` | A7=0, A1:0=11 (regs by A6:5) | R/W | WD1793 | `dosAct` && (!cpm \|\| !rom14) | CONS |
| `#FF` | | R/W | Beta system (drive/side/density, DRQ/INTRQ read) | dosAct, `!cpm`; `#BF` when cpm && !rom14 | CONS |
| `#83,#A3,#C3,#E3` | | R/W | WD1793 regs (extended) | EXT | CONS |
| `#3F` | | R/W | Beta system (extended) | EXT | CONS |
| `#8B,#AB,#CB,#EB` | A7=1, A4:0=01011; reg=A10:8 | R/W | IDE, with high-byte latch; A5 polarity swaps read vs write | EXT | CONS |
| `#BF/#FF` (addr), `#DF/#9F` (data) | | W/R | RTC (DS12885 / MC146818-like) | EXT | CONS/HW - **implemented**, see §6.2 |
| `#5F` (L), `#3F` (R) | | W | Covox/SoundRive DAC | NORMAL | CONS - **implemented**, see §6.2 |
| `#87,#A7,#C7,#E7` | | W | Covox in extended mode | EXT | Unreal only [?] - not implemented |
| `#1F` | | R | Kempston joystick | NORMAL | CONS |
| `#FBDF,#FFDF,#FADF` | | R | Kempston mouse | NORMAL | CONS |
| `#008B,#018B,#028B` | | W | Karabas system/turbo/lock | KP | **not implemented**; see §11 |

Overlap resolution order (first match wins): paging/AY (A15-based) → FE family → EXT set → NORMAL set. Conflicting aliases (`#3F` is Beta-system in EXT, Covox-R in NORMAL; `#BF/#FF` is RTC address in EXT, Beta-system in NORMAL) are resolved by the qualifiers above, which are mutually exclusive by construction.

### 6.2 Peripherals to add
* **Beta Disk gating** in `DecodePortIn/Out` following the Pentagon128 pattern; reuses the existing WD1793.
* **RTC**: done - `#BF/#FF` (address), `#9F/#DF` (data), EXT mode only (`cpm && rom14`), checked before the FDC/system-port decode since `#BF/#FF` alias to it outside EXT mode. New Profi-owned `ProfiCMOS` (`core/src/emulator/memory/profi/proficmos.{h,cpp}`), sharing only the DS12885 register map (`core/src/emulator/io/rtc/ds12885.h`) with ATM3's `CMOS` - not its I2C NVRAM, which Profi's real RTC never had. **Deviates from the plan below**: serves live host time (mirroring ATM3's own wiring), not a deterministic tick from emulated time, and has no TTD state capture yet - so a TTD-recorded session with RTC reads will see the host clock at replay time, not the clock at capture time. Low priority per Q10.
* **IDE (Nemo/Profi)**: only if the existing `core/src/emulator/io/hdd` supports the 8-bit high-byte latch; otherwise defer to phase 2. Register with TTD (roadmap ST-3).
* **Covox/SoundRive**: done - `#5F`/`#3F`, NORMAL mode, mapped onto the existing `Covox` device's `PORT_LEFT_A`/`PORT_RIGHT_A` from `PortDecoder_Profi::DecodePortOut` (the `_A` ports, not `_B` - `computeStereoAmplitudes()`'s mono-compatibility fallback arms on `LeftA==LeftB==RightA==0` and substitutes `RightB` into both channels; using `_B` as the target left the fallback keyed on `_A` alone, leaking Right into Left whenever Left passed through silence). Extended-mode aliases (`#87/#A7/#C7/#E7`) still open.
* **Kempston mouse / joystick**: base helpers, gated by NORMAL.

### 6.3 Unresolved port decode details
Width of the FDC alias (`(p&0x83)==0x03` vs strict `A4:0=11111`) differs (Unreal loose, ZXMAK2/Xpeccy strict). **Default: UnrealSpeccy's loose decode (A7=0, A1:0=11)** — the most complete and demo-validated emulator; a decode-width test documents it.

---

## 7. Video

### 7.1 Standard mode (DS80=0)

256×192 with attributes and flash; the existing `M_PROFI` geometry row (352×288, paper 256×192) is correct for the picture. Screen page 5, or 7 when `7FFD.3=1`. Border colour from `#FE`. Reuse the existing ZX/Pentagon renderer path for `M_PROFI`.

**Timing (decided from the emulator sources, see [frame-timing-investigation.md](frame-timing-investigation.md))**: every emulator that defines Profi timing uses the 48K/Scorpion frame, not Pentagon:

| Source | Frame | INT length | INT → first paper |
|---|---|---|---|
| UnrealSpeccy `PRESET.PROFI` ("thanks to DDp") | 224 T × 312 = **69888** | 28 T | 12580 T |
| ZXMAK2 (Profi 3.2, "TODO: needs approve") | 224 × 312 = **69888** | 39 T (was 42) | 12583 T |
| Xpeccy-plus `ULA.Profi` | 448×312 px = **69888** | 32 T | layout row unaudited, paper offset disputed |
| pico-spec (copies Unreal/ZXMAK2) | 69888 | 28/39 | 12580 |

No source cites a measurement of the standard frame on a real Profi, and there are no Profi timing/compatibility modes in any of them (only board 3.xx/5.xx, 512K/1024K, and Unreal's user-selected preset). Therefore: **69888 T = 224 T × 312 lines at 3.5 MHz, no contention, INT length 32 T (middle of 28/32/39), INT→first-paper 12580 T**, all recorded as *unverified but consensus*. No Pentagon-vs-48K switch is added; the Pentagon 71680 figure came only from the Karabas RTL and is dropped. `M_PROFI` reuses the existing 312-line 48K/Scorpion-style geometry row.

### 7.2 Hi-res 512×240 (DS80=1) [HW][CONS]

Per 8-pixel byte, two reads at the **same offset** in different pages:

| Fetch | Page (7FFD.3=0 / 1) |
|---|---|
| bitmap | 4 / 6 |
| attribute | 0x38 (56) / 0x3A (58) |

Address per 16-pixel cell: `A[13:0] = { ~h3, v[7:6], v[2:0], v[5:3], h[8:4] }` — the ZX Spectrum layout for `v` = 0..239 (three thirds of 64 rows via `v[7:6]`), with A13 = `~h3`: the **first** byte of a pixel pair is read at `+0x2000`, the second at `+0x0000`. Attribute byte: `b7` paper-bright, `b6` ink-bright, `b5:3` paper GRB, `b2:0` ink GRB. **No flash.** Pixel 1 bpp MSB first; attribute covers 8×1.

Palette index = `{bright, G, R, B}` (16 entries).

Geometry: paper 512×240, right border 48, left border 48 (**storage 608×288** proposed: 512 + 2×48 wide; 240 + 2×24 tall). Add a new geometry row `M_PROFIHR` (or reuse `M_PROFI` with mode-dependent row selection through `DetectModeProfi`'s `R_512_240`); the ATM 704-wide rows are the precedent for beam ≠ storage width.

### 7.3 Palette

Write: `OUT (xx7E), n` with DS80=1, A7=0 & A0=0. **Colour** comes from the address bus: `c = ~(A15:A8)`; **index** = `(previous #FE write value ^ 0x0F) & 0x0F`. Format `GGGRRRBB(B)`:

* Emulators: Unreal/ZXMAK2 use `Gg0Rr0Bb` (2 bits/channel, mid bits ignored); Xpeccy 3+3+2. Karabas (clone, non-authoritative) uses 3-3-3.
* Decision: **implement Unreal/ZXMAK2 `Gg0Rr0Bb` as the reference behaviour** (demo-validated) and make the decode a superset that also accepts the 3-bit patterns Xpeccy writes; Karabas's third blue bit is not modelled. 
* Reset palette: standard 16 Spectrum colours (bright = high intensity). *[?]* (§12 Q5)

The "index from previous FE write" rule means the `#FE` latch must remember the last value on every FE-family OUT, not only on `#xx7E`.

### 7.4 Frame timing in hi-res *[?]*

Unreal, Xpeccy and ZXMAK2 do **not** change the frame, INT or CPU frequency when DFFD.7 is set (Unreal/Xpeccy only switch the draw routine, with the 240 lines centred on the 192-line paper). ZXMAK2 alone switches to a 192 T/line raster with a 19 T INT offset but keeps the frame at 69888. The only real-hardware evidence in the corpus is pico-spec's recalibration against photos of a real Profi running the *mcprofi2016* demo: 192 T/line, first paper 9238 T after INT, frame 69888 (one demo, uncorroborated). ZXMAK2 also carries an unimplemented comment that the DS80 frame may be 59904 (312×192). The Karabas 3 MHz / 59904 clock change is a clone property and is **not** adopted.

Decision: hi-res keeps the standard 3.5 MHz, 69888 T frame and INT. 512 pixels do not fit the 256-px paper window at 2 px/T, so the renderer draws two hi-res pixels per standard pixel slot on the standard beam (storage wider than beam, the same technique the ATM 640/704-wide modes use); the 240 lines are centred on the 192-line paper as Unreal and Xpeccy do. Timing option `ProfiHiresRaster=pico` (192 T/line, first paper 9238 T after INT) is available as an experiment and is covered by tests so it can become default if real-hardware evidence appears. See §12 Q3.

### 7.5 Border in hi-res
Shown through the palette with the **inverted** index (`palette[~border & 7]`, non-bright): ZXMAK2 `ProfiRenderer` and Xpeccy (`nextbrd ^= 7`) agree; Karabas matches. Implemented in `ScreenZX::DrawProfiHiRes`.

### 7.6 Renderer structure
`Screen::DrawProfi(n)` per beam-clock, like `DrawATMHiRes` (per-`n` T-state chunks, `vbuf` writes with `vptr`). Pixel/attribute pages via `GetActiveSurfaceRAMPages()` returning `{4|6, 0x38|0x3A}` for `M_PROFIHR`. `Screen::GetVideoModeName` → "PROFI" / "PROFI512" so `/state/screen/mode` reports the mode. Frame-size consumers (Qt viewer, `recordingmanager.cpp:162`, screencapture, GIF) must handle the 512-wide storage; ATM 704-wide already forced the same, so no new consumer work is expected beyond verifying.

---

## 8. TTD integration

Per the model-state contract (`timetravelmanager.cpp` `RegisterModelPeripherals`) recording is **refused** when a declared id has no serializer.

1. `PeripheralId::ProfiPaging = 9` (before `Count`).
2. `core/src/debugger/ttd/profi/ttdprofipaging.{h,cpp}` — `TTDProfiPaging`, modelled on `TTDAtmPaging` (packed POD with `static_assert` on size, `Snapshot()` shared by save and hash). Payload (v1):
   ```
   uint8_t pDFFD;     uint8_t dosAct;     uint8_t lastFE;      // palette index source
   uint16_t palette[16];                   // hi-res palette
   uint8_t rtcAddr;   uint8_t rtc[256];    // if RTC implemented
   uint8_t ideLatch;                       // if IDE implemented
   ```
   Restore re-runs bank mapping (`UpdateZ80Banks`) and `Screen::InitRaster()` so mode and windows are rebuilt.
3. `PortDecoder_Profi::GetTTDModelStateIds()` → `{ProfiPaging}`; `CreateTTDSerializers()` → `TTDProfiPaging`.
4. `p7FFD` written by the decoder (B2) so the existing `TTDChipsetState` path is correct.
5. Update `ttd.ksy` / `ttddumpformat.h` to document the id.
6. Confirm `machinestatehash` covers `pDFFD` via the serializer (test at `machinestatehash_test.cpp:126` already sets it).
7. Storage (roadmap §6): WD1793 track data is a separate item (ST-4); Profi inherits whatever Beta Disk gets. IDE: register with the paged-storage overlay when that lands.

Video-mode timing switches (§7.4) require the frequency multipliers in the checkpoint to be restored before the next frame; existing ATM turbo tests are the template.

---

## 9. Implementation order

| Step | Work | Size | Depends |
|---|---|---|---|
| 1 | `configs/profi/unreal.ini`; verify model creates and shows the SYS menu with the buggy decoder (baseline) | S | – |
| 2 | Decoder rewrite: table-driven, B1–B9 fixed, 7FFD/DFFD/FE, ROM index, WOROM, SCO, SCR | M | 1 |
| 3 | Bank translation (`UpdateModelMemoryBanks`), regression check on 128K/Pentagon/Scorpion | M | 2 |
| 4 | DOS latch + Beta Disk gating + FDC/system ports | M | 3 |
| 5 | TTD: `ProfiPaging`, serializer, contract test | M | 2 |
| 6 | Standard video via existing renderer; timing decision applied | S | 2 |
| 7 | Hi-res renderer, palette, geometry, InitRaster hooks | L | 6 |
| 8 | Covox (done), RTC (done), mouse/joystick gating, (IDE) | M | 4 |
| 9 | Automation: `/state/paging`, `/state/screen/mode`, CLI ROM-page names, port trace rules, AGENTS.md creatable list, MCP `unreal://machine/profi` | S | 2, 7 |
| 10 | Full test suite (§10) grows with each step; conformance matrix rows | L | all |
| 11 | Real recordings for TTD v2 benchmark (roadmap §5.4) | S | 5, 7 |

Existing suites that mention Profi must change in the same step as the decoder: `modelsregression_test` (goldens encode the inverted ROM polarity), `portdecoder_porttag_test`, `portdecoder_portmap_test`, `kempston_mouse_decode_test` (its "no Profi machine creatable" comment).

---

## 10. Test plan

All tests follow `core/tests/README.md`: no `sleep_for`, <50 ms except justified boot tests, `TestWait`, `GetUniqueTestScratchPath()`, `EnableTurboMode()` on boot-bound tests and **never** on pixel-asserting tests. Files under `core/tests/emulator/` (globbed, no CMake edit).

### 10.1 Port decoder unit tests (`portdecoder_profi_test.cpp`, synthetic tagged ROM, mirrors `scorpionfixture.h`)

| Group | Assertions |
|---|---|
| 7FFD | every bit; A2 don't-care; lock at bit 5; lock lifted by DFFD.4; rejected write leaves screen bit unchanged; ROM polarity (`rom14=1` → tag 48/DOS side) |
| DFFD | RAM high bits → page = `hi<<3 \| lo` for all 64 pages; SCO swap matrix (4 windows × 2); SCR page 6; WOROM RAM-at-0 + write-through; CPM/DS80 latches |
| Decode | `#5FFD`, `#1FFD`, `#DFFD`, `#7FFD`, `#FFFD`, `#BFFD` each hit exactly one handler; A15/A13/A1 boundary sweep over all 65536 ports vs a reference table |
| ROM/DOS | reset = SYS; `3Dxx` M1 with rom14 sets DOS; fetch ≥ 4000 clears; DFFD.4 clears/blocks; set-over-clear priority |
| Ports by mode | NORMAL/EXT/CPM×rom14×dosAct truth table: FDC, `#FF/#BF/#3F`, IDE, RTC (done: `#BF/#FF/#9F/#DF` EXT-only via `cpm && rom14`, verify RTC wins over FDC's `#BF/#FF` system-port alias when both `dosPorts` and EXT are true), Covox (done: `#5F`/`#3F` NORMAL-only, verify FDC wins when `dosAct`), joystick, mouse |
| Trace | `decodeRuleIndex` and `decodedPort` per rule; `getPortMapEntries` rows |

### 10.2 Video tests (`profi_video_test.cpp`, no turbo)

* Standard: pixel/attr/flash, screen 5 vs 7, border.
* Hi-res: write a known pattern into page 4 and 0x38 through `RAMPageAddress`, run one frame, assert framebuffer for: first/second byte offset (+0x2000), all three thirds, ink/paper/bright bits, 512 columns, 240 rows, border inverted.
* Palette: write via `OUT (xx7E)` with crafted A15:A8; verify index-from-previous-FE, 3-3-3 encoding and the Unreal `Gg0Rr0Bb` subset; ignored when DS80=0.
* Mode detect: DFFD.7 toggles `M_PROFI ↔ R_512_240`, `/state/screen/mode` output, mid-frame change.
* Timing: INT position and frame length for both `ProfiTiming` values and the hi-res option.

### 10.3 TTD tests (`core/tests/debugger/ttd/profi/`)

`ttdprofipaging_test` (round trip, hash sensitivity per field), add `"PROFI"` to `ttdmodelstatecontract_test.cpp:49` and make it mandatory once creatable, seek/restore test paging RAM through DFFD and toggling DS80 mid-run, `machinestatehash` sensitivity to `pDFFD`, divergence-corpus entry, reverse-step equivalence (roadmap §8).

### 10.4 System tests with real ROMs (skip cleanly with `GTEST_SKIP` if the ROM is absent)

| Test | Material | Pass criterion |
|---|---|---|
| SYS boot | `data/rom/profi.rom` | menu text (`TR-DOS 48K`…) on screen; ROM page 0 |
| 48K/128K BASIC | menu selections via keyboard injection | `(C) 1982` / `1986 Sinclair Research` banner; `7FFD.4` state |
| TR-DOS entry/exit | menu → TR-DOS, `CAT` | TR-DOS banner, DOS latch transitions, only Beta Disk responds |
| Memory test | `TEST3_00.ROM` (3840 B) | final "test projden" text, no "oshibka po adresu" — exercises `#7FFD` and extended RAM paging |
| RAM/diagnostics | `DiagROM.v50` | "Lower 16KB RAM: OK", "Upper 32KB RAM: OK" |
| Z80 exerciser | `ZEXDOC.$C` / `ZEXALL.$C` loaded at `#8000` with 48K ROM paged, run unthrottled | every line `OK`, ends "Tests complete" (long-running; `DISABLED_`-style opt-in or nightly, not in the default <50 ms set) |
| System menu | `TEST430.ROM` | interactive; used manually for hi-res/palette visual smoke, not automated |
| Disk | `pqdos1.fdi` | boot floppy image loads (FDC path) once FDI import applies |
| Hi-res demos | none in corpus | **gap**: capture real Profi hi-res software for §5.4 recordings |

### 10.5 Automation tests
`/state/paging` shows `extended_ram_bank` (DFFD.2:0) and `video_512x240` (bit 7); `/state/screen/mode` reports `PROFI`/512×240; `emulatormanager_test` creatable flag; MCP `list_models`.

### 10.6 Conformance matrix (roadmap doc 01 §8)
PROFI joins: capture/restore round-trip, session serialize/load, seek storm, zero-bytes-for-absent-peripheral, storage overlay (with Beta Disk), reverse-step equivalence including a DS80 switch.

---

## 11. Materials collected

Under `testdata/machines/profi/` (Karabas-Pro is MIT, licence copied as `LICENSE-karabas-pro`; the ROM images inside are third-party — same "test material only" basis as `testdata/NOTICE.md`, which must get a Profi row before commit):

| File | Origin | Use |
|---|---|---|
| `rom/profi_mainrom_standart.rom` | Karabas `firmware/src/fpga/profi/rom` | Reference main ROM (4 pages) |
| `rom/TEST3_00.ROM`, `TEST430.ROM`, `TEST48K.rom`, `DiagROM.v50` | same | Hardware tests |
| `rom/TR-DOS_6.11Q_ZX_PROFI1024_RMD_A.ROM` | same | TR-DOS variant |
| `rom/profi_v450.ROM` | same | Extended-ports-from-SYS probe (§12 Q2) |
| `software/ZEXALL.$C`, `ZEXDOC.$C` | `software/profi/tests` | CPU exerciser |
| `software/pqdos1.fdi` | `software/profi/pq-dos` | FDC/boot |

Working copy of the full upstream tree (VHDL, docs, other ROMs) is in `scratch/profi/kp` (git-ignored). Reference documents for the design are in this folder. Cross-emulator sources reviewed: UnrealSpeccy (`unreal-speccy`, `zx-evo/pentevo/unreal`), ZXMAK2 `Hardware/Profi`, Xpeccy(+plus).

**Karabas-only features and whether we add them**: `#008B/#018B/#028B`, DivMMC, ZiFi, turbo 7/14 MHz, 6 MB RAM, AVR ROM-bank switching, `fd_port` correction (blocks full-address ports after `OUT (n),A`), NMI→DOS — none needed for original Profi software. A single optional `ProfiFdPortQuirk` is *not* planned.

---

## 12. Open questions and decisions

| # | Question | Evidence | Proposed default |
|---|---|---|---|
| Q1 | Standard-mode frame and INT | **Resolved from emulators**: all use 69888 (224×312); INT 28 (Unreal) / 32 (Xpeccy) / 39 (ZXMAK2, "needs approve"); INT→paper 12580–12583 | 69888, INT 32 T, paper offset 12580 T; unverified vs real hardware. Karabas 71680 dropped (clone artefact) |
| Q2 | Do SYS-ROM (page 0) accesses see extended ports (Karabas only) or normal ports (Unreal/ZXMAK2/Xpeccy)? | Emulator review §5.6 | Emulator rule, switch for the Karabas variant; settle with `profi_v450.ROM`/menu boot test |
| Q3 | Hi-res line/frame timing | No emulator changes the frame; ZXMAK2 uses 192 T/line; pico-spec calibrated on a real Profi (mcprofi2016): 192 T/line, paper 9238 T after INT, frame 69888 | Standard 3.5 MHz frame; 192 T/line raster as option; need a real-hardware capture to decide |
| Q4 | SCR window gated by `7FFD.3` (Xpeccy) or not (RTL, others)? | RTL vs one emulator | Not gated |
| Q5 | Power-on palette; `#FE` read bit 7 meaning | RTL flag, no emulator implements | Standard 16 colours; bit 7 = 1 in DS80 *[?]* |
| Q6 | Palette write mask A7=0 & A0=0 vs exact `xx7E` | 4 variants | A7=0 & A0=0 (Unreal/ZXMAK2 form) |
| Q7 | DOS entry when DFFD.4=1 | RTL blocks, others don't | Do not block (Unreal/ZXMAK2); RTL-only behaviour |
| Q8 | FDC alias width | Unreal loose / others strict | Loose (UnrealSpeccy) |
| Q9 | Which existing Profi goldens encode the wrong ROM polarity and need re-baselining? | `modelsregression_test` | Re-baseline in the decoder change, reviewed line by line |
| Q10 | RTC behaviour: Karabas RTC is a 256-byte RAM fed by the AVR; real DS12885 counts time | RTL comment | Deterministic counters from emulated time; low priority |

**Risks**: unverified hardware facts (Q1, Q3, Q5); goldens locking mistakes; 512-wide frame consumers; TTD timing-multiplier restore in hi-res; the CPLD (FDC, `#FF` system latch) is **not** in the Karabas repo, so `#FF`/`#BF` bit behaviour is taken from the WD/Beta convention and the emulators, not from the RTL.

---

## 13. Deliverable checklist for "done"

Mirrors roadmap DoD-1..9: functional tests; `GetTTDModelStateIds`/serializers; no >4 KB blobs (none here); COW overlays via Beta Disk; conformance matrix; UNS round-trip when it lands; automation reports honestly; per-machine MCP resource; real recordings captured. Quality gates before any commit (only on explicit request): `ninja -C cmake-build-release`, `core-tests`, zero warnings, links checked with `tools/fix-absolute-paths.py`.


---

## 14. Implementation status (branch `profi`, 2026-09-21)

Reference for behaviour: UnrealSpeccy (`zx-evo/pentevo/unreal/Unreal/io.cpp`, `drawers.cpp`).

| Area | Status |
|---|---|
| `data/configs/profi/unreal.ini`; timing defaults in `Config::ApplyModelTimingDefaults` (69888 T, INT-to-paper 12580 T, INT 28 T) | done |
| Decoder rewrite: #7FFD/#DFFD masks (B3, B4), lock + WOROM override (B6, B7), `p7FFD` written (B2), ROM polarity (B1), reset into SYS (B8), FE-then-paging-then-AY dispatch | done |
| Bank mapping (`PortDecoder_Profi::UpdateModelMemoryBanks`, called from `Memory::UpdateZ80Banks`): RAM high bits, SCO, SCR, WOROM, CPM (`CF_DOSPORTS`) | done |
| DOS latch (`CF_TRDOS` via the existing $3Dxx M1 trap) and Beta Disk ports: normal, CP/M (#BF) and "modified" (#83/#A3/#C3/#E3, #3F) sets | done |
| Palette port (`OUT #xx7E`, previous-FE index) and `profiPalette` state | done |
| TTD: `PeripheralId::ProfiPaging = 9`, `TTDProfiPaging`, declared through `GetTTDModelStateIds`; contract test includes PROFI; seek test on a real PROFI | done |
| Video: standard mode `M_PROFI` on the 312-line row; hi-res `M_PROFIHR` (608x288 storage, 512x240 paper at 4 px/T, page 4/6 + attr 0x38/0x3A, inverted-index border, `ProfiMonochrome`) | done |
| Tests: decoder truth tables, TTD serializer, golden bank map re-baselined by hand, real-ROM boot (SYS BIOS splash renders in hi-res, 1024K RAM recognised) | done |
| Covox/SoundRive DAC at `#5F` (L) / `#3F` (R), NORMAL mode only (`PortDecoder_Profi::DecodePortOut` forwards to the existing shared `Covox` device via its canonical Left/Right ports - no changes to `covox.h/cpp` or `soundmanager.cpp`) | done |
| RTC (DS12885 #BF/#FF + #DF/#9F, EXT mode - live host time, no TTD state yet, no deterministic tick) | done |
| IDE (#8B/#AB/#CB/#EB), Kempston joystick #1F, FE read bit 7, Covox extended-mode aliases (#87/#A7/#C7/#E7) | **open** (UnrealSpeccy parity remainder) |
| Port-trace decode rules / port map (`getPortMapEntries`: #7FFD `0x8002/0x0000`, #DFFD `0xA002/0x8000`, palette #xx7E `0x0081/0x0000`, Beta128 rows gated on `CF_DOSPORTS`), `/state/paging` pDFFD fields (`extended_ram_bank`, `sco`, `worom`, `cpm`, `scr`, `video_512x240`), ROM page names (`ROM::GetROMPageRole`: SYS/Menu, TR-DOS, 128K Editor + STS Monitor, 48K BASIC), WebAPI/CLI/Lua/Python mode reporting (`PROFI`, `PROFIHR` 512x240), MCP resource `unreal://machine/profi`, `ttd.ksy` id list | done |
| Boot past the BIOS splash to the main menu (with or without a disk) | done, see below |

### Boot hang at "Please wait ..." - root causes and fixes
The BIOS drive probe issues an FDC command and immediately polls `IN A,(#1F) / RRCA / JR NC` (`$0797`) until BUSY = 1. Two emulation shortcuts hid BUSY, so the BIOS spun forever:
1. **Fast disk loading** collapsed the Restore/verify delay to 1 T-state while "TR-DOS paged in" - which the DOS latch also reports for the Profi SYS ROM. `DiskFastLoad::IsArmed()` now declines on `MM_PROFI` with ROM14 = 0 (UnrealSpeccy's own docs: "Profi service ROM can work only when all TR-DOS delays are enabled").
2. **A Type II command (Read Sector, `OUT #1F,#86`) on a not-ready drive** ended inside the register write. A real 1793 raises BUSY and drops it shortly after; `startType2Command` now holds BUSY for 64 T-states (`NOT_READY_BUSY_HOLD_TSTATES`) before ending. Test `WD1793_SleepTimeout_Test.ReadSectorWithoutDiskInsertedFailsGracefully` was adapted to the hold.
Result: the BIOS shows its main menu ("Основное Меню": CP/M, TR-DOS 48K/128K, Sinclair 48/128, test menu) with or without a disk. Tests: `ProfiBoot_Test.BiosReachesMainMenuWithoutDisk`, `BiosLeavesPleaseWaitWithDisk`. Not yet verified: launching the menu entries.
