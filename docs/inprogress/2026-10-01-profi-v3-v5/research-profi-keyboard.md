# Profi keyboard: what to emulate

Date: 2026-10-03. Confidence: H = read directly from a schematic, netlist or code, or reproduced by running the firmware. M = two or more indirect sources agree. O = open or a single weak source.

Materials (outside the repo): `materials/keyboard/` holds the 8035 simulator, the experiment outputs and a reconstructed firmware image (README there). The simulator is also in the repository: [tools/machines/profi/xtkbd/](../../../tools/machines/profi/xtkbd/README.md); the image is [data/rom/profixt/](../../../data/rom/profixt/README.md).

**Implemented** 2026-10-03 (merged into master): [design.md](design.md) section 9. Two statements below were corrected by running the firmware on the emulator's MCS-48 core; they are marked *Correction*.

## Summary

- **v5.0x boards (and v4.01).** The 20-pin keyboard connector X9 carries:
  - the 40-key matrix: KA8-KA15 and KD0-KD4;
  - a 6th data line, KD5, which reads as bit 5 of #FE;
  - /KBW, a WAIT line into the Z80;
  - /CSKBD, the keyboard-read strobe;
  - /HRESET, a reset line.

  Either a plain mechanical matrix keyboard or the PROFI-XT controller plugs into X9. H.
- **PROFI-XT controller.** It is a 1816VE35 (8035) MCU with a 2 KB 573RF2 EPROM, firmware "JV KRAMIS 1.27, 28.10.1992".
  - It does not send scan codes to the Z80. It acts as a virtual matrix.
  - On every Z80 read of #FE while a key is held, it stretches the cycle with WAIT. It then reads A8-A15 and puts KD0-KD5 on the bus.
  - Keys the Spectrum lacks are a letter plus an extra key called EXT. EXT sits at KD5 of half-row A14 (#BFFE).
  - H, from the schematic plus running the firmware in a simulator.
- **v3.2 board.** The 16-pin KEYB connector has no KD5 and no WAIT. Its pin 2 carries KD7 (bit 7 of #FE).
  - The board also has "IBM PC/XT KEYBOARD" pads: the keyboard clock is ANDed into the Z80 /INT, and the keyboard data goes to bit 7 of #FE.
  - The XT controller's extra keys therefore do not work on a stock v3.2. H for the schematic and forum. O for which v3.2 software (if any) used the direct path.
- **BIOS 2.0 / the CP/M system ROM.**
  - The scan reads 6 bits per half-row.
  - It collects KD5 of all 8 half-rows into a byte at system variable 0x99DA.
  - It tests bit 6 of that byte (A14) as "EXT" and remaps A..P to F1-F10, Home, End, PgUp, PgDn, Ins, Del.
  - The Kramis BIOS reads only 5 bits.
  - H.
- **Other emulators.** ZXMAK2, Karabas-Pro and Xpeccy-plus put "EXT" on bit 5 for every half-row, globally. pico-spec puts it on the letter's own half-row. All four are wrong against the real controller and the BIOS, which use A14 only. All four agree with the firmware on the F-key and navigation map. H.

## 1. Keyboard hardware per board

| Board | Connector | Lines | D5 / extra | WAIT | Source | Conf. |
|---|---|---|---|---|---|---|
| v3.2 (Kramis) | KEYB, 16 pins | KA8-KA15 via the U11 latch (555IR23) and diodes X7-X14; KD0-KD4 into U13 (555IR23, read buffer of #FE); pin 8 /HRESET; pin 2 DATAKEY → U13 D8 → **DC7 (bit 7)** | no KD5 | no | MDESK 2020 retrace `profi32cl-mdesk-sch.pdf` sheet 7 (rendered and read). Forum zx-pk 14599 p.35: "on Profi 3.2, pin 2 of the keyboard connector carries KD7, not KD5 as on Profi 4-5" | H |
| v3.2 | "IBM PC/XT KEYBOARD" pads PAD3-6 | CLCKEY → U62:A (555LI1, AND) with /INT → /INTCPU, so every XT clock pulse interrupts the Z80. DATAKEY → bit 7 of #FE | - | - | same sheet 7 | H (wiring) / O (software using it) |
| v4.01, v5.0x | X9, IDC-20 | KA8-KA15 via DD17 (IR22, latched on F2T, OE=/CSKBD) and diodes VD5-12. KD0-KD5 into DD16 (IR22, OE=/CSKBD) → DC0-DC5. DD16 D6=MAGIN, D7=GX0. /KBW → DD18.13 (AND) → /READY (Z80 WAIT). /HRESET on X9.16 | **KD5 = bit 5**, pulled up by R10 | **yes** | v5.06 netlist `netdump.txt` (X9, DD16, DD17, /KBW nets). `PROF5-0B/0D` text schematics. Manuals: 5.0 p.10 and 4.01 p.22, "Клавиатура D0-D4 (D5) /r FE" | H |
| v5 | /CSKBD decode | DD19: OR(A0, /IORQ) → OR with /RD. So any even port read, A0=0 | - | - | `PROF5-04` + netlist | H |
| v5 mechanical keyboard | sheet 9 (`PROF5-0D`) | Spectrum 8×5 matrix plus combination keys (EDIT, CAPS LOCK, INV, cursor keys, GRAF, DELETE, EXT, BREAK, MODE) that close two contacts | none on KD5 | - | `PROF5-0D.utf8.txt` | M |

Manual 5.0 p.2: the optional controller allows a "PC/XT keyboard both in CP/M and in Sinclair mode" and fits earlier Profi boards and "any Sinclair". H.

Forum zx-pk 21644 p.7 (Vadim, 2014): an XT controller was developed in 1993-94 that "supported 16 additional keys … one more data bit is used". M.

## 2. The PROFI-XT controller

### Hardware (`PROFI-XT.PDF`, rendered at 250 dpi and read)

| Part | Function |
|---|---|
| DD1 1816VE35 (8035), 8 MHz crystal | MCU. EA=1, so it runs only from external memory. |
| DD4 573RF2 (2716) | firmware. A0-A7 come from DD2 (IR22 address latch on ALE), A8-A10 from P20-P22. /CS = P23, /OE = /PSEN. |
| P10-P17 | inputs from **BA8-BA15** (Z80 A8-A15) |
| DD3 IR22 | output latch: DK0-DK5 (Q0-Q5), plus Q6/Q7 from AD7 (RESC, RES). Clocked by OR(A7, /WR), so a `MOVX @Rx` with address bit 7 = 0 writes it. Its outputs are enabled only while **P27 = 1** (OE = NOT P27). |
| DD5 TM2 (second half) | WAIT flip-flop: D = P27, clock = /CSK. /Q → **/WAIT** to the Z80, and also to MCU pin T1. Reset by OR(A5, /WR), so a `MOVX` with address bit 5 = 0 releases WAIT. |
| X1, 5-pin DIN | XT keyboard. The clock edge goes through a differentiator (jumpers SB1/SB2 pick the edge) to the MCU **/INT**. The data is sampled into DD5 (first half) at the clock edge, and /Q goes to **T0**. |
| X2, IDC-20 | matches v5 X9 pin for pin: 1 BA11, 2 DK5, 3 DK3, 4 DK1, 5 DK2, 6 DK4, 7 DK0, 8 RES (AD7 through a jumper), 9 /WAIT, 10 /CSK, 11 +5 V, 12-13 GND, 14 BA15, 15 BA14, 16 BA12, 17 BA13, 18 BA8, 19 BA9, 20 BA10 |

(The v5 sheet-9 numbering, KA11=1 … CSKBD=10 and KA10=20, is the same connector.) H.

### Firmware

`speccy4ever/rom/PROFI_XT-9A8E2686.ROM`, 2048 bytes, CRC32 9A8E2686 (verified). It is the same file as `keyboard/profi-xt-keyboard-controller.rom` of the materials collection. The banner at 0x7A0 reads "JV KRAMIS (C) 28.10.1992 vers 1.27, no autoselect, fool-proof, no wait!".

**The dump is damaged.** The bytes at 0x02E-0x032 are `C8 11 20 17 37`.
- The ROM contains no `EN I` anywhere. Without it the receive interrupt at 003h → 069h can never run.
- The main loop at 02Fh (the target of every `JMP 02Fh`) has to fetch a byte, and the only fetch routine is at 05Fh.
- Replacing the bytes with `05 14 5F 00 00` (EN I; CALL 05Fh; NOP; NOP) makes the firmware work in simulation for every key.
- So this is a reconstruction, not a verified dump. H that the dump is broken; M that the patch matches the original. No other dump was found on the web.

How the firmware works (8035 simulator plus the board model above; outputs in `sim/keymap-*.txt`):

| Item | Behavior | Conf. |
|---|---|---|
| Receive | interrupt per XT clock; 9 bits (start bit = 1, then 8 data bits, LSB first). Make codes 01-58h; break = make\|80h. E0 prefix handled (handler at 21Ah). E1 (Pause) swallows 5 bytes. | H |
| Internal state | RAM 0x20-0x27 = one byte per half-row A8..A15 (bits 0-5 = KD0-KD5). 0x28 = AND of all half-rows. 0x29 = FFh. | H |
| Idle | while no key is held, P27 = 0. The Z80 then gets no WAIT, and the latch is off, so the pull-ups read KD0-KD5 = 1. ("no wait!" means no wait while idle.) | H |
| Key held | P27 = 1. Every /CSK (Z80 IN from an even port) sets WAIT. The MCU sees T1 = 0 at its next poll, reads P1 = A8-A15, writes the answer to the latch and releases WAIT. | H |
| Which half-row it answers | A15-A8 = 00h: AND of all half-rows. Exactly one A-line low: that half-row. **Any other pattern** (two to seven half-rows selected, such as #7EFE or #FCFE): FFh, meaning "no key". A15-A8 = AAh: sets mode bit R7.6 and returns 00h. 55h: clears it and returns 00h. *Correction:* the firmware's decode table at 400h looks at A8-A11 first: exactly one of them low selects that half-row and A12-A15 are not looked at, so #7EFE answers half-row A8; only with A8-A11 all high does it decode A12-A15 the same way. Two low lines in one nibble (#FCFE, #3FFE) give "no key". | H |
| WAIT length | about 26-58 MCU machine cycles (1.875 µs each at 8 MHz), so about 50-110 µs, or about 175-380 T-states at 3.5 MHz. It is longer if the MCU is busy (inside the receive interrupt or processing a code). *Correction:* the read path from the idle loop is 49 cycles plus up to 8 for the poll: 49-60 cycles (92-113 µs, 320-395 T) on the emulator's core, up to ~70 while the MCU is busy. | M (cycle counts approximate) |
| Ctrl+Alt+Del | any Ctrl + any Alt + Del (plain or E0) writes latch 7Fh. Bit 7 = 0 drives RES, which is X2 pin 8 → X9 /HRESET, a machine reset. | H |
| ScrollLock | toggles R7.6, the same mode bit as #AAFE. In that mode \` gives SS+X instead of SS+W, and Left Shift becomes a separate key at **A15/KD5** instead of SS. | H |
| NumLock | keypad digits by default; after a NumLock press, the keypad acts as cursor/navigation keys | H |
| Rollover | several keys at once are reported (tested with 4 letters and with 2 F-keys) | H |

### Key map from the firmware (default mode)

"EXT" means KD5 = 0 when half-row A14 (#BFFE) is read.

| PC key (set 1) | Profi matrix | PC key | Profi matrix |
|---|---|---|---|
| letters, digits, Space, Enter | same key | Esc | CS+1 (EDIT) |
| Backspace | CS+0 | Tab | CS+I |
| Left/Right Ctrl | CS | Left/Right Shift | SS |
| Left Alt | SS+Enter | Right Alt | SS+Space |
| CapsLock | CS+SS | F1…F10 | A…J **+ EXT** |
| F11 / F12 | SS+Q / SS+W | Home / End | K / L + EXT |
| PgUp / PgDn | M / N + EXT | Ins / Del | O / P + EXT |
| Up / Down / Left / Right | CS+7 / CS+6 / CS+5 / CS+8 | KP Enter | Enter |
| - = | SS+J, SS+L | Shift+- / Shift+= | SS+0 (_) / SS+K (+) |
| [ ] | SS+Y, SS+U | Shift+[ / ] | SS+F / SS+G ({ }) |
| ; ' | SS+O, SS+7 | Shift+; / ' | SS+Z / SS+P (: ") |
| \` (backquote) | SS+W (mode: SS+X) | Shift+\` | SS+A (~) |
| \ | SS+D | Shift+\ | SS+S (\|) |
| , . / | SS+N, SS+M, SS+V | Shift+, . / | SS+R, SS+T, SS+C (< > ?) |
| KP * / KP - / KP + / KP . / KP / | SS+B / SS+J / SS+K / SS+M / SS+V | Shift+digit | SS+digit (not translated: Shift+6 gives &, not ^) |

## 3. How the software reads it

| Software | Code | What it reads | Conf. |
|---|---|---|---|
| BIOS 2.0 (`bios20.rom` bank 0, 0x0600) | `LD BC,#FEFE / IN A,(C) / CPL / AND 3Fh / ADD A,E0h / EXX / RR L / …` over B = FE…7F | 6 bits per half-row. KD5 of each half-row is shifted into L', which is stored at **0x99DA** at 06A0 (bit n = half-row A(8+n)) | H |
| BIOS 2.0 0x127C | `LD A,(99DA) / BIT 6,A / RET Z / remap via table 0x3494` | A14 KD5 = EXT. Table: A→75h … J→7Eh (F1-F10), K→7Fh, L→80h, M→81h, N→82h, O→83h, P→84h, plus shifted and control variants | H |
| BIOS 2.0 keyboard test 0x1328 | reads all 8 half-rows; keeps `#BFFE` and tests `BIT 5` | the XT test waits on EXT at A14 | H |
| BIOS 1.0 | the same code (99DA at 0x44A / 0x1301) | same | H |
| CP/M disk `testdata/machines/profi/CPM.UDI` | ROM image inside it with the same scan (writes 99DA) and remap (`BIT 6` + table 0x365B) | same | H |
| Kramis BIOS v0.2/v0.3 | scan at 0x0879: `AND 1Fh`. Test at 0x13F9: 5 bits | no KD5. EXT keys are ignored. | H |
| Port #AAFE / #55FE | not found in any ROM or disk image in the corpus | mode switch is unused by the known software; ScrollLock is the user's switch | M |

Forum zx-pk 21356 p.14 describes the same scheme from memory, used by the Kondor ROM and MicroDOS: F1 clears the bit "on the half-row of A", and the 6th bit appears only on the "last" half-row. The firmware and BIOS say the last half-row is A14 (ENTER), not A15 (SPACE). M.

## 4. Other emulators

| Emulator | Port | Extended keys | PC mapping | Matches firmware? |
|---|---|---|---|---|
| ZXMAK2 `KeyboardProfi.cs` | #FE (mask 0x67 = FE&0x67); bits 0-4 matrix | an ext key clears **bit 5 for every read**, whatever the half-row (`value &= 0xC0`) | F1-F10 = A..J+b5, Home/End K/L, PgUp/PgDn M/N, Ins/Del O/P; Ctrl=CS, Shift=SS, LAlt=SS+Ent, RAlt=SS+Sp, F11/F12 SS+Q/W, Esc=CS+1(?), arrows CS+5-8 | map yes; bit-5 placement no |
| Xpeccy-plus `kbdScanProfi` + `keymap.cpp` (commit 6262c6ac) | #FE (A0) | bit 5 cleared when **any** half-row is selected and an ext key is held | "grab" layout equals the firmware map (Tab CS+I, \` SS+X, Caps CS+SS, Shift-pair table) | map yes; bit-5 placement no; no WAIT |
| Xpeccy (orig.) | kbd_set_type KBD_PROFI | similar | - | - |
| Karabas-Pro (FPGA + AVR) | `#FE = GX0 & !TAPE & kb(5:0)`; `KB(5) = not kb_data(40)`, global | global bit 5 ("bit6") | `ZXKeyboard.cpp` profi_mode: same map; PrtScr toggles Profi/ZX layout | map yes; bit 5 no |
| pico-spec `Ports.cpp:640` | bit 5 per half-row (`extPort[row]`) | ext key on **its letter's own** half-row | comments cite the BIOS ALT remap at 0x365B | per-row yes; wrong half-row |
| UnrealSpeccy | no Profi keyboard code found | - | - | - |

Every emulator agrees on the key map. None places EXT at A14 only, and none emulates WAIT or the multi-row "no key" quirk.

## 5. Existing unreal-ng keyboard stack (branch `profi-v3-v5`)

| Piece | What exists | Use for Profi |
|---|---|---|
| `core/src/emulator/io/keyboard/keyboard.{h,cpp}` | ZX matrix `_keyboardMatrixState[8]`, 5 bits. `HandlePortIn` ANDs the selected half-rows. Extended ZX keys split into modifier + base (`_zxExtendedKeyMap`). Routing `HostKeyboardRoute {Auto, Matrix, Ps2, Both}`; Auto becomes Both once a sink is attached. `SetPs2Sink`, `ApplyPcKey`, `WantsPcKey`. | matrix keyboard: as is. XT: attach as the sink. |
| `pckey.{h,cpp}` | `enum class PcKey` (all PC keys). `IPs2KeySink {OnPcKey, ReleaseAllPcKeys}`. `Ps2Set2Bytes` (set 2 only). `FromWindowsScanCode` is set-1 *input*. `FromZxKey` (CS→LShift, SS→LCtrl). | needs a **set-1 generator** `XtSet1Bytes`, and a Profi reverse map (CS→Ctrl, SS→Shift) |
| `ps2keyboardstream.*` | set-2 byte stream in emulated time with typematic repeat (Sprinter) | pattern for an XT stream: same timing model, set 1, 9-bit frames |
| `atm2kbc.{h,cpp}` (+ `cpu/mcs51/`) | ATM2 KBC: real ROMs on an MCS-51 core. `ReadPort` latches A15-A8, runs the MCU until it answers, and adds Z80 wait states (`AddWaitStates`). `OnPcKey` → set-2 bit-bang. TTD state blob, `PeripheralId::Atm2Kbc=26`. Config `[ATM] Kbc=`. | **the template**: the Profi XT controller has the same shape (latch the address, WAIT, answer from MCU firmware) |
| `unreal-qt/.../keyboardmanager.*` | Qt event → both `MC_KEY_*` (ZX) and `MC_PCKEY_*` (PcKey). `machineOwnsKey` gives F-keys to the machine when the route is PS/2. | no change |
| `core/src/debugger/keyboard/debugkeyboardmanager.*` | `PressKey`/`TapKey` by ZX key or PC-key name. On a PS/2 route it derives PC keys via `pckey::FromZxKey`. `TypeText` uses `FromCharacter`. | needs a per-sink ZX→PC map (Profi: CS→LCtrl, SS→LShift) |
| TTD input kinds `Key` / `PcKey` (`ttdinputjournal.h:72`, `ttdinputapply.cpp`) | PcKey is replayed into the attached sink | no change. The device state needs a new `PeripheralId` (after Vdac2=43) and a serializer. |
| Profi decoder `portdecoder_profi.cpp:313` | #FE → `Default_Port_FE_In`; bit 5 always 1. Bit 7 = GX0 on v5 DS80. `ProfiBoard::For()` holds per-board fields. | add the keyboard choice here |
| MCS-48 core | **none in the repo** | new |

## Emulator rules

1. **Port #FE read, Profi v5 (MM_PROFI).** Decoded on A0=0 (/CSKBD).
   - Bits 0-4 = KD0-KD4. Bit 5 = **KD5**, read from the keyboard device for the selected half-row, pull-up 1. Bit 6 = tape. Bit 7 = GX0 as now.
   - The matrix model becomes 8 half-rows × **6 bits**. Bit 5 of each half-row is a real key position.
   - H.
2. **Port #FE read, Profi v3.2 (MM_PROFI3).** Bits 0-4 = KD0-KD4 and bit 5 = 1.
   - Bit 7 = DATAKEY (XT data from the on-board pads; 1 when idle). The XT clock can raise /INT.
   - Only build this if software for it turns up; until then, bit 7 = 1. O.
3. **Configuration.** `[PROFI] Keyboard=Matrix|XT`. Default **XT on v5** (the CP/M experience the user expects) and **Matrix on v3**.
   - XT on v3 is allowed but loses EXT: DK5 lands on bit 7 there, so EXT keys do not register.
   - Also a create-JSON key (`"profi":{"keyboard":"xt"}`) on all 5 automation surfaces.
   - M (a design choice).
4. **Matrix device (native keyboard).** The existing `Keyboard` with no D5 keys. Host Shift → CS as on other machines. H.
5. **XT device: new `ProfiXtKbc : IPs2KeySink` in `core/src/emulator/io/keyboard/`.** It is a sibling of `Atm2Kbc`, attached with `pKeyboard->SetPs2Sink()` in `portdecoder_profi`, and it answers #FE bits 0-5 in place of the matrix.
   - **Recommended: low-level emulation** on a new isolated **MCS-48 core** (`core/src/emulator/cpu/mcs48/`, about 100 opcodes; the Python model in `materials/keyboard/sim/` is a reference), running `data/rom/profixt/profi-xt-v1.27.rom` (the reconstructed image). This reproduces exactly:
     - the WAIT length (≈50-110 µs per read while any key is held, as Z80 wait states, the `Atm2Kbc::ReadPort` pattern);
     - P27 gating: no wait and all 1s when idle;
     - single-half-row answers, with multi-row reads returning "no key";
     - Ctrl+Alt+Del → machine reset (latch bit 7 → /HRESET);
     - ScrollLock and #AAFE/#55FE mode switching;
     - NumLock on the keypad.
   - **Fallback: high-level emulation** with the key table from §2 and the rules above, if a re-dump disagrees or the core is postponed.
   - The XT input is new code: a **set-1** generator (`pckey::XtSet1Bytes`, make, break = |80h, E0 prefixes, E1 for Pause) and a 9-bit frame feeder (start bit 1 + 8 bits, LSB first) on /INT and T0, with typematic repeat as in `Ps2KeyboardStream`.
   - H for the behavior. M for the 5 reconstructed bytes.
6. **Routing.** With XT attached, host keys must go **only** to the XT device; X9 takes one keyboard. Otherwise Shift would press CS on the matrix and SS through the controller.
   - Unavoidable change: the sink declares that it replaces the matrix (for example `IPs2KeySink::ReplacesMatrix()`), so `Auto` resolves to `Ps2`, not `Both`, for Profi.
   - DebugKeyboardManager / TypeText then use a **Profi reverse map**: CS→LCtrl, SS→LShift, letters, digits, Enter and Space 1:1; EXT combos as PC keys (F1-F10, Home, …).
   - M (design).
7. **TTD.** Host and automation input already arrive as `PcKey` and replay into the sink. New: a `PeripheralId` (next free after 43) and a serializer for the MCU state (RAM, registers, P1/P2, latch, WAIT flip-flop, receive frame position, key-repeat state, mode). H.
8. **Tests to write.**
   - Every PC key → matrix table (§2), against the low-level emulation.
   - BIOS 2.0 boot: F1 → code 75h via 0x99DA bit 6.
   - `#BFFE` bit 5 = 0 only on that half-row.
   - A multi-row read returns 3Fh.
   - Wait states > 0 only while a key is held.
   - Ctrl+Alt+Del resets.
9. **Open items.**
   - (a) A clean re-dump of the v1.27 EPROM to confirm 0x02E-0x032.
   - (b) Whether any v3.2 software decodes the on-board XT path (bit 7 + clock on /INT).
   - (c) The native v5 mechanical keyboard's extra keys (EXT, MODE, GRAF): sheet 9 shows them as two-contact combinations on the 40 keys, not KD5. O.
