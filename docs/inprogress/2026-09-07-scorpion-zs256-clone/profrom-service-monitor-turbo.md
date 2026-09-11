# ProfROM v4.01 service monitor — turbo (7 MHz) detection, switching and the "Computer speed" item

Source: `data/rom/scorp_prof401.rom`, ROM page 2 (file `#8000..#BFFF`), mapped at Z80 `#0000-#3FFF`.
Disassembled with `z80dasm -a -l -g 0`. All addresses below are page-2 addresses unless a page
is named. Facts marked **[live]** come from the earlier live port/RAM trace, not from the disassembly.
Everything else was read from the ROM bytes.

## 1. Summary

- Monitor variable base `IY = #E014`; `(IY+$19) = #E02D` is the monitor config byte.
  Bit 7 = turbo hardware present, bit 6 = turbo enabled, bit 3 = output-pause request (see §6).
- Turbo hardware is a flip-flop toggled by **I/O reads**: `IN A,(#7FFD)` sets 7 MHz, `IN A,(#1FFD)`
  returns to 3.5 MHz (`#04CE-#04E1`). The data read is discarded; the strobe is issued twice.
- Speed is *measured*, not queried: `#2C1F` runs a fixed 3584-iteration count loop (~96 900 T) with
  interrupts enabled; the INT handler forces `A=0` so the loop exits early with Z at 3.5 MHz
  (loop > one 69 888 T frame) and completes with NZ at 7 MHz (loop < one 139 776 T frame), provided
  it starts within the first ~42 800 T after an INT.
- Two independent measurements exist: `#025E` (monitor first-entry init, no INT sync, sets bits 7+6
  when the loop completes) and `#2C30` (boot-path re-check, HALT-synced, refreshes bit 6 only if bit 7
  is already set).
- The "Computer speed" enable/disable actions are `#02D1` (SET 6) / `#02D7` (RES 6). Both first run
  `#02DD`, which refuses (pops the return address, sets `(#DD7F)=1`) when bit 7 is clear. They are
  reached only through the 14-entry action table at `#08B7` (codes `#87`/`#88`), dispatched by `#0225`.
- Emulator consequence: applying the `#7FFD` read-strobe only at the next frame boundary makes the
  `#025E` loop run at 3.5 MHz → INT hits → `#E02D` stays 0 → item refused, while the flip-flop is
  nevertheless set → the monitor then runs at 7 MHz. See §8 and
  `profrom-nmi-gaps-and-findings.md` §8.

## 2. Memory map of the relevant variables

| Address | Via | Meaning (as verified from use) |
|---|---|---|
| `#E014` | `IY` (`#0672: LD IY,#E014`, `#1048`) | monitor variable base |
| `#E02D` | `(IY+$19)` | config: b7 turbo HW present, b6 turbo enabled, b3 output-pause request |
| `#E026` | `(IY+$12)` | boot/entry flags (b0 → interactive loop at `#0AF2`, b3 shadow-#5B00 mode, b4, b7) |
| `#E029` | `(IY+$15)` | character-output flags (b0 bit-banged serial, b1 alt baud, b2, b3 stop bits, b7 invert) |
| `#E012/#E013` | `LD DE,(#E012)` | saved user `#7FFD` (E) and `#1FFD` (D) values |
| `#E039` | `#011D` | boot delay multiplier (× `CALL #0241`, 1024-iteration loop) |
| `#E057/#E059` | `#0A40/#0A47` | bit-bang delay counters (normal / alternate baud) |
| `#E00D` | `#038B`, RAM stub | RAM-init marker, compared with ROM byte `(#00FB)=#01` |
| `#E516` | `(#00FF)=#16, (#0100)=#E5` | IM 2 vector (I=0, bus `#FF`) → RAM handler at `#E516` |
| `#E510-#E52C` | ROM image at `#3CE4-#3D00` | RAM stub, see §3.6 |
| `#DD6B` | `#0167`, `#022A` | pointer into the byte stream executed by `#01F5/#0225` |
| `#DD7F` | `#02E7` | result/status byte of the last action (0 = ok, 1 = refused) |
| `#C064` | `#305E` | hook set by the ROM self-test on failure |

## 3. Annotated listings

### 3.1 `#04CE` — apply / force turbo (hardware strobe)

```
04CE  3A 2D E0     LD   A,(#E02D)      ; config
04D1  CB 77        BIT  6,A            ; turbo enabled?
04D3  28 04        JR   Z,#04D9
04D5  06 7F        LD   B,#7F          ; entry "turbo ON"
04D7  18 02        JR   #04DB
04D9  06 1F        LD   B,#1F          ; entry "turbo OFF"
04DB  0E FD        LD   C,#FD          ; BC = #7FFD or #1FFD
04DD  ED 78        IN   A,(C)          ; strobe 1 (value ignored)
04DF  ED 78        IN   A,(C)          ; strobe 2 (value ignored)
04E1  C9           RET
```

**[live]** the two `IN #7FFD` at `#04DD/#04DF` are the only turbo-on strobes seen during monitor entry.

Callers (all verified by cross-reference; there is one more than the seeded list):

| Site | Entry | Context |
|---|---|---|
| `#053F` | `#04CE` apply | `#04E2` exit-to-user-program: restores user registers/I/SP from the `#DD6B..` save area, applies turbo per config, restores `#7FFD` ROM bits from `(#E012)`, `B=#1F`. `#015C` calls it, then `#0164 JP #000B` → `OUT (C),A` with `A=0`, `BC=#1FFD` leaves the service ROM and continues at `#000D` of the user ROM. |
| `#1096` | `#04CE` apply | second exit path `#1048`: `IY=#E014`, keyboard check (row `#FE` b0 / row `#F7` b4), sanity-sums user ROM `#0500-#05FF` via `RST 28h` against `(#2BB2)=#D3` (mismatch → `JP #001E` → reset path `#003B`), then applies turbo, then `RST 30h #0A5A,page 4`. |
| `#0261` | `#04D5` on | monitor first-entry init `#025E` (§3.5) — precedes the speed measurement. |
| `#0676` | `#04D5` on | `#0672` cold start: `IY=#E014`, turbo on, `#1FFD=#12`, clears/verifies RAM pages `#17..#10` at `#C000`. Reached from `#004F→#007B` when the RAM signature at `#EAF5` does not match. |
| `#0AF2` | `#04D5` on | entry of the interactive monitor loop (from boot `#0133` when `(IY+$12)` b0 set and `(#C063)=0`, and from `#0AEB`). Unconditional 7 MHz for the UI; then `(#E01E)=0`, screen `#2CA8`, loop `#0B06`. |
| `#025E` | `#04D9` off | first instruction of the entry init (§3.5): guarantees a known 3.5 MHz state before the ON strobe. |
| `#09BC` | `#04D9` off | character-output driver `#09A9` when `(IY+$15)` b0 (bit-banged serial/RS-232 through `#1FFD` b3, start byte `#1A`, delays `#E057/#E059`). Software timing needs 3.5 MHz. The routine does **not** restore turbo (`#0A03 POP AF; RET`). |
| `#3152` | `#04D9` off (`JP Z`) | ROM self-test `#3135` (entered from `#072D`): sums `#0000-#00F5`, `#00F7-#0103`, `#0110-#3FFF` (skips the plane-strobe addresses `#0104-#010F`), compares with `(#00F6)`; on match turbo off and return, else `#305B` sets `(#C064)=#3052` and retries. |

### 3.2 `#2C1F` — speed test loop

```
2C1F  AF           XOR  A
2C20  67           LD   H,A            ; H = 0 (256 inner counts)
2C21  1E 0E        LD   E,#0E          ; 14 outer counts
2C23  3C           INC  A              ; A = 1 (non-zero)
2C24  B7           OR   A              ; A still non-zero?
2C25  28 07        JR   Z,#2C2E        ; no → an INT handler zeroed A → "slow"
2C27  25           DEC  H
2C28  20 FA        JR   NZ,#2C24
2C2A  1D           DEC  E
2C2B  20 F7        JR   NZ,#2C24
2C2D  1C           INC  E              ; E = 1 → NZ = "fast" (completed)
2C2E  F3           DI
2C2F  C9           RET                 ; Z: interrupted (slow) / NZ: completed (fast)
```

Exit flag: Z from `OR A` (A became 0) or NZ from `INC E`. `H` is 0 on NZ exit and holds the residual
inner count on Z exit; `L` is untouched.

### 3.3 IM 1 handler `#0038 → #0092`

```
0038  C3 92 00     JP   #0092
0092  3E 00        LD   A,#00          ; zero A → speed loop exits with Z
0094  10 FE        DJNZ #0094          ; burns B iterations (B=1 from #3B75, B=#7F after #04D5)
0096  B8           CP   B              ; B is now 0 → always equal
0097  20 01        JR   NZ,#009A
0099  14           INC  D              ; D++ : "an INT happened"
009A  FB           EI
009B  C9           RET
```

IM 2 alternative: `I=0` (set at `#0084`) and bus `#FF` → vector `(#00FF)` = `16 E5` → `#E516` in RAM,
which after patching is `XOR A; EI; RET` (§3.6) — same effect on `A`, no `INC D`.

### 3.4 `#2C30` — detection (boot path)

```
2C30  FD CB 19 7E  BIT  7,(IY+$19)     ; hardware present flag
2C34  C8           RET  Z              ; no hardware → nothing to do
2C35  CD 75 3B     CALL #3B75          ; sync to INT; returns A=D, Z if handler did not run
2C38  F5           PUSH AF
2C39  ED 56        IM   1
2C3B  FB           EI
2C3C  CD 1F 2C     CALL #2C1F          ; speed loop, starts ~130 T after the INT
2C3F  21 2D E0     LD   HL,#E02D
2C42  CB B6        RES  6,(HL)
2C44  28 02        JR   Z,#2C48        ; interrupted → leave bit 6 clear
2C46  CB F6        SET  6,(HL)         ; completed → enabled
2C48  F1           POP  AF
2C49  C0           RET  NZ             ; handler ran → keep IM 1
2C4A  ED 5E        IM   2              ; else restore IM 2
2C4C  C9           RET

3B75  AF           XOR  A
3B76  57           LD   D,A            ; D = 0
3B77  06 01        LD   B,#01          ; DJNZ in the handler runs once
3B79  FB           EI
3B7A  76           HALT                ; wait for the INT
3B7B  F3           DI
3B7C  7A           LD   A,D
3B7D  B7           OR   A              ; NZ if #0092 ran (D=1)
3B7E  C9           RET
```

Caller: boot init `#0126`, after `#011D`: `LD A,(#E039); LD B,A; loop: CALL #0241; DJNZ`
(`#0241`: `PUSH BC; LD BC,#0400; DEC BC/LD A,C/OR B/JR NZ; POP BC; RET`). Then `#0129` tests
`(IY+$12)` b0 and `(#C063)` to enter the interactive loop at `#0AF2`, otherwise continues with the
`#0136` boot sequence and exits via `#015C/#0164`.

### 3.5 `#025E-#0296` — monitor first-entry init (hardware detection)

```
025E  CD D9 04     CALL #04D9          ; turbo OFF (IN #1FFD ×2)
0261  CD D5 04     CALL #04D5          ; turbo ON  (IN #7FFD ×2)
0264  3E AF        LD   A,#AF
0266  32 16 E5     LD   (#E516),A      ; IM2 handler in RAM := XOR A (was #00 in the ROM image)
0269  CD 1F 2C     CALL #2C1F          ; speed loop — NO interrupt sync here
026C  28 08        JR   Z,#0276        ; interrupted → hardware absent, #E02D unchanged
026E  3A 2D E0     LD   A,(#E02D)
0271  F6 C0        OR   #C0
0273  32 2D E0     LD   (#E02D),A      ; present + enabled
0276  22 2B E5     LD   (#E52B),HL     ; overwrite JP operand of the RAM stub (see §3.6)
0279  3E CD        LD   A,#CD
027B  32 1C E5     LD   (#E51C),A      ; patch RAM stub byte
027E  0E FF        LD   C,#FF
0280  AF           XOR  A
0281  5F           LD   E,A
0282  57           LD   D,A            ; DE = 0 → 65536 iterations
0283  3C           INC  A
0284  47           LD   B,A            ; B = 1
0285  FB           EI
0286  76           HALT                ; sync to INT (handler #0092 runs with B=1)
0287  3C           INC  A
0288  47           LD   B,A            ; handler left A=0, so A=1 → B = 1
0289  CD 83 E4     CALL #E483          ; RAM routine (not in page 2)
028C  ED 70        IN   F,(C)          ; read port #01FF (B=1, C=#FF): flags only
028E  CD 83 E4     CALL #E483
0291  1B           DEC  DE
0292  B7           OR   A
0293  20 F4        JR   NZ,#0289       ; ← loops while A≠0; DE is decremented but never tested
0295  F3           DI
0296  F7 ...       RST  30h ...        ; cross-page call, continues the init
```

Notes verified from the bytes:
- `#0283-#0293` is *not* a port-measurement loop in the sense of counting: the exit condition is
  `OR A` on the accumulator, i.e. it spins (calling `#E483` and reading port `#02FF` via `IN F,(C)`)
  until an interrupt handler zeroes `A`. `DEC DE` only affects DE, the flags are overwritten by `OR A`.
  So it is an INT-synchronising wait (one full frame, since A is set to 1 right after the `HALT`),
  with `#E483` executed on each pass and port `#01FF` read via `IN F,(C)`. What `#E483` does is
  outside page 2 (RAM). The `IN F,(C)` flags are unused (no conditional follows before `OR A`).
- The loop at `#0269` is started at an arbitrary phase relative to INT (no HALT before it, only two
  ~40 T strobe calls). See §5 for the consequence.

### 3.6 RAM stub `#E510-#E52C` and its ROM image `#3CE4-#3D00`

ROM page 2 contains the image byte-for-byte (`#3CE4`): `3E 10 ED 79 D9 C9 | 00 33 33 | FB 3C 32 0D E0
3E C9 32 18 E5 3E FB 32 17 E5 ED 56 C3 5E 02`. The copier is not in page 2 (no reference to `#3CE4`
found in pages 0-3, 5, 6, 10, 14). Decoded at its RAM location:

```
E510  3E 10        LD   A,#10
E512  ED 79        OUT  (C),A          ; paging helper
E514  D9           EXX
E515  C9           RET
E516  00 → AF      (#0266 writes XOR A)   ┐ IM 2 handler after patching:
E517  33 → FB      (E525 writes EI)       │ XOR A ; EI ; RET
E518  33 → C9      (E520 writes RET)      ┘
E519  FB           EI                  ; one-shot init entry
E51A  3C           INC  A
E51B  32 0D E0     LD   (#E00D),A      ; RAM-init marker (compared with (#00FB)=#01 at #0388)
E51E  3E C9        LD   A,#C9
E520  32 18 E5     LD   (#E518),A
E523  3E FB        LD   A,#FB
E525  32 17 E5     LD   (#E517),A
E528  ED 56        IM   1
E52A  C3 5E 02     JP   #025E          ; → monitor first-entry init
```

So `#E516` is the IM 2 interrupt entry (vector `(#00FF)=#E516`), and `#025E` is reached from this
stub. `#027B` overwrites `#E51C` (the low byte of the `LD (#E00D),A` operand) with `#CD`, and `#0276`
overwrites the `JP` operand at `#E52B` with `HL` as left by the speed loop (`H=0` if it completed).
Neither location is read again in page 2; treat `#E51C` and `#E52B/#E52C` as reused RAM variables.

**[live]** RAM at `#E510` read as `3E 10 ED 79 D9 C9 AF FB C9 FB 3C 32 CD 12 02 C9 32 18 E5 3E FB 32
17 E5 ED 56 C3 00 B0` — consistent with the image plus the patches above (`E516=AF`, `E517/E518
=FB/C9`, `E51C=CD`, `E52B/E52C=00 B0` = `HL=#B000`: `L=0`, `H=#B0` residual → the loop was interrupted).
The bytes `12 02` at `#E51D/#E51E` are not written by any page-2 code; unexplained.

### 3.7 `#02D1 / #02D7 / #02DD` — enable / disable actions

```
02D1  CD DD 02     CALL #02DD          ; action "enable turbo"
02D4  CB F6        SET  6,(HL)
02D6  C9           RET
02D7  CD DD 02     CALL #02DD          ; action "disable turbo"
02DA  CB B6        RES  6,(HL)
02DC  C9           RET
02DD  AF           XOR  A              ; result 0 = ok
02DE  21 2D E0     LD   HL,#E02D
02E1  CB 7E        BIT  7,(HL)         ; hardware present?
02E3  20 02        JR   NZ,#02E7
02E5  3C           INC  A              ; result 1 = refused
02E6  C1           POP  BC             ; drop caller's return → SET/RES skipped
02E7  32 7F DD     LD   (#DD7F),A      ; status byte
02EA  C9           RET
```

Neither `#02D1` nor `#02D7` is called or jumped to directly; both appear only as words in the table
at `#08B7` (§6). Note: the actions only change bit 6 — the hardware strobe is applied later by a
`#04CE` caller (exit paths `#053F`, `#1096`).

## 4. Call graph

```
boot/NMI entry #00B6 ─┬─ #011D delay ((#E039) × #0241) ─ #0126 CALL #2C30 ─ #3B75 (HALT sync)
                      │                                              └ #2C1F speed loop
                      ├─ #0133 → #0AF2 CALL #04D5 (turbo ON) → interactive loop #0B06
                      └─ #015C CALL #04E2 ─ #053F CALL #04CE (apply) → #0164 → #000B exit
cold start #004F → #007B CALL #0672 ─ #0676 CALL #04D5 (turbo ON) → RAM clear
RAM stub #E519 ... #E52A JP #025E ─ #025E CALL #04D9 (OFF) ─ #0261 CALL #04D5 (ON)
                                   └ #0269 CALL #2C1F → OR #C0 into #E02D → #0283 INT wait → RST 30h
stream executor #01F5/#0225 ─ table #08B7[code-#80] ─┬─ #87 → #02D1 ─ #02DD ─ SET 6
                                                      └─ #88 → #02D7 ─ #02DD ─ RES 6
exit path #1048 ─ #1096 CALL #04CE (apply)
char output #09A9 ─ #09BC CALL #04D9 (OFF, not restored)
ROM self-test #3135 ─ #3152 JP Z,#04D9 (OFF)
IM 1: #0038 → #0092 (A=0, D++)      IM 2: (#00FF)=#E516 → XOR A; EI; RET
```

## 5. Timing analysis

Loop `#2C1F` (T-states from the opcode tables; `JR` taken 12 / not taken 7):

| Part | T |
|---|---|
| prologue `XOR A; LD H,A; LD E,#0E; INC A` | 19 |
| inner iteration (`OR A; JR Z nt; DEC H; JR NZ t`) | 27 |
| last inner iteration of an outer pass (`JR NZ` not taken) | 22 |
| one outer pass (255×27 + 22 + `DEC E` 4 + `JR NZ` 12) | 6 923 |
| last outer pass (`JR NZ` not taken) | 6 918 |
| loop total, 14 passes (13×6923 + 6918) | 96 917 |
| epilogue `INC E; DI; RET` | 18 |
| **CALL to RET** (17 + 19 + 96 917 + 18) | **96 971** |

Iterations: 14 × 256 = 3 584.

- **3.5 MHz**: frame = 69 888 T. 96 917 > 69 888, so wherever the loop starts, an INT is accepted
  before it finishes (EI is in force: `#2C3B` / `#E519`). The handler (`#0092`, 44 T with B=1,
  ≈1 690 T with B=#7F) zeroes A, the next `OR A` gives Z → "slow". Result is phase-independent.
- **7 MHz**: frame = 139 776 T. The loop completes only if it starts no later than
  139 776 − 96 917 ≈ **42 860 T** (≈ 6.1 ms) after an INT.
  - `#2C30`: HALT-synced start. Overhead after the INT edge ≈ INT ack 19 + handler 44 + `DI/LD/OR/RET`
    22 + `PUSH AF; IM 1; EI; CALL` 40 ≈ 125 T → completes with ≈ 42 700 T (30 % of a frame) to spare → NZ = "fast".
  - `#025E`: not synced. Only the two strobe calls (~90 T) precede the loop; the outcome depends on
    where in the frame the stub entered `#025E`. If entry itself follows an INT closely (as after a
    HALT), it behaves like `#2C30`; otherwise the detection can fail on real 7 MHz hardware with
    probability ≈ 1 − 42 860/139 776 ≈ 69 %. This is a property of the ROM, not of the emulator.

## 6. Menu / action logic

### 6.1 What is verified
- The only writers of bit 6 of `#E02D` in page 2 are `#02D4` (SET), `#02DA` (RES), `#2C42/#2C46`
  (detection) and `#0271` (`OR #C0`). The only readers are `#04D1`, `#3426`, `#2C30`, `#02E1`, `#37BD`.
- `#02D1/#02D7` are entries 7 and 8 of the 14-word table at `#08B7`:
  `#0911 #02F4 #0339 #2A5B #035F #0368 #08D3 #02D1 #02D7 #0321 #02EB #02AA #02CC #02AE`.
- Dispatcher `#01F5-#0240`: `RST 28h` (`#0028: OR A; BIT 7,H; → #058E`, the monitor's paging-aware
  peek of the user's address space) reads one byte at `HL=(#DD6B)`; `#FF` ends the stream; `SLA A`
  moves bit 7 into CF, `JR C,#0225`; there `CP #1B / JR NC` rejects codes above `#8D`; the byte after
  the code is stored back to `(#DD6B)`, `#0216` is pushed as continuation, `SET 5,(IY+$0B)`, and
  `JP (HL)` through `#08B7 + 2×(code−#80)`. Hence code `#87` = enable turbo, `#88` = disable turbo.
  `#02DD`'s `POP BC` discards the `CALL #02DD` return so the action returns straight to `#0216` with
  `(#DD7F)=1`; success leaves `(#DD7F)=0`.
- The interactive UI lives in ROM page 5 (RST 30h trailer byte = page number; page 5 carries
  signature `01 05` at `#0100`, and `#1C73`/`#0EB5` decode as code only there). Page 5's only use of
  `#E02D` is the predicate `#0919: LD A,(#E02D); RLCA; CCF; RET` → CF = 1 when bit 7 is **clear**.
  It sits in a block of similar flag predicates (`#08D0-#0990`, each returning CF). This is the
  natural "grey the item" test, but the descriptor that binds it to the `V` item was not located.
- Menu text is token-encoded: page 5 `#13C5-#14EA` is a dictionary of 65 words with the last
  character ORed with `#80` (`computer` = token 32, `speed` = token 7, `ON` = 26, `OFF` = 25).
  Plain-ASCII search therefore finds nothing; no byte pair `32,07` (with or without an offset) that
  looks like a label was found in page 5, so the label is composed by code, not a literal.
- Page 6 (`#0DB8-#0DDC`) serialises the config: on load (`#0DBC`) it copies bit 6 of the stored byte
  into `#E02D` only if bit 7 is set, then IM mode and border; on save (`#0DD9`) it stores `#E02D`.
- Page 2 `#3426` (`BIT 6,(IY+$19)` after `CALL #3B75`, `LD A,'2'; SUB D; RST 10h`) prints an extra
  marker on a status line when turbo is enabled.

### 6.2 Bit 3 of `#E02D`
`#37B9-#37D6`: `BIT 3,(HL); RES 3,(HL)`; if it was set → `#381C` (delay `#3807`, read a key via
`#3023`, and `SET 3,(HL)` again if it is Space or `S`); if clear → `#3813` (if Space is held: wait for
release, then the same). Then Caps Shift (row `#FE` b0) and `1` (row `#F7` b0) must both be down,
otherwise return; if they are, `RES 3,(IY+$19)` and wait for key release. `#37B5: SET 3,(HL)` → `LD
A,#81; JP #0AFB`. Bit 3 is therefore a **pause-output request** (Space/`S` = pause the listing,
Caps+1 = cancel), unrelated to turbo.

### 6.3 Not verified
The interactive keystroke `V` → `#87/#88` binding and the greying of the item are in page 5's
token-driven menu code and were not traced; only the predicate `#0919` (CF = hardware absent) and the
action codes are established.

## 7. Contradictions / corrections to the seeded findings

1. `#04CE` has one more caller than listed: `#3152 JP Z,#04D9` (ROM self-test success → turbo off).
2. The `#0283-#0293` loop does not "measure" the port: it spins until an INT handler zeroes `A`
   (`OR A` is the only exit test; `DEC DE` is dead for control flow). It is an INT wait that runs the
   RAM routine `#E483` twice per pass; the `IN F,(C)` result is unused.
3. `#E516` is the **IM 2** vector (`(#00FF)=#16,(#0100)=#E5` with `I=0`), not an IM1 hook; the
   patch makes it `XOR A; EI; RET`. `#025E` is entered from the RAM stub `#E52A JP #025E`, whose
   image is at page 2 `#3CE4-#3D00`.
4. `#02D1/#02D7` are dispatched by the byte-stream executor `#0225` via table `#08B7` (codes
   `#87/#88`), not by a key handler in page 2; the RST 30h trailer byte is a ROM page number (UI is
   in page 5), not a plane code.
5. `#025E` has no INT synchronisation before its speed loop, so even on real hardware the result is
   phase-dependent (§5); `#2C30` is the synchronised one.

## 8. Emulator implications (no code changed)

The emulator applies the `IN #7FFD` strobe at the next frame boundary. In the `#025E` sequence the
strobes at `#04DD/#04DF` are immediately followed by `#0269 CALL #2C1F`, which therefore runs at
3.5 MHz: after ≤ 69 888 T the INT arrives, `A=0`, the loop exits Z, `#026C` skips `OR #C0`, and
`#E02D` remains 0 (bits 7 and 6 clear). Consequences:

- `#2C30` returns at `#2C34` (bit 7 clear) — the boot re-check never runs.
- `#02DD` refuses `#87/#88` (`(#DD7F)=1`); page 5's predicate `#0919` reports "no hardware".
- `#04CE` (exit paths) always takes the `#04D9` branch, but the flip-flop set by `#0261` is never
  cleared before the interactive loop, and `#0AF2` strobes ON again, so the monitor runs at 7 MHz
  with the item disabled.

Applying the read-strobe immediately (reference: `profrom-nmi-gaps-and-findings.md` §8.4) makes the
`#0269` loop run at 7 MHz. Because `#025E` is not INT-synced, detection then also depends on the
frame phase at which the RAM stub reaches `#025E` (≈ 42 860 T window per frame, §5); a
deterministic emulator will give a fixed result for a fixed boot timeline.

## 9. Turbo-detect test flake — post-mortem (2026-09-10)

`ScorpionTurboDetect_Test.ProfRomMonitorDetectsSevenMhz` intermittently failed with `#E02D=#80`
across all press phases. Tstate-exact tracing of a real entry resolved the chain the test actually
exercises:

1. NMI → prologue `#03D0-#0457`: register save, then a bank scan (`OUT (#7FFD)` / `OUT (#1FFD)`
   paging every bank looking for `#55/#AA` at `#C001/#C002`). **`#E02D` "changes" observed while
   the PC sits in `#03F2-#0457` are bank-window artifacts of the sampled address, not stores.**
2. `#00B6` entry, branching on the `#DD86` boot flags, then `#011D`: a `#E039 × CALL #0241`
   delay of ~240 000 T (~1.75 frames at 7 MHz), PC idling at `#0245-#0248`.
3. `#0126 CALL #2C30` at ≈ t+244 000 — the HALT-synced re-check (`RES 6` at `#2C42`, `SET 6` at
   `#2C46` iff the loop completed), then `#0AF2` (strobe ON, interactive loop).

Consequences:

- **Entry from `#E02D=C0` never strokes the latch** — the monitor-entry grid only exercises the
  HALT-synced `#2C30`, which is deterministic *per clock speed*: at 7 MHz it always completes, at
  3.5 MHz it is always interrupted. The all-`#80` failure signature therefore means the turbo
  flip-flop was OFF at press time (causally reproduced by forcing it off and pressing). The phase
  grid cannot mask or rescue this either way.
- The `ApplyHardwareTurboNow`-sensitive path (stub/`#025E`: `#04D9` OFF strobe, `#04D5` ON strobe,
  un-synced `#2C1F`, `OR #C0`) runs **during the boot itself**, at frames 41-45 (turbo OFF flip f41,
  ON flip f44, `#E02D 00→C0` write f45 — identical on every boot). Test coverage of the mid-frame
  clock switch now lives in `ProfRomBootDetectsSevenMhz`; the monitor-entry grid stays as the
  `#2C30`/NMI-chain guard.

Non-determinism sources found and fixed (boot timeline was *not* fixed, §8's precondition):

- The SMUC RTC stub served live host time, so the 128-menu clock shifted boot-timeline events with
  the wall clock. `SMUCNvram::SetFixedTime()` now lets tests freeze it
  (`1767268830` = 2026-01-01 12:00:30 UTC).
- Power-on RAM was host-heap garbage (`new uint8_t[PAGE_SIZE * MAX_PAGES]`); the allocation sites
  now zero-init, making every cold boot start from the same memory image.
- Residual, cosmetic only: one staging loop iteration at `#00D1/#00E5` still varies between boots
  (persists with frozen RTC and zeroed RAM); it changes garbage staging bytes, never test outcomes.

Test-writing caveat: `Emulator::RunNFrames(n)` derives its whole t-state budget from the clock
multiplier at entry. The boot itself flips turbo ON at ~frame 11, so a single 60-frame call spans
only ~35 video frames and stops before the f45 write. Step per frame (`RunNFrames(1)` in a loop)
when a window must be expressed in video frames across a speed change.
