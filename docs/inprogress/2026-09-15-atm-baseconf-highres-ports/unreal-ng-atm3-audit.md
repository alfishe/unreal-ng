# unreal-ng ATM3 (ZX-Evo BaseConf, MM_ATM3 / "ATM3") — implementation inventory

> **Role in this folder:** what unreal-ng implements for `ATM3` at `95fce44d`, feeding [gap-analysis.md](gap-analysis.md). Items marked "(verify)" were later settled by [baseconf-hardware-reference.md](baseconf-hardware-reference.md); the gap analysis holds the verdicts.

Audit date: 2026-09-27, the repository root, branch `master` @ `95fce44d`.
Read-only audit; no repo files modified. All paths relative to repo root.

Legend: **Impl** = implemented, **Partial**, **Stub** = port decoded but fixed/dummy behavior, **Missing** = not decoded /
no code, **Dead** = code exists but is unreachable at runtime, **N/A** = not a BaseConf feature.
"(verify)" = the hardware claim is from BaseConf docs/TSConf docs/memory, not re-checked against BaseConf RTL in this audit.

---

## 0. Class / model wiring

| Item | Evidence |
|:--|:--|
| Enum `MM_ATM3` ("ATM Turbo 3.0") | `core/src/emulator/platform.h:313` |
| Model table row `{"ZX-Evo","ATM3",MM_ATM3,4096,RAM_4096}` — 4096 KB only | `core/src/emulator/config.h:54` |
| Decoder factory → `new PortDecoder_ATM3` ; `IsModelSupported` true (creatable) | `core/src/emulator/ports/portdecoder.cpp:71,124-125` |
| Class hierarchy `PortDecoder_ATM3 : PortDecoder_ATM710 : PortDecoder` | `portdecoder_atm3.h:27`, `portdecoder_atm710.h:32` |
| `Memory::UpdateZ80Banks` delegates ATM710/ATM3 bank computation to decoder `UpdateModelMemoryBanks()` → `PortDecoder_ATM710::updateMemoryBanks()` (ATM3 inherits, no override) | `core/src/emulator/memory/memory.cpp:859-866`; `portdecoder_atm710.cpp:95-98,670-748`; `portdecoder_atm3.cpp:506-508` |
| Timing: 312×224 = 69888 T, intstart 1756, intlen 32 (shared ATM450/710/ATM3 branch, both ini-driven and canonical geometry) | `core/src/emulator/config.cpp:916-926,979-985` |
| INT pulse cleared by acknowledge (baseconf `zint.v`) — ATM3 only | `core/src/emulator/cpu/z80.cpp:1088-1096` |
| ATM3 has **no INT gate** (FF77 bit 5 ignored, unlike ATM710) | `portdecoder_atm3.cpp:342-343` vs `portdecoder_atm710.cpp:560-567` |
| GUI model menu lists MM_ATM3 | `unreal-qt/src/menumanager.cpp:620` |

---

## 1. Port decoder — every decoded port

Decode order in `PortDecoder_ATM3::DecodePortIn` (`portdecoder_atm3.cpp:40-100`): #xx57 → #xxBF → #xxBE → CMOS data →
#EFF7 → base ATM710 (`portdecoder_atm710.cpp:100-177`: full-decode claim override → #FE → #FFFD → #EFF7 → GS #B3/#BB →
Beta128).
Decode order in `DecodePortOut` (`portdecoder_atm3.cpp:102-214`): #xx57 → #xxBF → #xxBE → [manager gate open:
#x7F7(37F7) → #xx77 → #x FF7 windows → CMOS addr/data (DEF7/BEF7)] / [gate closed: swallow 37F7/xx77/xFF7; CMOS
DFF7/BFF7] → base ATM710 (`portdecoder_atm710.cpp:179-336`: full-decode claim → #FE → #7FFD → xx77 (ATM710 path,
unreachable for ATM3 since handled above) → #EFF7 → xFF7 (unreachable) → #BFFD → #FFFD → GS #B3/#BB → #33 → Beta128
+ palette #FF).

**Manager gate** `IsManagerEnabled()` = `pBF.0 (shaden) || aFF77.9 (~CPM)==0 || CF_TRDOS` (`portdecoder_atm3.cpp:220-230`).

### 1.1 Port table

| Port (mask/value) | Dir | Gating | Behavior | Status | Evidence |
|:--|:--|:--|:--|:--|:--|
| **#FE** — A0=0 (mask 0x0001, val 0) | IN | none | `Default_Port_FE_In` (keyboard matrix + tape EAR) | Impl | `portdecoder_atm710.cpp:133-137,358-362` |
| #FE | OUT | none | `Default_Port_FE_Out` (border 3 bit, beeper, MIC) + **A3 → `atmBorderBright`** (4th border bit / palette cell pointer) | Impl | `portdecoder_atm710.cpp:203-212` |
| NOTE: A0-only #FE decode means *every even port* (e.g. Nemo IDE #10..#F0, #C8) is border/beeper on ATM3 | — | — | Real BaseConf FE decode width (verify) | risk | `portdecoder_atm710.cpp:358-362` |
| **#7FFD** — mask 0x8006, val 0x0004 (A15=0,A2=1,A1=0) | OUT | lock: ignored only while `EFF7.2 (lockmem) && p7FFD.5` | stores p7FFD, `UpdateZ80Banks`, shadow screen switch | Impl | `portdecoder_atm3.cpp:468-482`; `portdecoder_atm710.cpp:364-371,494-514` |
| #7FFD bits 5-7 as P1024 page extension (lockmem=0) | — | — | Header claims "Extended 7FFD bits 5,6,7"; `updateMemoryBanks` "RAM from 7FFD" uses only `p7FFD & 7` | **Missing** (header claim not implemented) | `portdecoder_atm3.h:12`; `portdecoder_atm710.cpp:715-719` |
| #7FFD readback | IN | — | only via #0ABE | Impl (via #BE) | `portdecoder_atm3.cpp:446-447` |
| **#xx77** (FF77) — mask 0x00FF, val 0x77 (any high byte, e.g. #BC77) | OUT | manager gate | pFF77=value, aFF77=port (A8=PEN, A9=~CPM, A14=pen2 palette lock); video-mode change → `InitRaster`; `updateTurboMode`; `UpdateZ80Banks`. No INT gate, no memswap | Impl | `portdecoder_atm3.cpp:155-160,232-238,330-376` |
| #xx77 with gate closed | OUT | — | swallowed (debug log) | Impl (per orig. Unreal) | `portdecoder_atm3.cpp:189-195` |
| #xx77 as **Z-Controller SD chip-select/config** (outside shadow) | OUT/IN | — | not decoded; OUT swallowed, IN → 0xFF | **Missing** | same; hw: TSConf hs §8.1 (#77 W bit1 CS_n, R=0x00) |
| **#xFF7 windows** (#3FF7/#7FF7/#BFF7/#FFF7) — mask 0x3FFF, val 0x3FF7; window = A15:A14; reg set = 7FFD.4 | OUT | manager gate | `pFFF7[idx] = (((v&0xC0)<<2)|(v&0x3F)) ^ 0x33F` → bits 9:8 type (RAM/ROM × from-7FFD/from-FFF7), 6-bit page (active-low) | Impl | `portdecoder_atm3.cpp:162-169,247-256`; `portdecoder_atm710.cpp:572-590` |
| xFF7 readback | IN | — | none on the port itself (floating 0xFF by design); readable via #00BE..#07BE | Impl (via #BE) | `portdecoder_atm710.cpp:150-155` |
| **#x7F7** (#37F7/#77F7/#B7F7/#F7F7) — mask 0x3FFF, val 0x37F7 | OUT | manager gate | 8-bit RAM page (4 MB), type bit 9 kept, bit 8 cleared (always RAM) | Impl | `portdecoder_atm3.cpp:146-151,240-245,378-394` |
| **#EFF7** — exact 0xEFF7 | OUT | none | pEFF7 latch; `updateTurboMode` (bit 4 = 3.5 MHz); z-bits (bit0 4bpp/ALCO, bit5 HWMC) → `InitRaster` | Impl | `portdecoder_atm3.cpp:484-504`; `portdecoder_atm710.cpp:253-256,592-603` |
| #EFF7 | IN | none | returns pEFF7 | Impl (raw; FPGA masks some bits — verify, see docs gap #4) | `portdecoder_atm3.cpp:89-96` |
| EFF7.2 lockmem | — | — | used only for the 7FFD lock rule | Partial | `portdecoder_atm3.cpp:475` |
| EFF7.3 ROCACHE (RAM at #0000) | — | — | only in debug dump; not applied to mapping | **Missing** | `portdecoder_atm710.cpp:39,818-821` |
| EFF7.7 CMOS enable | — | — | CMOS ports not gated by EFF7.7 (TSConf hs §9 says board CMOS needs EFF7.7 or DOS — BaseConf verify) | **Missing/verify** | `portdecoder_atm3.cpp:264-275` |
| **#xxBF** — mask 0x00FF, val 0xBF | OUT | none (always) | pBF=value; `UpdateZ80Banks` (bit0 shaden gates manager/CMOS/palette) | Partial | `portdecoder_atm3.cpp:116-121,396-410` |
| #xxBF bit 0 shaden | — | — | gates manager, CMOS addr pair, palette | Impl | `portdecoder_atm3.cpp:220-230,264-275,292-297` |
| #xxBF bit 1 (ROM write enable, "romrw_en" — verify) | — | — | latched only | **Missing** | `portdecoder_atm3.cpp:401` |
| #xxBF bit 2 font RAM write (fntw_en) | — | — | latched only; CPU writes never redirected to font RAM | **Missing** | docs gap #2 `docs/inprogress/2026-09-15-atm-baseconf-highres-ports/verification-gaps-and-tests.md:78-82` |
| #xxBF bit 3 1→0 edge = NMI request | — | — | "NMI serving is not wired", latched only | **Missing** | `portdecoder_atm3.cpp:398-401` |
| #xxBF | IN | none | returns pBF | Impl | `portdecoder_atm3.cpp:58-68` |
| **#xxBE** — mask 0x00FF, val 0xBE | OUT | none | pBE = 2 (NMI-exit counter) — **never consumed** anywhere | Stub/Dead | `portdecoder_atm3.cpp:123-129,412-419`; only uses of pBE: reset/out/TTD |
| #00BE..#07BE | IN | none | non-inverted page of pFFF7[0..7] | Impl | `portdecoder_atm3.cpp:424-428` |
| #08BE | IN | | RAM/ROM flags (bit8 of each window, inverted) | Impl | `:432-438` |
| #09BE | IN | | dos7ffd flags (bit9, inverted) | Impl | `:439-445` |
| #0ABE | IN | | p7FFD | Impl | `:446-447` |
| #0BBE | IN | | pEFF7 | Impl | `:461-462` |
| #0CBE | IN | | aFF77 bits 14/9/8 + pFF77 low nibble | Impl | `:448-450` |
| #0DBE | IN | | palette cell of 4-bit border, `(reg & 0xF3) | 0x0C` | Impl | `:451-457` |
| #0EBE | IN | | font byte under beam → returns 0xFF | **Stub** | `:463-464` |
| #0FBE | IN | | last 4-bit border | Impl | `:458-460` |
| #10BE+ | IN | | 0xFF | Missing (if hw defines more — verify) | `:463-464` |
| **#xxBD** (pBD: BaseConf breakpoint / "ATM3_BREAKPOINTS_BD") | IN/OUT | — | `pBDl/pBDh` only zeroed in reset; `#define ATM3_BREAKPOINTS_BD 0xBD` unused; port falls through to base (swallowed/0xFF) | **Missing** | `portdecoder_atm3.cpp:30-31`; `core/src/emulator/ports.h:12-15`; `platform.h:1049-1059` |
| **#FF palette** — exact low byte 0xFF | OUT | `IsManagerEnabled()` AND `aFF77.14 (pen2)==0` | 16-cell DAC palette, cell = 4-bit border; raw byte kept for #0DBE | Impl | `portdecoder_atm3.cpp:283-297`; `portdecoder_atm710.cpp:323-331,618-655` |
| **CMOS data** #BFF7 (gate closed) / #BEF7 (gate open) — exact 16-bit compare | IN/OUT | manager gate selects address pair | `CMOS::ReadCMOS/WriteCMOS` | Impl (exact decode; hw partial — verify) | `portdecoder_atm3.cpp:80-87,178-183,204-209,264-269` |
| **CMOS address** #DFF7 / #DEF7 | OUT | same | `CMOS::SetCMOSAddress` | Impl | `portdecoder_atm3.cpp:171-177,197-203,271-275` |
| **#xx57** Z-Controller SD SPI data | IN | none | returns 0xFF ("no card") | **Stub** | `portdecoder_atm3.cpp:45-56` |
| #xx57 | OUT | none | swallowed | **Stub** | `portdecoder_atm3.cpp:104-111` |
| **#FFFD** — mask 0xC002 val 0xC000 | IN/OUT | none | AY register read / select (TurboSound chip select by device) | Impl | `portdecoder_atm710.cpp:139-143,274-278,424-430` |
| **#BFFD** — mask 0xC002 val 0x8000 | OUT | none | AY data | Impl | `portdecoder_atm710.cpp:269-273,416-422` |
| **GS #B3/#BB** — `(port & 0xF7)==0xB3` | IN/OUT | card fitted | `PeripheralPortIn/Out(0xB3/0xBB)` | Impl | `portdecoder_atm710.cpp:163-167,290-294` |
| **GS #33** — low byte 0x33 | OUT | card fitted | GS control (reset/NMI) | Impl | `portdecoder_atm710.cpp:295-299` |
| **Beta128 WD1793** #1F/#3F/#5F/#7F (+ #FF system) — `decodePort` canonicalises low byte | IN/OUT | **no DOS/shaden gating** (FDC answers always); #FF bit 6 (DDEN) masked → always MFM | `PeripheralPortIn/Out` → WD1793 | Impl (ungated) | `portdecoder_atm710.cpp:169-173,300-321,432-454`; WD1793 has no gate `wd1793.cpp:3230,3315` |
| Beta "vdos" / virtual drive (FDD_VIRT, page 0xFF at W0) | — | — | nothing on ATM3 (only TSConf fields `tsconf.h:293-294`, unused) | **Missing** | — |
| **Kempston joystick #1F** (outside DOS) | IN | — | #1F always goes to FDC; no joystick path in ATM decoders | **Missing** | `portdecoder_atm710.cpp:169-173` (vs Scorpion `portdecoder_scorpion256.cpp:252,742`) |
| **Kempston mouse #FADF/#FBDF/#FFDF** | IN | — | ATM710/ATM3 never call `IsPort_KempstonMouse`; ini `Mouse=KEMPSTON` creates a device that is unreachable | **Missing** | compare `portdecoder_pentagon128.cpp:142-148`, `portdecoder_spectrum48.cpp:100` |
| **Covox #FB / SounDrive** | OUT | — | ATM decoders never call `DispatchSelfDecodingOut/In`, so the `Covox` self-decoding device registered from `CovoxFB=1`/`SD=1` never receives writes; #FB swallowed | **Missing (dead config)** | `portdecoder.cpp:1568-1590`; callers only `portdecoder_pentagon128.cpp:174,272`, `portdecoder_scorpion256.cpp:288,316,524,540`; `soundmanager.cpp:120,1533-1535` |
| SAA1099 | — | — | no SAA device in core | Missing (not stock Evo anyway) | ini `Saa1099=0` |
| MoonSound (#C4-#C7 etc., full-decode low-byte claims) | IN/OUT | claim override | handled by base `OverrideDecodeForFullDecodeClaim` before any ATM decode | Impl (add-on, not stock Evo) | `portdecoder_atm710.cpp:114-130,186-200`; tests `fulldecodeclaim_test.cpp:728-759` |
| NeoGS | — | — | only config placeholders on master; implementation on `neogs` worktree | Missing on master | `config.cpp:505-530` |
| **Nemo IDE** #10,#30..#F0 (data/regs), #11 (hi byte), #C8 (ctrl) | IN/OUT | — | not decoded; even ports collide with A0-only #FE decode (border/beeper); odd #11 → 0xFF | **Missing** | `portdecoder_atm710.cpp:358-362`; `io/hdd/*` (see §5) |
| Gluk extension regs #F0-#FF (PS/2 scancode log, versions) via CMOS | IN/OUT | — | plain RAM cells in `_cmos[]` | **Missing** | `cmos.cpp:164-166` |
| PS/2 keyboard via Gluk reg F0 | — | — | none; host keyboard feeds the #FE matrix only; ini `ATMKBD=1` not parsed | **Missing** | `config.cpp` (key absent) |
| COM / ZiFi #xxEF | IN/OUT | — | not decoded (IN → 0xFF) | Missing | — |
| Turbo control | — | — | `hw_turbo_ratio` = 4 if FF77.3 (14 MHz), else 1 if EFF7.4 (3.5 MHz), else 2 (7 MHz) | Impl | `portdecoder_atm3.cpp:314-328` |
| 14 MHz wait states / contention | — | — | none | Missing (verify need) | — |
| INT position | — | — | fixed from config (intstart 1756); BaseConf has no programmable INT | N/A | `config.cpp:916-926` |
| DMA | — | — | BaseConf has none (TSConf td §1.1 table) | N/A | — |
| NMI → window 0 = RAM page 0xFF | — | only if `EmulatorState::nmi_in_progress` | **EmulatorState::nmi_in_progress is never set at runtime** (the Z80 sets its own `cpu.nmi_in_progress`); only a unit test sets the state field | **Dead** | `portdecoder_atm710.cpp:743-747`; `platform.h:997`; `z80.cpp:990-993`; test `portdecoder_atm3_test.cpp:250-265` |
| NMI source (Magic button / #BF.3) | — | — | `Emulator::RequestNMI` generic Z80 NMI only; `RequestMNI` special-cases Scorpion only; `Z80::HandleNMI` empty | Partial | `emulator.cpp:855-870,872-`; `z80.cpp:1081-1086` |

### 1.2 Introspection gaps in the decoder surface

| Item | State | Evidence |
|:--|:--|:--|
| Port trace session model name | ATM3 → "Unknown" | `portdecoder.cpp:469-479` |
| `getPortMapEntries` (GET /ports) | no ATM710/ATM3 paging/system rows (falls to `default`); mouse row advertised if mouse present even though ATM never decodes it | `portdecoder.cpp:516-587,590-605` |
| `getPortTraceDecodeRules` | not overridden (PLAN #8) | `docs/inprogress/PLAN.md:77` |
| Paging latches (`PFFF7Window0..`) | present in `PagingLatch` enum | `portdecoder.h:188`, `portdecoder.cpp:750,845` |

---

## 2. Memory / paging

| Item | Implementation | Evidence |
|:--|:--|:--|
| RAM size | 4096 KB = 256 pages (`MAX_RAM_PAGES=256`); `ramMask = ramsize/16 - 1 = 0xFF` | `platform.h:250`; `config.h:54`; `portdecoder_atm710.cpp:680-682` |
| ROM | `zxevo.rom` 512 KB = 32 pages; accepted sizes 64/128/512/1024 KB; standard set = LAST 4 pages (28 sos, 29 dos/EVO-DOS, 30 128, 31 sys/BaseConf service); `romMask = banks-1` | `rom.cpp:98-99,209-214` (initial pages 0-3, overridden) `,325-338`; `portdecoder_atm710.cpp:684-686`; `data/configs/atm3/unreal.ini:196-209` |
| Max ROM pages | 128 (2 MB) global | `platform.h:253` |
| Window mapping (4 windows × 2 register sets by 7FFD.4) | pFFF7 type 0x000 RAM-from-7FFD (`(7FFD&7) | (fff7 & 0xF8 & ramMask)`), 0x100 ROM-from-7FFD (`(fff7&0xFE&romMask)+trdos`), 0x200 RAM-from-FFF7 (8-bit), 0x300 ROM-from-FFF7 | Impl | `portdecoder_atm710.cpp:706-741` |
| PEN=0 (aFF77.8) | all 4 windows = last ROM page | Impl | `portdecoder_atm710.cpp:695-704` |
| ~CPM=0 (aFF77.9) | forces CF_TRDOS (DOS ROM half) | Impl | `portdecoder_atm710.cpp:688-693` |
| RAM in ROM area (#0000-#3FFF) | via pFFF7[0] type 0x000/0x200 | Impl | same |
| 4 MB reach | only via #x7F7 (8-bit page); #xFF7 carries 6 bits (1 MB) | Impl | `portdecoder_atm3.cpp:378-394`; `portdecoder_atm710.cpp:581` |
| 7FFD bits 5-7 P1024 extension (EFF7.2=0) | not applied | **Missing** (see §1) | `portdecoder_atm710.cpp:715-719` |
| EFF7.3 ROCACHE | not applied | **Missing** | — |
| ROM write enable (#BF.1, verify) | ROM windows mapped read-only via `SetROMPageToBank`; no write path | **Missing** | `portdecoder_atm3.cpp:396-410` |
| Font RAM (2 KB) write redirect (#BF.2) | none | **Missing** | docs gap #2 |
| AtmMemSwap (A5-A7↔A8-A10) | deliberately not emulated (ini default off) | N/A | `portdecoder_atm710.cpp:520-526`; ini `AtmMemSwap=0` |
| Boot defaults | `reset()` clears 7FFD/EFF7/palette, `hw_turbo_ratio=1`; ATM3 adds pBD/pBE/pBF=0 + CMOS type Dallas; `ApplyBootROMDefaults`: RM_DOS → PEN+CPM+pen2, pFF77=0xE3, windows ROM1/RAM5/RAM2/RAM0; other modes → FF77 port 0 (PEN off → service ROM page 31). ini `RESET=128` → RM_128 → PEN off path | Impl | `portdecoder_atm710.cpp:33-93`; `portdecoder_atm3.cpp:25-38`; `memory.cpp:818-828`; ini `:22` |
| ROM layout comment in ini: pages 24-27 RAM disk / SD / MAGIC service; page 0 = TS-BIOS (must not boot) | documentation only | — | `data/configs/atm3/unreal.ini:200-209` |
| Memory API `total_rom_pages` reports **4 (64 KB)** for ATM3 although 32 pages are loaded | inaccurate | `core/automation/webapi/src/api/state_memory_api.cpp:352-376`; CLI `cli-processor-state.cpp:651-672` |

---

## 3. ROM set & config (`data/configs/atm3/unreal.ini`)

ROM: `[ROM] ATM3=rom/zxevo.rom` (`unreal.ini:199`) → `config.atm3_rom_path` (`config.cpp:202`) → `rom.cpp:98,214`.
`data/rom/zxevo.rom` = 524288 bytes (512 KB BaseConf+TSConf combined image; BaseConf uses pages 28-31 per ini,
TSConf pages 0-3 per TSConf TODO). Boot target = **BaseConf service ROM / "EVO Reset Service"** (page 31), EVO-DOS in
page 29 (`zxevo_boot_test.cpp:1-20`). No ATM3/Evo ROMs under `testdata/` (only `testdata/machines/atm/*.trd/.scl`
ATM software).

| ini key | Value | Parsed by config.cpp? | Evidence |
|:--|:--|:--|:--|
| `[MISC] HIMEM` / `RAMSize` | ATM3 / 4096 | yes | `config.cpp:553-557` |
| `[MISC] RESET` | 128 | yes → RM_128 | `config.cpp:147-171` |
| `[MISC] CMOS` | DALLAS | **no** ("MISC::CMOS sub-section" empty comment); ATM3 hard-codes Dallas in `reset()` | `config.cpp:173`; `portdecoder_atm3.cpp:35-37` |
| `[MISC] ZC` | 1 | **no** | key absent from parser |
| `[MISC] SMUC`, `Cache`, `EFF7mask`, `ULAPLUS`, `TS_VDAC*`, `Modem`, `ZiFi` | — | **no** | parsed-key list (grep of `GetValue/GetLongValue`) |
| `[ZC] SDCARD=wc.img`, `SDDelay=1500` | — | **no** (`zc_sd_card_path` field exists but nothing reads/writes it) | `platform.h:716`; only `[NGS] SDCARD` alias parsed `config.cpp:514` |
| `[HDD] Scheme=NEMO-DIVIDE`, `Image0=wc.img`, CHS/LBA/RO/CD | — | **no** ("// HDD section" empty); `IDE_CONFIG ide[2]` field unused | `config.cpp:299`; `platform.h:374,530` |
| `[INPUT] Mouse/Wheel/SwapMouse/MouseScale` | KEMPSTON | yes (but ATM3 never decodes mouse) | `config.cpp:259-297` |
| `[INPUT] KJoystick`, `ATMKBD`, `Matrix`, `Joy` | 1 | **no** | — |
| `[BETA128] Beta128/Noise/Traps/Fast/IL/BOOT` | 1 | yes | `config.cpp:251-258` |
| `[SOUND] GSType=Z80`, `GSRamSize=512`, `GSReset` | | yes | `config.cpp:428-503` |
| `[SOUND] SD=1`, `CovoxFB=1`, `CovoxDD` | | yes (device created but unreachable on ATM3) | `config.cpp:302-304` |
| `[SOUND] MoonSound=1` | | yes | `config.cpp:327` |
| `[SOUND] Saa1099=0` | | no | — |
| `[SOUND] TurboSound` | absent → AY pair (default) | yes | `config.cpp:373-400` |
| `[AY] Chip=YM2203`, `Scheme=AYX32` | | deliberately **not** honoured | `config.cpp:373-377` |
| `[NGS] RamSize=2048` | | yes (placeholder) | `config.cpp:511` |
| `[ULA] Frame/Line/intstart/intlen` | 69888/224/1756/32 | yes | `config.cpp:916-926` |
| `[AUTOLOAD] diskA=boot.$b` | | (not checked in this audit) | — |
| `[COLORS] ATM=...`, `UsePalette=1` | | not relevant (ATM palette is port-driven) | — |

---

## 4. RTC / NVRAM (CMOS)

| Item | State | Evidence |
|:--|:--|:--|
| Class `CMOS` (DS12885 style) owned by `PortDecoder_ATM3::_cmos`; survives `Core::Reset` (lives in decoder) | Impl | `portdecoder_atm3.h:31-33`; `core/src/emulator/memory/atm/cmos.h:16-83` |
| Shared register map enum (`CMOSMemoryEnum`) | `core/src/emulator/io/rtc/ds12885.h:16-39` |
| Time regs 0/2/4/6/7/8/9 from host local time (BCD unless reg 11 bit 2), sampled ≤ 2×/s; reg 10 = `0x20|low nibble`; reg 11 = `(reg11 & 4) | 2` (24h forced); reg 12 UF flag; reg 13 = 0x80 | Impl | `cmos.cpp:84-170` |
| Deterministic clock `SetFixedTime/UseLiveTime` | Impl | `cmos.cpp:39-48` |
| Alarm regs 1/3/5, reg 12 interrupt flags other than UF, periodic/alarm IRQ | Missing | `cmos.cpp:126-167` |
| RAM cells 14..255 | plain array | Impl |
| `_cmos[0x100]` initial contents | **not initialized** (empty ctor) — undefined/garbage at power-on | `cmos.h:20`, `cmos.cpp:9-11` |
| Persistence to host file (battery-backed) | **none** | — |
| Gluk extension F0-FF (config/bootloader version, PS/2 scancode log, modes) | **Missing** (plain cells) | `cmos.cpp:164-166`; spec TSConf hs §9 |
| I2C EEPROM "NVRAM" (`SetNVRAMAddress/WriteNVRAM/ReadNVRAM`) | **stubs**; state machine commented out | `cmos.cpp:17-32,172-281` |
| Ini `CMOS=DALLAS` | ignored; Dallas hard-wired | `portdecoder_atm3.cpp:35-37` |
| `io/rtc/smucnvram.*` | Scorpion SMUC only, not ATM3 | `io/rtc/smucnvram.h` |
| EFF7.7 gating of CMOS | not implemented (verify on BaseConf RTL) | `portdecoder_atm3.cpp:264-275` |
| TTD | CMOS contents deliberately not captured; `AtmPagingState.cmos_addr` saves `EmulatorState::cmos_addr`, **which ATM3 never uses** (the live latch is `CMOS::_cmos_addr`) → CMOS address latch is not actually restored | `ttdatmpaging.h:18-21,46`; `ttdatmpaging.cpp:32,66`; `platform.h:1063`; `cmos.h:22` |

---

## 5. Storage

| Item | State | Evidence |
|:--|:--|:--|
| Beta128 / WD1793 | Impl (shared FDC), ungated on ATM3; `#FF` DDEN masked | `portdecoder_atm710.cpp:300-321` |
| Disk autostart | refused on ATM3 ("boots disks through the BaseConf menu") | `core/src/emulator/io/fdc/diskautostart.cpp:48-51`; test `diskautostart_test.cpp:410` |
| vdos / virtual TR-DOS | none for ATM3; `tsconf.h:293-294` has unused `vdos` fields | — |
| IDE: `core/src/emulator/io/hdd/` | `hdd.h` = declarations of `ATA_DEVICE`/`ATA_PORT`/`HDD` (ported header only); **no method definitions** except `HDD::Reset` zeroing 5 `ide_*` state bytes; `hddio.h` ASPI decls; no port wiring on any model | `io/hdd/hdd.cpp:1-20`, `io/hdd/hdd.h:8-163`, `core.cpp:616` |
| SD card / SPI | **no implementation on master**; ATM3 #xx57 stub; `PortTag::Storage` doc mentions SD | `portdecoder_atm3.cpp:45-56,104-111`; `portdecoder.h:111,130` |
| `SdCardSpi` / `SpiDevice` | exist only in worktree `scratch/wt-neogs` (branch `neogs`, uncommitted): `core/src/emulator/io/sdcard/sdcardspi.{h,cpp}`, `core/src/emulator/io/spi/spidevice.h` | `git worktree list` |
| Z-Controller | only the #57 stub | — |

---

## 6. Video (brief — detail in `docs/inprogress/2026-09-15-atm-baseconf-highres-ports/`)

| Mode | Trigger | Renderer | Evidence |
|:--|:--|:--|:--|
| ZX 256×192 | FF77=3, EFF7 z0=z5=0 (or both) | generic ZX | `screen.cpp:387-424` |
| ALCO 16-color 256×192 (M_P16) | FF77=3, EFF7.0 | `DrawAlcoMode`, ATM 312-line timing override | `screen.cpp:411-418,1071-1077` |
| HW multicolor 256×192 (M_PMC) | FF77=3, EFF7.5 | same | same |
| EGA 320×200 16c (M_ATM16) | FF77=0 | `screenatm.cpp` | `screen.cpp:395-401` |
| Hi-res/HW MC 640×200 (M_ATMHR) | FF77=2 | `screenatm.cpp` | same |
| Text 80×25 (M_ATMTX) | FF77=6 | `screenatm.cpp`, built-in `ATM_FONT` | `screenatm.cpp:179-194` |
| Linear text (M_ATMTL, ATM3 only) | FF77=7 | dedicated RAM page 8/10 | `screenatm.cpp:140-163` |
| FF77=1/4/5 | M_NUL fallback | | `screen.cpp:403` |
| Palette | 16-cell #FF DAC (active-low 2 bpc) → `atmPalette[16]`; extended/z-modes use it; plain ZX mode still uses fixed `spec_colors` (docs gap #3) | `portdecoder_atm710.cpp:618-655` |
| Font RAM | not emulated; fixed `ATM_FONT` (`video/atm/atmfont.h`); #0EBE → 0xFF | docs gap #2 |
| Border | 4-bit (FE A3 bright) through palette | `portdecoder_atm710.cpp:207-211` |
| Raster | 312×224 in every mode | `config.cpp:916-926`; `screen.cpp:1071-1077` |

---

## 7. Sound devices for ATM3 (default config)

| Device | Config | Reachable on ATM3? | Evidence |
|:--|:--|:--|:--|
| Beeper (FE bit 4) | always | yes | `portdecoder_atm710.cpp:205` |
| AY / TurboSound (2×AY by default, chip select via #FFFD) | `TurboSound` absent → AY | yes (#FFFD/#BFFD) | `config.cpp:373-400`; `soundmanager.cpp:67-108` |
| General Sound (LLE Z80, 512 KB) | `GSType=Z80`, `GSRamSize=512` | yes (#B3/#BB/#33) | ini `:130-134`; `portdecoder_atm710.cpp:163-167,290-299`; test `portdecoder_atm3_test.cpp:431` |
| MoonSound (OPL4) | `MoonSound=1` | yes (full-decode claims) | ini `:139`; `soundmanager.cpp:156-161,1554-1556` |
| Covox #FB / SounDrive | `CovoxFB=1`, `SD=1` | **no** — device created but ATM decoders never dispatch self-decoding devices; `.recipe/peripherals/covox-sounddrive.md:21-23` says "ship SD=1 ... not verified" | `soundmanager.cpp:120-122,1533-1535` |
| SAA1099 | `Saa1099=0` | no device | — |
| NeoGS | placeholder only | no | — |

Note: stock ZX-Evo BaseConf has AY(+TS?), beeper, Covox #FB and optional GS/NeoGS; MoonSound is an add-on. (verify
BaseConf sound set against RTL.)

---

## 8. TTD and automation

| Item | State | Evidence |
|:--|:--|:--|
| `GetTTDModelStateIds()` | `{PeripheralId::AtmPaging}` (id 8), inherited from ATM710 | `portdecoder_atm710.cpp:826-836`; `ttdserializable.h:52` |
| `AtmPagingState` (44 B): pFFF7[8], aFF77, pBD, pBE, pBF, aFE, aFB, atmMemSwapped, cmos_addr | Impl | `debugger/ttd/atm/ttdatmpaging.h:36-51` |
| pFF77, pEFF7, p7FFD, nmi_in_progress (CPU) | in generic TTDChipsetState / CPU state | `ttdcheckpoint.h:161-164`, `ttdcheckpoint.cpp:66,149-152` |
| **Not captured**: `atmPalette[16]`, `atmPaletteRegs[16]`, `atmBorderBright`, CMOS RAM + CMOS address latch (`CMOS::_cmos_addr`), decoder-held `_7FFD_Locked` (unused on ATM3) | gaps | grep of `core/src/debugger/ttd` finds no atmPalette/atmBorderBright |
| TTD comment "pBF.0 (shaden) also gating the FDC" | inaccurate — no FDC gating exists | `ttdatmpaging.h:16-17` |
| Contract test includes ATM3 | yes | `core/tests/debugger/ttd/ttdmodelstatecontract_test.cpp:52` |
| WebAPI models/creatable | generic (`IsModelCreatable`) | `lifecycle_api.cpp:138,203-211` |
| MCP help lists ATM3 | yes | `core/automation/mcp/src/mcp-tools.cpp:120` |
| Memory info ROM pages | hard-coded 4 pages for ATM3 (wrong for 512 KB image) | `state_memory_api.cpp:361-366`; `cli-processor-state.cpp:661` |
| Port trace model name / port map rows | Unknown / none | §1.2 |
| Recipe | `.recipe/machines/atm.md` (ATM710 + ATM3) | `.recipe/machines/atm.md:1-150` |
| `DeviceState` / CMOS debug surface | only `GetCMOS()` accessor for tests; no automation endpoint for CMOS/SD | `portdecoder_atm3.h:49-51` |

---

## 9. Existing tests touching ATM3

| File | ATM3-relevant tests |
|:--|:--|
| `core/tests/emulator/ports/models/portdecoder_atm3_test.cpp` (+ `.h`) | 17: IsPort_FF77_PartialDecode, IsPort_37F7, IsPort_BF, Reset, ApplyBootROMDefaults_InheritedFromATM710, InheritsPort_7FFD, InheritsPort_EFF7, IsPort_FFF7_NarrowerDecode, Port_37F7_Encoding_PreservesRAMType, Port_37F7_MapsTopRAMPage, NMI_ForcesTopRAMPageAtWindow0 (sets state flag directly), Turbo_FF77Bit3_EFF7Bit4_MultiplierSelect, IsPort_ATM_Palette_ExactFFDecode, PaletteFF_ManagerGate, Port_7FFD_LockOnlyWithEFF7Lockmem, PortBE_ReadbackRegisters, GSHostPortsReachBaseDecodeThroughOverrides |
| `core/tests/emulator/machines/zxevo/zxevo_boot_test.cpp` | ZXEvoBoot_Test.BootsToInteractiveServiceShell, MenuKeyUBoots128KBasic (real `zxevo.rom`) |
| `core/tests/emulator/emulatormanager_test.cpp:96` | CreateZXEvo_BootsBaseConfRomSet |
| `core/tests/emulator/video/atm_video_modes_suite_test.cpp` | ModeMatrix_ATM3_FF77AllValues, ModeMatrix_ATM3_EFF7ZBits_HierarchicalDecode, PortFF77_ATM3PartialDecode_MatchesXF77, PortEFF7_ControlBitsStored_ZBitsTriggerRedetection, Render_ATMTL_*, Render_ATM3Alco_FourPlanes_PagePair, Render_ATM3Hwmc_*, Timing_ATM3ZModes_KeepAtm312LineFrame, palette/border tests (33 tests total, 31 ATM3 refs) |
| `core/tests/emulator/video/atm_videomode_test.cpp` | InitRaster_DetectsATM3LinearTextMode, LinearTextMode_NotAvailableOnATM710, Render_ATMTL_DedicatedPageContent (+ generic ATM) |
| `core/tests/emulator/video/screen_test.cpp:150` | DescribeBeam_Atm3AlcoKeepsAtmRaster |
| `core/tests/emulator/video/int_timing_test.cpp:394-434` | ApplyDefaults_ATM_CanonicalGeometryIsBaseClockRaster, ATM_INTToZXPaperMatchesReference, ShippedAtmConfigs_FrameEqualsRasterInEveryMode |
| `core/tests/emulator/cpu/int_test.cpp:393` | IntPulse_Test.ZxEvoAcknowledgeEndsThePulse |
| `core/tests/emulator/io/fdc/diskautostart_test.cpp:410` | DiskAutostart_Unsupported.Atm3ReportsReason |
| `core/tests/emulator/ports/fulldecodeclaim_test.cpp:728-759` | FullDecodeClaim_ATM3_Test.{Claimed_DirtyWrites_StandDown, Claimed_OwnPortsUnaffected, Claimed_FMStatusRead_ServedByCard} |
| `core/tests/debugger/ttd/atm/ttdatmpaging_test.cpp` | 7 TtdAtmPaging tests (layout, round trip, hash, registry, decoders declare serializer, seek restores map, seek restores clock across turbo) |
| `core/tests/debugger/ttd/ttdmodelstatecontract_test.cpp:52` | model loop includes "ATM3" |
| `core/tests/emulator/video/videomode_change_test.cpp`, `core/tests/emulator/sound/tsfm/tsfm_volume_replay_test.cpp`, `portdecoder_atm710_test.cpp` | incidental mentions (zxevo/atm3 string) |

No tests for: CMOS via ports on ATM3, Z-Controller #57, SD, IDE, mouse/joystick, Covox on ATM3, #BF bits 1-3, #BD, font RAM.

---

## 10. Reusable designs in TSConf + NeoGS docs (for the ATM3 plan)

### 10.1 `docs/inprogress/2026-09-27-tsconf/`

| Topic | Where | Content |
|:--|:--|:--|
| BaseConf vs TSConf comparison | technical-design §1.1 (`technical-design.md:34-57`) | BaseConf = "done — the ATM3 machine"; storage "VG93 + Nemo IDE + SD"; **table claims BaseConf register readback is `#xxBD`** (our code uses #xxBE — reconcile); both share `zxevo.rom` and board peripherals (keyboard, mouse, CMOS, SD, FDD) |
| Current-state table | td §3.1 (`:317-344`) | "SD card: no implementation anywhere (ATM3 returns 0xFF on #57; `[ZC] SDCARD=` never parsed)" |
| Decisions | td §3.2 (`:346-376`) | **D2 Nemo IDE deferred to shared IDE core (PLAN #13a), ports answer 0xFF** (resolved 2026-09-29: TSConf uses the shared scheme `NEMO-DIVIDE`, on master since `f5fc5f05`); D4 no Soundrive; D7 COM/ZiFi out |
| Instruction-start hook (vdos flip at next M1) | td §3.6 (`:478-496`) | `CF_MACHINEM1` flag + `pMachineM1Hook->OnM1(pc, opcode)` interface |
| Port decoder table | td §3.7 (`:498-529`) | `0x57/0x77` SPI data/chip-select always; Beta 1F/3F/5F/7F/FF gated `DOS || FDD_VIRT[7]` + vdos arm/exit; Kempston joystick #1F `!DOS && !FDD_VIRT[7]`; Gluk CMOS reuse `memory/atm/cmos.h` **+ add extension regs F0-FF**; Kempston mouse xxDF via `Default_Port_KempstonMouse_In`; xxEF/IDE → 0xFF |
| **Storage** | td §3.11 (`:597-627`) | `SdCardSpi : SpiDevice` (`emulator/io/sdcard/sdcardspi.{h,cpp}`, `emulator/io/spi/spidevice.h`) shared with NeoGS (exists on `neogs` branch): CMD0/8/9/10/12/13/16/17/18/24/25/55/58/59 + ACMD41, SDSC/SDHC, CRC off, write modes Session/Persist/Off, deterministic latency, `readBlock/writeBlock`. TSConf adds `TsConfSpi` glue (#57 previous-byte pipeline, #77 CS → `select()`, DMA SPI path); **block-store seam `ISdBlockStore { blocks(); read(lba,buf); write(lba,buf); }`** with `FileBlockStore` and **`VirtualFatBlockStore`** (FAT32 over a host folder, Xpeccy `vfat.c` semantics: MBR, partition at LBA 2048, 2 FATs, 4 KB clusters, LFN + cp866 8.3, sectors on demand, read-only → WriteMode Off); raw image from `[ZC] SDCARD=`; media API `POST/GET/DELETE /api/v1/emulator/{id}/sd` (image or folder) + MCP/CLI/Lua/Python with one `SdCardState` descriptor. Beta-128: DOS/`FDD_VIRT[7]` gating + vdos. IDE deferred (D2) |
| TTD | td §3.13 (`:643-667`) | `PeripheralId::TsConfPaging = 13` (10 MoonSound, 11 GS-LW, 12 NeoGS reserved); SD card protocol state follows neogs-tdd §7.4; vFAT read-only → no sector journal |
| Automation | td §3.15 (`:683-698`) | SD media endpoints, recipes |
| Risks | td §3.20 item 6 (`:763`) | IDE-booting software fails until D2 revisited (resolved 2026-09-29, see D2 above) |
| SD over SPI (hardware) | hardware-spec §8.1 (`hardware-spec.md:493-510`) | #57 W start exchange; #57 R returns **previous** exchange byte and sends 0xFF; #77 W bit1 SD CS_n (0 = selected), bits 2/3/4 FT812/SD2/ESP CS; **#77 R = 0x00 constant**; decoded in every mode (not DOS-gated); 16 fclk per byte; DMA shares master |
| Beta-128 + vdos | hardware-spec §8.2 (`:512-525`) | VG93 ports only when `DOS || FDD_VIRT[7]`; no #9F; vdos on: IN/OUT to FDC while `DOS && !vdos` and latched drive has FDD_VIRT bit → next M1 (`pre_vdos`); vdos off: access to 1F/3F/5F/7F (not FF) while vdos; while vdos: FF writes only drive bits, W0 = RAM 0xFF writable, INT gated, CMOS reachable; "virtual drive" = Z80 code in page 0xFF, nothing host-side |
| Nemo IDE | hardware-spec §8.3 (`:527-531`) | in standard build; v1 defers (D2), decoder answers 0xFF (resolved 2026-09-29: scheme `NEMO-DIVIDE` + `IdeAdapter::DmaReadWord/DmaWriteWord` for DMA 0x3 / 0xB) |
| Other peripherals | hardware-spec §9 (`:533-544`) | Gluk CMOS: low byte F7, A8=1; #DFF7 addr, #BFF7 data, #EFF7 bit 7 = CMOS enable; reachable when `(EFF7[7] || DOS) && (!DOS || vdos)`; Gluk extension F0-FF (0 config ver, 1 bootloader ver, 2 PS/2 scancode log, 3 config/modes; reg 0x0C=0 disables EEPROM mode first); Kempston mouse xxDF (A8=0 → {wheel,1,btn}; A8=1 → A10?Y:X); Kempston joystick #1F 8-bit only `!DOS && !FDD_VIRT[7]`; COM/ZiFi #xxEF → 0xFF in v1; floating bus 0xFF; NMI not generated (TSConf) |
| Phase IDs | implementation-plan §1 (`implementation-plan.md:26-50`) | P0 infrastructure (INF-1..10) → P1 decoder+memory (DEC-1, TTD-1, ROM-1, BOOT-0, MEM-*, P7F-*, FM-*, CCH-*, REG-*, RST-*) → P2 INT+clock (INT-*, CLK-*) → P3 engine+ZX video (ENG-*, VID-*) → P4 TSU (GFX-*, TSU-*) / P5 DMA (DMA-1..14) → **P6 storage + SPG** (`:200-217`: **SPI-1, SD-0, BLK-1, VFAT-1..3, BETA-1, VDOS-1..2**, SPG-1..3, **API-1**, BOOT-3) → P7 surfaces (SND-1..3, DBG-1..3, AUTO-1, TTD-5) → P8 timing (TIM-1..5). Dependencies: PLAN #40 V0 before TTD; NeoGS `SdCardSpi` before P6 |
| TODO | `TODO.md:34-42` | not started; prerequisites incl. NeoGS SdCardSpi merge/lift before phase 6; revisit D2 with PLAN #13a |
| Reference note | references.md `:10-30` | `pentevo/tools/unreal_fix/0.39.0/` = Unreal patched for **baseconf-era** ZX-Evo: Z-controller IDE `zc.cpp`, SD loader `sdcard.cpp`/`TSdCard`, `gsz80.cpp` — the natural ATM3 porting source |

### 10.2 `docs/inprogress/2026-09-19-general-sound/neogs-tdd.md`

| Topic | Section | Content |
|:--|:--|:--|
| Existing state | §1.3 (`:86-105`) | no SD/SPI/flash/MP3 in core; ATM3 Z-Controller port is a stub; nothing parses `[ZC] SDCARD` |
| SPI master timing | §3.7 (`:423-474`) | byte time 16 clocks at /2; read before completion returns previous byte; inclusive boundary; restart on new start; CPU+DMA share SD master |
| Components | §5.4 (`:991-1015`) | `SdCardSpi` in `emulator/io/sdcard/sdcardspi.{h,cpp}`; **"One SD card model for the whole emulator"** — NeoGS, TS-Conf, Z-Controller reuse behind their own port glue; shared `SdCardState` automation descriptor; also `NeoGSSpi` (card-side glue), `Flash29F040B` |
| `SdCardSpi` spec | §5.5 (`:1017-1068`) | SPI-mode v1/v2, SDSC/SDHC; commands table (CMD0, 8, 55+ACMD41 (4 idle calls), CMD59, 16 (512 only), 58 CCS, 17/18+12, 24/25 tokens #FC/#FD, 9/10/13; others "illegal"); CRC off except CMD0/8/after CMD59; SDSC byte vs SDHC block addressing; image ≤2 GB → SDSC else SDHC, pad to 512; detect = image present; `SDWriteProtect` (slot switch bit only) vs `SDWrite=off` (reject: data response #0D, WP_VIOLATION); writes `session` (overlay map, discarded) / `persist` / `off`; fixed latency (read token after 8 poll bytes, write busy 64); TTD: protocol state + overlay, persistent writes only on live runs |
| Config | §6 (`:1229-1265`) | `[NGS] SDCardImage` (alias `SDCARD`), `SDWriteProtect`, `SDWrite=session|persist|off` |
| TTD | §7.4 (`:1375-1416`) | `PeripheralId NeoGS = 12`; SD protocol state in blob; bulk (RAM, flash, SD overlay) as TTD v2 memory regions; `TTDCanRecord(why)` refusal mechanism until v2 regions |
| Automation | §7.5 (`:1418-1428`) | `gs sd insert/eject`, WebAPI `sd_insert/sd_eject`; SD fields follow shared `SdCardState` |
| Phases | §9 (`:1495-1516`) | P0 runner/GSAudioOut/GSModuleReplay; P1 NeoGS core; **P2 `SdCardSpi` + `NeoGSSpi` + flash programming** (FAT16/FAT32 image boot); P3 MP3 + SD/MP3 DMA; P4 switching/automation/TTD refusal; P5 ZX-DMA hook; P6 NeoGS TTD on v2 regions |
| Decision 16 | §11 (`:1557`) | "one shared `emulator/io/sdcard/SdCardSpi` for NeoGS, TS-Conf and Z-Controller" |
| Status | memory note + worktree | implementation in `scratch/wt-neogs` (branch `neogs`, uncommitted): `core/src/emulator/io/sdcard/sdcardspi.{h,cpp}`, `core/src/emulator/io/spi/spidevice.h` exist |

### 10.3 PLAN cross-refs

- PLAN #13a (Profi remainder) = "Build the IDE once for Profi + TSConf" (Profi IDE design: `docs/inprogress/2026-09-21-profi/technical-design.md` §14) — `PLAN.md:63`.
- PLAN #41 TSConf, reuses NeoGS `SdCardSpi` (#45) — `PLAN.md:62`.
- PLAN #8 port-trace rules: ATM710/ATM3 report "Unknown" — `PLAN.md:77`.
- PLAN #53 ATM verification gaps (ATM3 font RAM upload, plain-ZX palette routing, …) — `PLAN.md:92`.

---

## 11. Gap summary (ATM3 vs real BaseConf)

Missing: Z-Controller SD (#57 real SPI, #77 CS/status), SD image/vFAT, `[ZC]` parsing; Nemo IDE (+ ATA core bodies,
`[HDD]` parsing) and the A0-only #FE decode that would swallow IDE ports; Kempston mouse and joystick decode; Covox #FB
dispatch (dead `CovoxFB/SD` config); #xxBD; #BF bits 1 (ROM write), 2 (font RAM), 3 (NMI); NMI → page 0xFF path (dead
state flag) and #BE exit counter; font RAM + #0EBE; EFF7.3 ROCACHE; 7FFD bits 5-7 P1024 page extension; EFF7.7 CMOS
gating (verify); Gluk F0-FF extension incl. PS/2 log; CMOS persistence and deterministic init; I2C NVRAM; FDC DOS
gating (FDC answers outside DOS) and vdos; COM/ZiFi; 14 MHz wait states (verify).
Surface gaps: port trace/port map rows, ROM page count in memory API, TTD for palette/border-bright/CMOS latch.
