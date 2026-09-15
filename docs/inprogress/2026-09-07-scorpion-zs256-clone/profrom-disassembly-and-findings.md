# ProfROM Static Disassembly, System Architecture & Root Cause Findings

This document records the comprehensive static and dynamic reverse-engineering of the **Scorpion ProfROM v4.01** image (`data/rom/scorp_prof401.rom`), detailing its memory mapping, cross-plane calling convention, NMI entry chain, service monitor menu execution, keyboard scanning & interrupt handling, and the exact root causes of emulator hangs/loops.

---

## 1. ProfROM Architecture & Memory Layout Overview

The ProfROM firmware is organized into four 64 KB quadrants (or "planes"), each containing four 16 KB pages:

| Quadrant | Plane | Page 0 (`#00`) | Page 1 (`#01`) | Page 2 (`#02`) | Page 3 (`#03`) | Role |
|:---:|:---:|:---:|:---:|:---:|:---:|:---|
| **Q0** | Plane 0 | Page 0 | Page 1 | Page 2 | Page 3 | Base System (128 BASIC, 48 BASIC, Shadow Service Monitor, TR-DOS 5.04T) |
| **Q1** | Plane 1 | Page 4 | Page 5 | Page 6 | Page 7 | Extension Plane 1 (Service Menu, Test Programs, TR-DOS) |
| **Q2** | Plane 2 | Page 8 | Page 9 | Page 10 | Page 11 | Extension Plane 2 (Disk Utilities / Tools) |
| **Q3** | Plane 3 | Page 12 | Page 13 | Page 14 | Page 15 | Extension Plane 3 (Debugger / Assembler / Tools) |

When the Shadow Service Monitor is active (`p1FFD` bit 1 = 1, `p1FFD` bit 0 = 0), the ROM window `#0000..#3FFF` accesses the Service Page of the currently active plane:
- In Plane 0: Page 2 is mapped.
- In Plane 1: Page 6 is mapped.

Simultaneously, `p1FFD` bit 4 = 1 maps **RAM Bank 8** at `#C000..#FFFF`. Bank 8 is the dedicated workspace and stack RAM for the ProfROM monitor (`#DD00..#FFFF`).

---

## 2. Deep Disassembly Analysis

### 2.1 The NMI Entry Chain & The Premature Trigger Release Bug

When the user presses the magic button (MNI), the hardware asserts `/NMI` and sets the `DD50.1` DOS flip-flop (`scorpionDosTrigger`).

1. **TR-DOS Entry (`#0066`)**:
   The hardware forces TR-DOS Page 3 at `#0000..#3FFF`. The Z80 CPU vectors to `#0066`:
   ```z80
   ; TR-DOS (Plane 0, Page 3)
   0066: jp   2A56h           ; C3 56 2A
   ...
   2A56: jp   0807h           ; C3 07 08
   ```

2. **Context Preservation and Bank 8 Setup (`#0807`)**:
   At `#0807`, the handler saves CPU registers and prepares to page in the Service Monitor:
   ```z80
   0807: push af
   0808: ld   a, r
   080A: push af
   080B: ld   a, 04h
   080D: push af
   080E: inc  sp
   080F: push bc
   0810: ld   bc, 1FFDh
   0813: push hl
   0814: ld   hl, (0C001h)     ; <<< DATA READ FROM RAM (>= 0x4000)!
   0817: ex   (sp), hl
   0818: ld   a, 55h
   081A: ld   (0C001h), a      ; write test pattern to Bank 8
   081D: cpl
   081E: ld   (0C002h), a
   0821: ld   a, 12h           ; bit 1 = Shadow Monitor, bit 4 = RAM Bank 8
   0823: jp   0033h
   ```

3. **The `#0033` Page Swap Trick**:
   At `#0033`, the code writes to port `#1FFD`:
   ```z80
   ; TR-DOS Page 3 at #0033:
   0033: out  (c), a          ; ED 79 — OUT (#1FFD), 0x12: pages Service ROM Page 2 at #0000!

   ; Service Monitor Page 2 at #0033:
   0033: nop                  ; 00
   0034: nop                  ; 00
   0035: jp   00B6h           ; C3 B6 00 — fetched immediately from Page 2!
   ```

4. **Service Monitor Initialization (`#00B6`)**:
   ```z80
   00B6: di
   00B7: ld   a, 12h
   00B9: out  (c), a          ; latch #1FFD = 0x12
   00BB: ld   b, 7Fh
   00BD: ld   a, 10h
   00BF: out  (c), a          ; latch #7FFD = 0x10
   00C1: ld   (0DD6Dh), sp    ; save interrupted program's SP
   00C5: ld   (0DDF7h), sp
   00C9: ld   sp, 0E38Dh      ; set monitor stack in RAM Bank 8
   ```

> [!CAUTION]
> **Defect 1 Identified**:
> In `core/src/emulator/memory/memory.cpp`, `MemoryReadFast` previously checked:
> ```cpp
> if (_scorpionDosTriggerActive && addr >= 0x4000) [[unlikely]] {
>     _context->emulatorState.scorpionDosTrigger = 0;
>     UpdateZ80Banks();
> }
> ```
> At `#0814`, instruction `LD HL, (0C001h)` executes a **data read** from `0xC001 >= 0x4000`. Unreal-NG immediately cleared `scorpionDosTrigger = 0` at `#0814`, unpaging TR-DOS before the CPU could reach `#0033` (`OUT (#1FFD), 0x12`). The CPU subsequently executed garbage from BASIC ROM and crashed.
> **Hardware Truth**: The DD50.1 flip-flop is only clocked and reset on **instruction fetches** (`/M1` + `/MREQ` + `A15|A14`), as verified in reference emulators:
> - **Xpeccy** (`hardware.c:201`): `if (m1 && comp->flgDOS && (pg->type == MEM_RAM)) { comp->flgDOS = 0; }`
> - **ZXMAK2** (`MemoryScorpion256.cs:72`): `bmgr.Events.SubscribeRdMemM1(0xC000, 0x4000, BusReadMemRamM1);`

---

### 2.2 The Cross-Plane Calling Mechanism (`RST 30h`) & Quadrant Switching

The ProfROM monitor relies on inter-plane subroutine calls across ROM pages using `RST 30h` as a system call gate.

1. **System Call Invocation (`RST 30h`)**:
   Throughout Page 2 and Page 6, subroutines invoke inter-plane functions using `RST 30h` followed by inline metadata bytes:
   ```z80
   rst  30h                   ; F7 — calls #0030
   defb arg0, arg1, ...       ; inline target routine address and plane specifier
   ```

2. **Vector `#0030` Dispatcher**:
   At `#0030` in both Page 2 and Page 6:
   ```z80
   0030: jp   0E3D3h          ; C3 D3 E3 — jumps to dispatcher in RAM Bank 8
   ```

3. **Dispatcher in RAM Bank 8 (`#E3D3`)**:
   The dispatcher in RAM Bank 8 saves the calling plane, switches to the target plane, calls the routine, and restores the original plane:
   ```z80
   E3D3: push hl
   E3D4: ld   hl, (00101h)    ; <<< READ SIGNATURE FROM #0101 TO IDENTIFY CURRENT PLANE!
   E3D7: ex   (sp), hl        ; save current plane signature on stack
   E3D8: push hl
   E3D9: ld   hl, 0006h
   E3DC: push af
   E3DD: add  hl, sp          ; index stack to get return address (points to inline args)
   E3DE: push de
   E3DF: push bc
   E3E0: ld   e, (hl)
   E3E1: inc  hl
   E3E2: ld   d, (hl)         ; DE = address following RST 30h
   E3E3: ex   de, hl
   E3E4: ld   c, (hl)         ; read inline target function address
   E3E5: inc  hl
   E3E6: ld   b, (hl)
   E3E7: inc  hl
   E3E8: ld   a, (hl)         ; target plane code
   E3E9: call 0E478h
   ...
   E3F5: call 0E4AAh          ; switch to target quadrant!
   ...
   ; Return path:
   E408: ...
   E40D: ld   a, l            ; restore caller's plane signature
   E40E: call 0E4AAh          ; switch back to caller's quadrant!
   E41A: ret
   ```

4. **Plane Identification Signatures (`#0100..#0101`)**:
   Each plane's Service ROM has a unique signature at `#0100..#0101`:
   - **Plane 0 (Page 2)**: `E5 02` (byte at `#0101` = `0x02`, bits 3:2 = `00`)
   - **Plane 1 (Page 6)**: `01 06` (byte at `#0101` = `0x06`, bits 3:2 = `01`)
   - **Plane 2 (Page 10)**: `01 0A` (byte at `#0101` = `0x0A`, bits 3:2 = `10`)
   - **Plane 3 (Page 14)**: `01 0E` (byte at `#0101` = `0x0E`, bits 3:2 = `11`)

5. **Quadrant Switching Subroutine (`#E4AA`)**:
   ```z80
   E4AA: ld   c, a
   E4AB: ld   hl, 00110h      ; pointer to strobe lookup table
   E4AE: rrca
   E4AF: rrca
   E4B0: and  03h             ; extracts plane index (0..3) from signature bits 3:2
   E4B2: add  a, l
   E4B3: ld   l, a            ; HL = #0110 + index
   E4B4: ld   l, (hl)         ; READ 1: fetches strobe offset (0x00, 0x0C, 0x08, 0x04) into L
   E4B5: ld   l, (hl)         ; READ 2: READS FROM (0x0100 + L) — HARDWARE STROBE!
   E4B6: ld   a, c
   E4B7: ret
   ```

   **The Strobe Table at `#0110`**:
   - In Page 2: `00 0C 08 04`
   - In Page 6: `0C 00 08 04`

   The first `LD L, (HL)` reads the table entry. The second `LD L, (HL)` performs a dummy read from:
   - `#0100`: Strobe Plane 0
   - `#0104`: Strobe Plane 1
   - `#0108`: Strobe Plane 2
   - `#010C`: Strobe Plane 3

> [!CAUTION]
> **Defect 2 Identified**:
> In `core/src/emulator/memory/memory.cpp`, the strobe check previously used:
> ```cpp
> if (_scorpProfromActive && (addr & 0xFFF0) == 0x0100) [[unlikely]]
> ```
> 1. `(addr & 0xFFF0) == 0x0100` matched addresses `#0101` and `#0102`!
> 2. Whenever `RST 30h` executed `LD HL, (00101h)` at `#E3D4`, reading address `#0101` accidentally clocked the GAL DD41 emulator logic and slammed the quadrant to 0 right in the middle of execution!
> 3. It also fired on instruction fetches (`isExecution == true`).
>
> **Hardware Truth**:
> The GAL only decodes `A3:A2` with `A0:A1` low on CPU **data reads** (`!isExecution`):
> - Address mask: `(addr & 0xFFF3) == 0x0100` (strictly `#0100`, `#0104`, `#0108`, `#010C`).
> - Validated in **Xpeccy** (`scorpion.c:51`): `if ((comp->p1FFD & 2) && ((adr & 0xfff3) == 0x0100) && !m1)`
> - Validated in **UnrealSpeccy** (`z80_main.inl:164`): `membits[0x100] | membits[0x104] | membits[0x108] | membits[0x10C]`.

---

### 2.3 Service Menu Loop, Interrupts & Keyboard Scanning

When the Service Monitor menu is displayed, execution runs in **Quadrant 1, Page 6**.

1. **Idle Polling Loop (`#074D..#075F` in Page 6)**:
   ```z80
   074D: ld   hl, 0E02Eh
   0750: bit  0, (hl)
   0752: jr   z, 075Bh
   0754: di                   ; disable INT during cross-plane operations
   0755: res  0, (hl)
   0757: rst  30h             ; execute background task
   ...
   075B: ei                   ; ENABLE INTERRUPTS FOR KEYBOARD SCANNING!
   075C: call 0773h           ; check if keyboard ring buffer is empty
   075F: jr   z, 074Dh        ; loop if empty
   0761: di
   0762: ex   de, hl
   0763: ld   a, (de)         ; get key code from buffer
   ...
   ```

2. **Ring Buffer Inspection (`#0773` in Page 6)**:
   ```z80
   0773: ld   de, (0E116h)    ; DE = buffer head pointer
   0777: ld   hl, (0E118h)    ; HL = buffer tail pointer
   077A: or   a
   077B: push hl
   077C: sbc  hl, de          ; Z set if head == tail (empty)
   077E: pop  hl
   077F: ret
   ```

3. **50Hz Frame Interrupt (`IM 1` Vector `#0038`)**:
   Every 50 Hz frame, maskable interrupt `INT` occurs. In `IM 1`, the CPU jumps to `#0038`:
   - In **Page 6** (Active Monitor Menu):
     ```z80
     0038: jp   0114h
     ...
     0114: push af
     0115: push hl
     0116: push de
     0117: push bc
     0118: ld   ix, (0E3B7h)   ; screen/window context structure
     011C: call 0792h          ; SCAN KEYBOARD!
     011F: pop  bc
     0120: pop  de
     0121: pop  hl
     0122: pop  af
     0123: ei
     0124: reti
     ```
   - In **Page 2** (Quadrant 0):
     ```z80
     0038: jp   0092h          ; completely different routine, not keyboard scan!
     ```

4. **Keyboard Scanner (`#0792` -> `#07B2` -> `#0845` in Page 6)**:
   The keyboard scanning routine at `#0845` reads all 8 half-rows via port `#FE`:
   ```z80
   0845: ld   bc, 0FEFEh      ; port #FE, initial row mask in B = 11111110b
   0848: ld   hl, 0DFE9h      ; temporary key buffer
   084B: ld   de, 05FFh       ; D = 5 max keys, E = key index counter
   084E: in   a, (c)          ; READ KEYBOARD HALF-ROW
   0850: push bc
   0851: ld   b, 05h          ; 5 keys per half-row
   0853: inc  e
   0854: rrca                 ; test key bit
   0855: jr   c, 085Fh        ; bit = 1: not pressed
   0857: dec  d               ; bit = 0: pressed!
   0858: inc  sp
   0859: inc  sp
   085A: ret  z
   085B: dec  sp
   085C: dec  sp
   085D: inc  hl
   085E: ld   (hl), e         ; store pressed key index
   085F: djnz 0853h
   0861: pop  bc
   0862: defb 0CBh, 030h      ; SLI B (undocumented SLL B): shift row mask left, insert 1 into bit 0!
   0864: jr   c, 084Eh        ; loop through all 8 rows until carry clear
   0866: ld   a, d
   0867: sub  05h             ; Z set if no keys pressed
   0869: ret
   ```

5. **Debounce & Autorepeat Filter (`#07CD..#07E9` in Page 6)**:
   ```z80
   07D5: ld   e, a
   07D6: ld   a, (0E007h)     ; last detected key
   07D9: cp   e
   07DA: jr   nz, 07BFh       ; key changed: reset filter
   07DC: bit  1, (hl)
   07DE: jr   nz, 0834h       ; autorepeat path
   07E0: ld   de, (0E051h)    ; debounce countdown counter
   07E4: dec  d               ; decrement hold counter
   07E5: ld   (0E051h), de
   07E9: ret  nz              ; MUST HOLD UNTIL D == 0!
   ...
   ; When counter reaches 0:
   07A0: ld   de, (0E116h)    ; get ring buffer head
   07A4: ld   (de), a         ; ENQUEUE KEY CODE!
   07A5: inc  de
   07AD: ld   (0E116h), de    ; advance head
   ```

> [!IMPORTANT]
> **Defect 3 ("No Reaction to Keyboard") Explained**:
> 1. If Defect 2 causes the active plane to be corrupted to Plane 0 (Page 2), the 50 Hz interrupt vectors to `#0038 -> #0092` instead of `#0114`. The keyboard is never scanned!
> 2. The ProfROM keyboard scanner incorporates a sustained-hold debounce filter in `(0E051h)` (`dec d; ret nz`). When testing key presses via WebAPI `POST /api/v1/emulator/{id}/keyboard/tap`, the default `frames` parameter is 2. A 2-frame pulse is filtered out as contact bounce by `dec d` and never enqueued into the ring buffer! Holding the key for 10-30 frames allows the debounce counter to reach 0 and successfully registers the keystroke.

---

## 3. Summary of Root Causes & Proposed Code Fixes

| Subsystem | Issue in Current Working Tree | Root Cause | Proposed Exact Fix |
|---|---|---|---|
| **Magic NMI Button** | TR-DOS `#0066` crashes at `#0814` before `#0033` page switch | `scorpionDosTrigger` released on **data reads** `addr >= 0x4000` (`(0xC001)`) | Gating trigger release strictly on **instruction fetch** (`isExecution == true && addr >= 0x4000`), matching DD50.1 `/M1` flip-flop hardware |
| **ProfROM Quadrants** | Inter-plane `RST 30h` corrupts active plane and locks CPU | Strobe gate `(addr & 0xFFF0) == 0x0100` triggers on signature reads (`#0101`) and instruction fetches | Gating strobe strictly on **data reads** (`!isExecution`) to canon strobe addresses `(addr & 0xFFF3) == 0x0100` (`#0100`, `#0104`, `#0108`, `#010C`) |
| **Service Menu Loop** | Menu appears frozen / no keyboard response | (1) Plane corruption routes INT `#0038` away from `#0114`; (2) WebAPI 2-frame key tap filtered by `(0E051h)` | Correcting the two gating bugs restores uninterrupted 50Hz `#0114` keyboard scans; test scripts must use `>= 15` hold frames |

### Proposed Exact Changes in `core/src/emulator/memory/memory.cpp`

1. **In `Memory::MemoryReadFast` (Line 185 and Line 196)**:
```diff
-    if (_scorpProfromActive && (addr & 0xFFF0) == 0x0100) [[unlikely]]
+    if (_scorpProfromActive && !isExecution && (addr & 0xFFF3) == 0x0100) [[unlikely]]
     {
         uint8_t quadrant = (addr >> 2) & 0x03;
         ResolveScorpionRomBases(quadrant);
         SetROMSystem();
     }

-    if (_scorpionDosTriggerActive && addr >= 0x4000) [[unlikely]]
+    if (_scorpionDosTriggerActive && isExecution && addr >= 0x4000) [[unlikely]]
     {
         _context->emulatorState.scorpionDosTrigger = 0;
         UpdateZ80Banks();
     }
```

2. **In `Memory::MemoryReadDebug` (Line 221 and Line 230)**:
```diff
-    if (_scorpProfromActive && (addr & 0xFFF0) == 0x0100) [[unlikely]]
+    if (_scorpProfromActive && !isExecution && (addr & 0xFFF3) == 0x0100) [[unlikely]]
     {
         uint8_t quadrant = (addr >> 2) & 0x03;
         ResolveScorpionRomBases(quadrant);
         SetROMSystem();
     }

-    if (_scorpionDosTriggerActive && addr >= 0x4000) [[unlikely]]
+    if (_scorpionDosTriggerActive && isExecution && addr >= 0x4000) [[unlikely]]
     {
         _context->emulatorState.scorpionDosTrigger = 0;
         UpdateZ80Banks();
     }
```
