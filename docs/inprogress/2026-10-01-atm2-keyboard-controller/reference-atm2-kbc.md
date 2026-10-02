# Reference: the ATM Turbo 2+ keyboard controller and its COM port

The facts the emulation of the ATM Turbo 2+ (v7.xx) keyboard controller -
an i8031 / AT89S52 that answers every `IN #FE`, carries the PC keyboard,
a clock and the RS-232 port - is built on. Research of 2026-10-01; every
statement carries its source or the tag **[inferred]**. Design:
[tdd-atm2-kbc.md](tdd-atm2-kbc.md).

> **Update 2026-10-01: the v4.0 and v4.1 firmware were found, with sources and
> binaries** (§9). Every statement below that calls v4.x "unknown" is answered
> there; §9 wins where they differ.

Date: 2026-10-01. Scope: everything needed to emulate the serial port of the
ATM Turbo 2+ (v7.xx, unreal-ng model `ATM710`) at register / protocol level so that
NedoOS ESP kernels (comType 1), Moon Rabbit `uart-atm` and other software can talk to
an ESP or any serial peer through it.

Notation: **[S: ref]** = sourced (reference IDs are resolved in the source table, §(c));
**[inferred]** = a reading of the sources or arithmetic, not stated by a source. Local paths are
relative to the emulator source collection (a local archive; every root has the public upstream in §(c)). Firmware line numbers refer to the
CP866->UTF-8 converted files. The line numbering is the same as in the originals,
because the conversion only removed CR characters.

---

## 0. Executive summary

* The "COM port" is **not a UART chip**. It is the built-in UART of the **keyboard
  microcontroller D100 (1816VE31 = i8031; later i8032/8052/AT89S52)**. The Z80 can
  only reach that controller by **reading port #FE**. Every read stalls the Z80 on
  /WAIT until the MCU's INT1 interrupt handler answers. Data travels Z80 -> MCU **only
  on address lines A15..A8** (the high byte of the port address). MCU -> Z80 data
  travels on the data bus. [S: DOC-PORTS §2, SCH-710, FW32]
* The belief in the request is **confirmed, with refinements**. A read with A15..A8 =
  `#55` returns `#AA` and arms command mode for **exactly one** following read. That
  read carries the command in A15..A8: bits 5..0 are the command code, bits 7..6 are a
  sub-address. Write commands take one more read whose A15..A8 is the data byte.
  RS-232 commands (firmware v3.0+): `#02` RX data, `#42` status, `#82` modem status,
  `#C2` RX count, `#03 d` TX data, `#43 d` DTR/RTS, `#83 d` control (INT enable bit),
  `#C3 d` baud "divisor". [S: FW32 :889-1041, DOC-PORTS App.1]
* **Baud rates depend on the firmware and the MCU clock.** The divisor byte uses the
  PC convention (115200/baud), but the actual rate is:
  - v3.x @ 7.000 MHz (the stock board: F0 = 14 MHz / 2): 1/2/3 -> **36458**,
    6 -> 18229, 12 -> 9115, 24 -> 4557, 48 -> 2431, 96 -> 1215.
  - v3.x @ 11.0592 MHz: 1/2 -> **57600**, 3 -> **28800**, 6 -> 19200, 12 -> 9600,
    24 -> 4800, 48 -> 2400, 96 -> 1200.
  - v4.0/v4.1 (AT89S52 @ 11.0592 MHz only): 115200 and 38400 are exact (per the
    release notes; no source available).
  NedoOS's "ATM2 COM 38400" (comType 1, divider 3) is therefore correct only on v4.x.
  On a stock v3.2 @ 7 MHz it runs at 36458 baud (-5.1 %), and on v3.x @ 11.0592 MHz it
  runs at 28800 baud. [S: FW32 :1192-1245, :1958-1980; FWPAGE; INI]
* Buffers (v3.2): **RX ring of 46 bytes. When it is full, the oldest byte is dropped.**
  TX: 1 byte in the shift register plus up to 7 queued (counter max 8; a write while
  full overwrites a queued byte). **No hardware flow control in firmware.** CTS is
  ignored for TX, and RTS changes only when the Z80 writes `#43`. That is why every
  driver pulses RTS. Format fixed at 8N1. Reading `#02` when the buffer is empty
  returns `#00`. [S: FW32]
* **No other emulator emulates this COM port.** UnrealSpeccy 0.39 / Unreal_NS /
  TSLabs Unreal and Xpeccy emulate only the keyboard half of the protocol. MAME carries
  the keyboard ROM dumps but runs no MCU. ZXMAK2, Fuse, ZEsarUX and Spectaculator have
  no ATM2 keyboard controller at all. The firmware source (v3.1/v3.2) is the only
  authority. [S: EMU-*]
* **ATM 4.50 / ATM Turbo 2 (v6.x and earlier) have no 8031 and no COM port.** On
  those boards `#FE` is a plain matrix port. [S: DOC-PORTS §2, DOC-ASM version history]

---

## 1. Hardware (board v7.10, NedoPC "green" = MicroART-equivalent)

### 1.1 Chips and signals

| Item | Fact | Source |
|---|---|---|
| Controller | D100 **1816VE31** (i8031 clone, ROMless). BOM also lists 1830VE31 / 8031 / 80C31 | [S: SCH-710 cp7_2.pdf; BOM d5.txt:126-129] |
| Program ROM | D101 573RF2 (2716-class EPROM) via address latch D99 (555IR23) | [S: DOC-ASM d4.txt:507; SCH-710] |
| MCU clock | pin 19 X2 = **F0**. F0 is Q1 of counter D5 (555IE10 = 74LS161), clocked by the 14 MHz generator U1/D58, so **F0 = 7 MHz**. Pads J8/J9 take an optional separate crystal. Errata: "if the keyboard is unstable, cut F0 from D100.19 and fit a 10-12 MHz crystal on pins 18/19" | [S: SCH-710 cp7_1.pdf (D5 Q1 -> F0), cp7_2.pdf (D100.19 = F0, J8/J9); DOC-ASM d4.txt:294 (14 MHz U1), :529 (J8/J9), :844] |
| RS-232 receivers | D104 **170UP2** (quad line receiver, MC1489-class), inputs CD, RX, CTS, RI -> MCU P1.0 CDV, P3.0 RXV, P1.1 CTSV, P1.2 RIV | [S: SCH-710 cp7_2.pdf; DOC-ASM d4.txt:509] |
| RS-232 drivers | D105, D106 **170AP2** (MC1488-class), from MCU P3.1 TXV, P1.3 DTRV, P1.4 RTSV -> TX, DTR, RTS. Powered from +12 V / -15 V (or -12 V) | [S: SCH-710; DOC-ASM d4.txt:509] |
| Connector | **I2 (ONP-10 header)**: 1 CD, 2 RX, 3 TX, 4 DTR, 5 GND, 7 RTS, 8 CTS, 9 RI. Uses DE-9 numbering, **DSR (pin 6) is not wired** | [S: SCH-710 cp7_2.pdf crop] |
| MCU port map | P1.0 CD in, P1.1 CTS in, P1.2 RI in, P1.3 DTR out, P1.4 RTS out, P1.5 INT_T out, P1.6 /RES out, P1.7 W_ON out. P3.0 RX, P3.1 TX, P3.2 CLK_K (INT0), P3.3 /KEYRD (INT1), P3.4 VE1 in, P3.5 DATA_K, P3.6 /VWR, P3.7 /VRD | [S: FW32 :37-54; SCH-710] |
| Z80 interrupt from MCU | P1.5 **INT_T** is ANDed (D38) with INT_D, giving INTZ, so the MCU can pull the Z80 /INT. The firmware never uses it (`en_int equ 0`). v4.1 enables it on RX overflow | [S: SCH-710 (D38 pins 12/13 -> 11 INTZ); FW32 :18, :260-271; FWPAGE v4.1 item 3] |
| Line polarity | The drivers and receivers invert. Firmware writes P1.3/P1.4 = NOT(bit), and reads "CTS active" when P1.1 = 0. A Z80 bit value of 1 means the line is asserted at the connector | [S: FW32 :947-956, :1022-1029 ("Инверсия битов", "сигнал инверсный")]; driver inversion [inferred from 1488/1489-class parts] |

### 1.2 How a Z80 read of #FE reaches the MCU

* Port decode: `IN #xxFE` = A0=0, A1=A2=1 (KEYRD'). [S: DOC-PORTS §1.2 "in #xxFE =
  %xxxxxxxx11111110 (A0=0, A1=A2=1)"; DOC-ASM d4.txt:424] Xpeccy decodes it the same
  way (mask `0x0007 == 0x0006`). [S: EMU-XPECCY atm2.c:232]
* Flip-flop **D71 (555TM2)**: D = W_ON, clocked by KEYRD at the start of the read.
  Q = WAIT_V, which is ANDed with WAIT_H to give the Z80 WAIT. The preset (/S) comes
  from D79 = /VWR AND /RS. So when W_ON = 0 every #FE read sets WAIT, and the MCU's
  /VWR strobe (or a reset) clears it. /KEYRD also goes to MCU INT1 (P3.3).
  [S: SCH-710 cp7_2.pdf crop; DOC-ASM d4.txt:503-504]
* MCU side: decoder **D103 (555ID4)** and mux D108 create the strobes from /VRD, /VWR
  and VA8 (= P2.0):
  - `movx A,@DPTR` with P2.0 = 0 reads A15..A8 through the buffer D108 while the Z80
    waits (D23 drives the mechanical keyboard matrix; see (b) item 8).
  - `movx A,@DPTR` with P2.0 = 1 reads the native keyboard/tape/joystick port (D45).
  - `movx @DPTR,A` with P2.0 = 1 writes D102, which drives the Z80 data bus, and its
    /VWR clears WAIT.

  [S: FW32 :776-801 comments "Чтение регистра старшего байта адреса Z80",
  "Бит выбора Адрес=0/Данные=1"; DOC-ASM d4.txt:504-507]
* **No FIFO in hardware.** There is one byte register each way, and handshaking is
  by /WAIT only. [S: SCH-710; DOC-PORTS §2]
* Disable path: **port #FF77 bit D6 = VE1**. When it is 1, "8031 functions disabled
  (all requests go to the ZX keyboard)". [S: DOC-PORTS d1.txt:138] With VE1 = 1, the
  firmware's INT1 handler answers `#FF` once. It then sets W_ON = 1 (WAIT generation
  off) and **busy-waits inside the INT1 handler until VE1 = 0**. [S: FW32 :694-713]
  Consequence [inferred]: while VE1 = 1, the serial and timer interrupts (same low
  priority) cannot run. At most one received byte survives in SBUF, the RTC stops, and
  the RX ring is not updated. When VE1 returns to 0 the handler clears keyboard state
  but not the serial buffers.
* VE0 (#FF77 D7) "not used, set to 1". [S: DOC-PORTS d1.txt:139] The assembly manual
  describes both VE0 and W_ON gating. [S: DOC-ASM d4.txt:502-504]

### 1.3 PCB revisions

| Board | Keyboard MCU / COM | Source |
|---|---|---|
| ATM-turbo (v4.50 era, "ATM450" in emulators), ATM-turbo 2 v6.10-6.40 | **No 8031. #FE is a plain matrix port. No RS-232 via #FE.** v6.x IBM keyboard used 537RU10 RAM written via `#FEE7/#FFE7` (WCSF') and read via `#7DFD` | [S: DOC-ASM d4.txt:72-77, :427, :436; DOC-PORTS d1.txt:708, :735] |
| v7.00 (yellow, NedoPC 2003-05) | 1816VE31 + RS-232 introduced ("Подключение IBM клавиатуры и стандартного RS232 реализовано на 1816ВЕ31") | [S: DOC-ASM d4.txt:77; PCAD readme ver_7_00] |
| v7.10 (green, MicroART / NedoPC 2005+) | same circuit (sheet cp7_2) | [S: SCH-710] |
| v7.18, 8.0 Zorel, 8.x | carry the same controller (a forum thread covers "ATM2+ (v7.xx) and ATM3 (v8.x)" controller firmwares). Not schematic-checked here | [S: svn atmturbo pcad tree; ZXPK-31077 title, page 403 to curl] [inferred for 8.x] |
| ZX-Evo BaseConf ("ATM3" in unreal-ng) | no 8031 emulation of `#55` in pentevo (grep negative). Its COM port is the AVR 16550 at `#F8EF` | [S: grep of `svn/pentevo`; reference-evo-com-port.md] |

---

## 2. The controller firmware (8051 source, Kamil Karimov "CARO")

Sources: `svn/atmturbo/source/keyb_rom_805x/ver_2_2_caro`, `ver_3_1_caro`, `ver_3_2_caro`
(assembly source + readme). **v3.2m is the reference**: it is the recommended
version, runs on 128-byte-RAM 8031s, and the 2019 build "fixed baud constants".
[S: FWPAGE, FW32 readme]

Version history [S: FWPAGE, FW22/31/32 readme]:

| Version | Date | COM | Notes |
|---|---|---|---|
| MicroART 1.00 (AT) / 1.06 (XT) | 1990s | none | version bytes 1,0,0,0 / 6,0,1,0. Commands `#16`/`#17` return `#FF` (bug) |
| 2.2 | 2005-03 | none | 7 / 11.0592 / 12 MHz builds |
| 3.0, 3.1(m) | 2006-10 | **RS-232 added** | needs 256-byte RAM (8032/8052); RX buffer 54 bytes |
| **3.2m (m2)** | 2019-11 / 2019-12 | yes | 8031-safe (stack 16 B in 128 B RAM), RX buffer **46** bytes; 7 MHz baud table corrected |
| 4.0 (LVD + Kulicheg) | 2021-10-29 | yes | **AT89S52 @ 11.0592 MHz only**; 115200 and exact 38400 added; Ctrl+Alt+Home / Ctrl+Alt+Ins block / unblock the controller (no WAIT, no answer) |
| 4.1 (Maxagor) | 2023-03-05 | yes | RTC not cleared on restart; old scan-code reader restored; **INT_T on RX overflow enabled** |

The source for v4.x was not found. Only the release notes on the ATM-turbo site
exist [S: FWPAGE]. See §9.

### 2.1 Clock-dependent constants

* Build flags `ft_07` (7.0000 MHz) / `ft_11` (11.0592 MHz). The version bytes at ROM
  `#2C` are `3,2,0,7` or `3,2,1,1`. The shipped source has `ft_11 = 1`.
  [S: FW32 :19-21, :179-186]
* UART: `SCON=#50` (mode 1, 8-bit, REN). Timer 1 in mode 2 (auto-reload) generates
  the baud rate. `PCON.SMOD` comes from bit 7 of the table entry, and
  `TH1 = NOT(entry & 0x7F)` = 256 - N. [S: FW32 :232-236, :1224-1233]
  Baud = 2^SMOD × Fosc / (384 × N). [S: FW32 :1956-1957 comment "N = (Fosc/192)/Baud SMOD=1,
  N = (Fosc/384)/Baud SMOD=0"]
* `set_speed` accepts only the divisor values 1, 2, 3, 6, 12, 24, 48, 96 (table slots
  1..8). **Any other value, including 0, leaves the baud rate unchanged but still
  resets all RX/TX pointers and counters** (= flush). [S: FW32 :1192-1245; DOC-PORTS
  App.1 "Любое другое значение игнорируется, но все указатели ... сбрасываются"]
  The doc lists "98 = 1200", but the code compares with **96**. [S: FW32 :1221 vs DOC-PORTS]

Actual baud rates (computed from the table bytes [S: FW32 :1958-1980; FW31 :1994-2016]; arithmetic [inferred]):

| Z80 divisor (requested) | v3.1 @7 MHz | v3.2 @7 MHz | v3.1 / v3.2 @11.0592 | v4.x @11.0592 |
|---|---|---|---|---|
| 1 (115200) | 36458 | 36458 | 57600 | 115200 [S: FWPAGE] |
| 2 (57600) | 36458 | 36458 | 57600 | 57600 [inferred] |
| 3 (38400) | 36458 | 36458 | **28800** | 38400 [S: FWPAGE] |
| 6 (19200) | 36458 (bug) | 18229 | 19200 | 19200 [inferred] |
| 12 (9600) | 9115 | 9115 | 9600 | 9600 [inferred] |
| 24 (4800) | 4557 | 4557 | 4800 | |
| 48 (2400) | 2431 | 2431 | 2400 | |
| 96 (1200) | 1215 | 1215 | 1200 | |

The v3.1 and v3.2 tables for 11.0592 MHz contain identical bytes (only the comments
changed). The "fix" in 3.2m2 was the 7 MHz slot 4 (`1-1+80h` -> `1-1`).
[S: diff FW31/FW32] Power-on default: divisor 6 (19200 nominal). [S: FW32 :249-250]

### 2.2 Memory layout (v3.2)

Register bank 3 (`#18-#1F`): adr_rs (RX write pointer), adr_rd (RX read pointer),
cnt_rd, cnt_wr, rtc_to. Direct RAM: `#20` b_sadr, `#21` mode, `#22` flags, `#23` stat_md,
`#24` stat_rs (bit 7 = f_int), `#25` adr_wr, `#26` adr_ws, `#27-29` t_res,
**buf_wr 8 bytes (`#2A-#31`)**, **buf_rd 46 bytes (`#32-#5F`)**, and from `#60` the
keyboard buffer, RTC and 16-byte stack. [S: FW32 :56-122] The addresses are computed
from the `ds` directives [inferred].

### 2.3 Interrupt structure and priorities

`IE = #97` (INT0, T0, INT1, serial). `IP = #01`: only INT0 (PS/2 / XT keyboard clock)
is high priority. INT1 (/KEYRD, the Z80 request), Timer 0 (50 Hz RTC) and serial are
low priority. INT0 and INT1 are edge-triggered (`TCON=#55`). [S: FW32 :239, :252-253]
Within the low level the 8051 polls in a fixed order (IE0 > TF0 > IE1 > TF1 > RI/TI)
[inferred, standard MCS-51 behavior]. Consequences:
* While the MCU is serving a Z80 request, no RX byte is stored. A second byte arriving
  while RI is still pending is lost (the MCS-51 UART has no FIFO) [inferred].
* The Z80 wait gets longer when the request lands during a serial ISR (about 20
  cycles), the 50 Hz RTC ISR (about 15 cycles, about 60 at a date rollover), or the
  high-priority keyboard-clock ISR. [inferred from code lengths]

### 2.4 Z80 request handler (INT1) — the protocol state machine

The state is register `R7` of bank 1 ("command flag"); `R3` = command code, `R5` =
prefix (A15..A14), `R6` = last high byte. [S: FW32 :694-1188]

```
on every IN (#FE) with WAIT active:
  if VE1 == 1:  answer #FF, W_ON=1, spin until VE1==0, clear kbd state, R7=0   (:696-713)
  hi = A15..A8
  if R7 == 0:
      if hi == #55: R7 = 1; answer #AA                                        (:783-801)
      else: normal keyboard read by mode (0..3), see §3                       (:716-865)
  elif R7 == 1:                       # command byte
      R5 = hi >> 6 ; code = hi & #3F
      if code == 0: answer #FF, R7=0     (NOP)                               (:877-880, :1072-1073)
      else R3 = code; dispatch (below)
  elif R7 == 2:                       # argument byte for 2-stage commands
      R6 = hi; dispatch(R3, R5) again with R7==2
```

Answers per command (RS-232 group first; "→" = byte the Z80 reads):

| hi byte sequence | Meaning | Z80 reads | Firmware behavior | Lines |
|---|---|---|---|---|
| `#55`,`#02` | read RX data | byte, or **`#00` if the buffer is empty** | pops from the ring, cnt_rd-- | :889-907 |
| `#55`,`#42` | read status | `(stat_rs & #9E) \| RD \| (cnt_wr != 8 ? #60 : 0)` | RD = bit 0 = cnt_rd != 0; **bits 5 (TD) and 6 (TE) are both set whenever the TX count is below 8**, so "TE = empty" is not literally true; bit 7 = f_int; bits 1-4 hold whatever `#83` wrote (the doc says "always 0") | :909-931 |
| `#55`,`#82` | modem status | `DCD<<7 \| RI<<6 \| CTS<<5 \| CTS<<4` | **DSR is a copy of CTS** (no DSR pin); all from P1 pins, active low | :933-958 |
| `#55`,`#C2` | RX count | cnt_rd (0..46) | | :960-963 |
| `#55`,`#03`,`d` | TX data | `#FF`, `#FF` | if cnt_wr == 0: `SBUF = d` immediately, cnt_wr = 1, both TX pointers reset. Else: if cnt_wr < 8 then cnt_wr++, and d is always stored at adr_ws++ (ring of 8) | :966-1005 |
| `#55`,`#43`,`d` | modem control | `#FF`, `#FF` | `P1 = (~(d<<3) & #18) \| #67`, so DTR = d0 and RTS = d1 (1 = asserted). Side effects: INT_T = 1, /RES = 1, W_ON = 0 | :1007-1030 |
| `#55`,`#83`,`d` | RS-232 control | `#FF`, `#FF` | `stat_rs = d` (bit 7 = INT enable; unused in v3.2) | :1032-1036 |
| `#55`,`#C3`,`d` | baud | `#FF`, `#FF` | `set_speed(d)`, then **flushes both buffers** | :1038-1041, :1192-1245 |
| `#55`,`#x1` | version byte x (x = hi>>6) | ROM `#2C+x`: 3,2,{0,7 or 1,1} | | :1044-1051 |
| `#55`,`#x7` | clear Spectrum key buffer | `#FF` | | :1053-1061 |
| `#55`,`#x8`,`m` | set mode m&3 | `#FF`, `#FF` | | :1063-1073 |
| `#55`,`#x9` | read kbd register R(x+1) | byte | | :1087-1093 |
| `#55`,`#xA` / `#xB` | RUS / LAT | `#FF` | | :1095-1103 |
| `#55`,`#xC` | enter WAIT (pause) mode | `#FF` | | :1105-1108 |
| `#55`,`#xD` | reset computer (pulse /RES 10 ms) | — | | :1110-1112 |
| `#55`,`#10+x` / `#12+x` | read time / date byte x | byte | | :1114-1130 |
| `#55`,`#11+x`,`v` / `#13+x`,`v` | set time / date byte | `#FF`,`#FF` | | :1120-1136, :1178-1188 |
| `#55`,`#14`,`v` / `#15`,`v` | P1 \|= v / P1 &= ~v | `#FF`,`#FF` | | :1138-1158 |
| `#55`,`#16` / `#17` | read P3 / P1 | port value | | :1160-1168 |
| `#55`, any other code | — | `#FF` | R7 = 0 | :1165, :1072 |

Notes:
* Only bits 5..0 select the command. So `#43`/`#83`/`#C3` are all command 3, and
  `#42`/`#82`/`#C2` are all command 2, told apart by A15..A14. [S: FW32 :872-881]
* After `#55`, **the next #FE read is always taken as the command**, whatever its high
  byte. An interrupt handler that scans the keyboard between the two reads would issue
  a random command. All drivers therefore wrap each sequence in DI/EI.
  [S: EMU/SW listings §4] [inferred risk]
* A second `#55` sent while R7 = 1 is command `#15` (clear P1 bits) with prefix 1.
  It is two-stage, so the next byte would be ANDed into P1 (it could clear /RES or
  W_ON). [inferred from the dispatch]
* In mode 0 (Spectrum keyboard emulation), a game that scans rows with A15..A8 = `#55`
  enters command mode. The doc warns that the controller "restricts some combinations
  of A8-A15". [S: DOC-PORTS d1.txt:632] [inferred mechanism]

### 2.5 Serial ISR (bank 3) [S: FW32 :1254-1297]

* **RX:** if cnt_rd == 46, drop the oldest byte (advance the read pointer,
  cnt_rd--). Then store SBUF at the write pointer and increment cnt_rd.
  Result: **keep the newest 46 bytes**.
* **TX done (TI):** cnt_wr--. If the count is still non-zero, send `buf[adr_wr++]`.
  Under this accounting cnt_wr counts the byte in the shift register plus the queued
  bytes. With cnt_wr = 8 and a further write, the new byte overwrites a queued,
  unsent byte (the TX ring is 8 entries while at most 7 can be pending).
  [inferred from :980-995 + :1259-1276]
* 8N1 only. No parity, no break, no framing or overrun reporting. [S: SCON=#50; status bit map]

### 2.6 Reset behavior

* Power-on (`prog`): P1 = #FF (DTR/RTS deasserted, W_ON = 1). Wait 10 ms. Clear RAM
  `#02-#37`, which **includes the 'ATM' signature at `#27`**. Baud = divisor 6.
  Buffers empty. Mode 0. stat_rs = 0. Then P1 = #7F (W_ON = 0, WAIT enabled).
  [S: FW32 :220-254]
* Ctrl+Alt+Del or command `#0D` (`reset`): pulse /RES for 10 ms, then check the 'ATM'
  signature. Because `prog` clears the signature, the check always falls through to a
  **full re-init** (baud back to 19200-nominal, buffers flushed, mode 0).
  [S: FW32 :198-208 + :224-246] [inferred: the signature can never survive]
  v3.1 differs: it parks with /RES low (`ajmp $`) and relies on the reset line also
  resetting the MCU. [S: FW31 diff]
* Z80 reset from the front-panel button: whether the MCU's RST pin follows the board
  RS line could not be read reliably from the schematic crop (pin 9 RST is labeled
  "RS"). **Open question**. With v3.2's "corrected RESET circuit" the MCU most likely
  stays running [inferred].

### 2.7 Timing (Z80 stall per `IN #FE`)

Exact cycle counts follow from the source. One MCS-51 machine cycle = 12 / Fosc:
1.714 µs @ 7 MHz, 1.085 µs @ 11.0592 MHz. Interrupt latency is 3..9 machine cycles
[inferred, MCS-51 datasheet behavior]. The counts below run up to the `movx @DPTR`
that releases WAIT [inferred, hand-counted from FW32]:

| Request | Cycles to release | @7 MHz | @11.0592 MHz |
|---|---|---|---|
| `#55` -> `#AA` | 25 + latency = 28..34 | 48-58 µs | 30-37 µs |
| `#02` / `#42` / `#C2` (command byte, read answer) | ~54 + latency = 57..63 | 98-108 µs | 62-68 µs |
| `#03` first byte (answer `#FF`) | ~40 + latency | ~75 µs | ~47 µs |
| `#03` data byte | ~58 + latency | ~105 µs | ~67 µs |
| mode 0 keyboard read (no key pressed) | ~22 + latency | ~45 µs | ~28 µs |

After each answer the handler needs 8 more cycles (pop ×3, reti) before it can take
the next request. That only matters when two `IN #FE`s are less than about 14 µs
apart. Firmware notes say: "from WAIT release to the end of the Z80 read cycle at
3.5 MHz: 0.4-0.7 µs" and "/VWR = 0.5 µs (movx) or 1.1 µs (direct) at 11.0592 MHz".
[S: FW31/32 readme]

Derived throughput [inferred]:
* TX: 5 reads per byte (`55`,`42`,`55`,`03`,`d`) ≈ 0.40 ms @7 MHz / 0.25 ms @11 MHz,
  so about 2.5 / 4 KB/s. That is slower than a 36458-baud line (3.6 KB/s): TX never
  backs up at v3.2 @7 MHz.
* RX: 4 reads per byte (`55`,`C2`,`55`,`02`) ≈ 0.31 / 0.20 ms, so about 3.2 / 5 KB/s.
  That is below the line rate at 57600 and 115200. **Without RTS pulsing, a continuous
  stream overruns the 46-byte ring.**
* NedoOS fork comments measure "24 empty polls ≈ 30 ms on 8952", which is about 1.25
  ms per poll iteration (2 status reads + a 6-read RTS pulse ≈ 8 reads, so about
  150 µs per read). That is slower than my count and probably includes driver
  overhead. [S: ESPNET :1318]
* The ATM2IOESP author reports that ATM2IOESP is "3+ times faster than the standard
  ATM2 port". [S: ZXPK-36278 via WebFetch]

---

## 3. Mode switching and interaction with normal keyboard reads

* There is no separate "COM mode". **Every** `IN #FE` (when the controller is present,
  VE1 = 0 and W_ON = 0) goes to the MCU. Ordinary reads answer according to the
  keyboard **mode** set by `#55,#08,m`:
  0 = Spectrum matrix emulation (key bits AND native port: tape bit, joystick,
  mechanical keyboard), 1 = last CP/M code (read clears it), 2 = CP/M code / flag
  registers selected by A15..A14, 3 = last XT scan code. [S: DOC-PORTS §2 + App.1; FW32 :716-865]
* The COM commands work in every mode. They are escapes on the same channel, so a
  COM transaction needs no mode change. [S: FW32 dispatch order]
* Mode 0 merge detail: the answer is `(native D45 value) AND (controller key bits)`.
  When no key is pressed the native value is passed through unchanged (tape bit D6,
  Z bit D5). [S: FW32 :723-761]
* Every keyboard read stalls the Z80 for about 45-60 µs @7 MHz [inferred §2.7]. This
  affects timing-sensitive Spectrum software on real hardware, and only `#FF77` VE1 = 1
  (or removing the chip) avoids it. [S: DOC-PORTS §2]

---

## 4. Software that uses the port (exact sequences)

All use `IN r,(C)` with B = the byte to send and C = `#FE`, and all run under DI.

**NedoOS userland `esp-com.c`** (comType 1). Upstream svn `nedoos.ru`, mirrored in
the alfishe/NedoOS fork. [S: ESPCOM]
* init (:194-203): `55FE`, `C3FE`, `(div<<8)|FE`, then `55FE`, `43FE`, `03FE`
  (DTR + RTS on).
* write (:75-86, sendcommand :502+): loop {`55FE`, `42FE`} until bit 5 (TD) is set,
  then `55FE`, `03FE`, `(data<<8)|FE`.
* hasByte (:227-231): `55FE`, `C2FE` returns the count.
* read (:247-251): `55FE`, `02FE`.
* setrts (:125-155): mode 1 = `55FE 43FE 03FE`; mode 0 = `55FE 43FE 00FE`;
  pulse = both back to back.
* uartFlush (:269-291): RTS on, drain with `C2FE`/`02FE`, RTS off.

`ini/espcom.ini` documents: `comType: ... 1 ATM2 COM 38400`; divider default 1 (for
the 16550). The ATM2 user sets divider = 3. [S: INI]

**NedoOS ESPNET `_sdk/espnet.asm`**, used by the ATM2 ESP kernels
(`build_kernel_atm2*_esp.bat`: `atm=2`, `INETDRV=0x02`). [S: ESPNET, KBAT]
* `esp_ui1` (:2017-2033): `55FE`, `C3FE`, `div`, `55FE`, `43FE`, `03FE`. Comment:
  "Keep DTR+RTS on: ESP32 CTS is 8952 RTS (GPIO15). Off => ESP never TXes."
* `esp_fill1` (:1555-1604): per byte `55FE`/`C2FE`; if non-zero `55FE`/`02FE`;
  if empty, RTS pulse `55FE 43FE 03FE 55FE 43FE 00FE`. Comment: "Do not tighten the
  RTS pulse – short strobes lose bytes on 8952."
* `esp_wr1` (:1746-1770): wait for TD with `55FE`/`42FE`, `bit 5`; then `55FE`,
  `03FE`, data. Comment: "03FE to data-strobe is ~50T".
* `esp_atm2_mcr` (:1606-1614).
* NedoOS fork comments call the MCU "8952", i.e. v4.x on AT89S52. [S: ESPNET :67, :1293, :1318, :1554, :2031]

**Moon Rabbit `drivers/atm-uart.asm`** (NedoOS tree; Kulicheg's `uart-atm.asm` is
the same apart from the RTS delay). [S: MR, MR-K]
* init: `55FE`, `C3FE`, `03FE` (divider 3 = "38400").
* read: loop {`55FE`, `C2FE`; if 0: RTS pulse `55FE 43FE 03FE` … `55FE 43FE 00FE`}
  then `55FE`, `02FE`. The pulse is `push de / pop de` (NedoOS) or `djnz` ×10
  (Kulicheg).
* write: wait for **bit 6** (TE) of `42FE`, then `55FE`, `03FE`, data.
  Bit 6 and bit 5 behave the same in v3.2.

**NedoOS `time2`** uses the same channel for the RTC: `55FE` + `(reg<<8)|FE`
[+ value]. [S: TIME2 :120-145]

**ATM CP/M / BIOS** (MicroART): checks the first version byte for 10 or 16, and the
game MINER detects the controller by `55` -> `AA`, `00` -> `FF`. [S: DOC-PORTS App.1]
Terminal programs for ATM CP/M were not located [open].

Wiring an ESP to the real port needs an RS-232 <-> 3.3 V level shifter. The ESP's
CTS (GPIO15 on ESP32 ESPNET firmware) is driven from the ATM's RTS. [S: ESPNET :2031]
[inferred: level shifter]

---

## 5. Other emulators (consensus table)

| Emulator | Keyboard-controller protocol (`#55`/`#AA`, modes, RTC) | RS-232 commands (`#02/#03/#42/#43/#82/#83/#C2/#C3`) | WAIT timing | Source |
|---|---|---|---|---|
| UnrealSpeccy 0.39 (nedopc build), Unreal_NS, alfishe/unreal-speccy, TSLabs zx-evo-unreal | Yes, when `ATMKBD=1` (`conf.atm.xt_kbd`). Emulates firmware **v1.06** (version 6,0,1,0), modes 0-3, RTC from host clock, reset `#0D` | **No.** Codes 2/3 fall to "R7 = 0; return `#FF`". For `#03` the data byte is then misread as a keyboard read | none | [S: EMU-UNREAL input.cpp:467-550 (alfishe), :496 (nedopc), :927 (NS), zx-evo-unreal :450] |
| Xpeccy | Yes (partial: version, modes, RUS/LAT, RTC; `#08/#11/#13/#14/#15` take an argument) | **No** (returns `#FF`; `#03`/`#43`/`#C3` take no argument, so the stream desyncs) | none | [S: EMU-XPECCY atm2.c:118-224] |
| MAME `atm.cpp` (atmtb2plus) | No MCU: ships `rf2ve3.rom` (XT) and `rfat710.rom` (AT) dumps in a "keyboard" region that nothing uses | No | — | [S: EMU-MAME atm.cpp:568-570, 591-593, 600-602] |
| ZXMAK2 | No (ATM450/710 memory and ULA only) | No | — | [S: EMU-ZXMAK2 `src/ZXMAK2.Hardware/Atm/`] |
| Fuse | No ATM model | — | — | [S: EMU-FUSE model list] |
| ZEsarUX | No ATM Turbo 2+ (has Pentagon, ZX-Evo BaseConf / TS-Conf) | — | — | [S: EMU-ZESARUX README] |
| Spectaculator | No ATM model found | — | — | [S: EMU-SPECT home page; not checked in depth] |
| unreal-ng (now) | `xt_kbd` config field only (`core/src/emulator/platform.h:752`); no controller | No | — | local grep |

Unreal deviations from the firmware [S: EMU-UNREAL vs FW32]:
* `#08` first stage returns `8` instead of `#FF`.
* `#09` uses a switch without `break`, so it always returns kR4.
* The `#03` argument byte is not consumed.

**Consensus:** none exists for the COM half. The **firmware source is the single
authority**, cross-checked with the MicroART/NedoPC port document (App.1) and the
drivers. On the keyboard half, Unreal and Xpeccy agree with the firmware on `#55` ->
`#AA`, NOP -> `#FF` and the mode semantics.

---

## 6. ATM 4.50 / ATM Turbo 2 differences

* No 8031 and no RS-232 on v4.50 or v6.x. The doc says the controller appeared in
  v7.00. [S: DOC-PORTS d1.txt:735; DOC-ASM d4.txt:72-77]
* So `ATM450` must not get this device. Its `#FE` read is the plain matrix + tape port.
  The unreal-ng atm450 requirements already treat `#FE` read as plain (PAL bit 7).
* Modem history: MicroART + Analitik-TS "Z-Contact 1200" (V.22, 1200 baud) internal
  modem on the I1 connector, and a Hayes modem driven through `#FA`/`#FB` (the
  external-device bus). That is a different device. [S: DOC-ASM d4.txt:556, :911;
  DOC-PORTS §4]

---

## 7. ATM2IOESP and AY-UART (later steps N5/N6) — pointers only

* **ATM2IOESP** (Kulicheg): ESP32 + **TL16C550C** on the ATM I/O-expansion connector.
  Register index goes to `#FB` (the CTS bus / "address"), data to/from `#FA`. GAL
  equations F0 / F8 select the base. Docs: `ATM2IOESP manual.pdf`, `GAL/*.jed`,
  datasheets. [S: IOESP repo tree] NedoOS comType 3: `out (#FB),reg; out/in (#FA)`.
  [S: ESPCOM :86-94, :166-178, :252-258] Forum thread: [S: ZXPK-36278].
* **AY-UART:** Moon Rabbit `drivers/ay-uart.asm` (bit-banged on AY port A, reg 14);
  existing notes in `reference-esp-modules.md`. [S: MR drivers dir]

---

## 8. Emulation notes beyond the register model [inferred]

* **Firmware-version knob:** `[ATM] KbdFw=V32_7MHZ | V32_11MHZ | V4` (name to be
  decided). It selects the baud table, RX buffer size and INT_T behavior. Default V4,
  so the NedoOS "38400" setup works. A peer should see a baud mismatch as garbage,
  exactly as hardware would (e.g. 28800 vs 38400).
* The baud rate matters only for pacing (byte time = 10 / baud). Model the MCU UART
  as one byte in the shift register + SBUF hold. Feed the RX ring at line rate.
* **WAIT model:** stall the Z80 for a fixed duration per request class (table §2.7,
  at the configured Fosc), plus up to one serial-ISR time when an RX/TX event is
  pending. Never zero: the drivers' throughput and ESPNET timeouts depend on it.
* RX bytes must be delivered at line rate into the 46-byte ring, dropping the oldest
  on overflow. The ESP peer must honor ATM RTS as its CTS (ESPNET firmware does this
  in hardware; the AT firmware may not).
* TTD: the whole MCU state is small (ring contents and pointers, cnt_wr, TX queue,
  R7/R3/R5, mode, stat_rs, P1 latch, baud slot, keyboard registers, RTC) and fits one
  journal blob.

---

## (a) Register / command model ready to implement

```text
Device: Atm2KbdController (ATM710 only; absent on ATM450 / ATM3)
Decode: IN with (port & 0x07) == 0x06   [A0=0, A1=A2=1]; OUT #FE unaffected (border)
Gate:   active when present && FF77.D6 (VE1)==0 && P1.7 (W_ON)==0
        otherwise #FE read = native port (matrix AND, D5=Z, D6=tape, D7=1)

Every gated IN #FE:
   hi = (port >> 8) & 0xFF
   stall Z80 for T_req(class) (see §2.7; @7 MHz: 55≈53us, cmd-read≈103us,
   cmd-write≈75/105us, kbd≈45us; scale ×0.633 for 11.0592 MHz)
   result = Step(hi)

State: R7 ∈ {0,1,2}, cmd (6 bit), pfx (2 bit), mode (2 bit)
       rx[46] ring (v3.1: 54; v4: unknown, assume 46), rxCount, txQueue(max 8 incl. shifting), 
       statRs (byte), dtr, rts (outputs, 1=asserted), baudSlot, rtc, kbd regs

Step(hi):
  if R7==0:
     if hi==0x55: R7=1; return 0xAA
     return KeyboardRead(mode, hi)
  if R7==1:
     pfx = hi>>6; cmd = hi & 0x3F
     if cmd==0: R7=0; return 0xFF
  else: arg = hi            # R7==2
  switch cmd:
   0x02: R7=0; switch pfx:
          0: if rxCount==0 return 0x00; else return pop()
          1: return (statRs & 0x9E) | (rxCount?1:0) | (txCount!=8 ? 0x60 : 0)
          2: return (DCD<<7)|(RI<<6)|(CTS<<5)|(CTS<<4)        # DSR := CTS
          3: return rxCount
   0x03: if R7==1: R7=2; return 0xFF
         R7=0; switch pfx:
          0: TxWrite(arg)    # if txCount==0 -> start shifting now, txCount=1
                             # else txCount=min(txCount+1,8); enqueue (8-entry ring, overwrite when full)
          1: dtr = arg&1; rts = (arg>>1)&1   # also forces INT_T=1, W_ON=0
          2: statRs = arg                    # bit7 = INT enable (v4.1: INT_T when rx overflows; v3.2: no effect)
          3: SetSpeed(arg)                   # 1,2,3,6,12,24,48,96 -> slot; ANY value flushes rx/tx
         return 0xFF
   0x08: two-stage, mode = arg & 3
   0x11,0x13,0x14,0x15: two-stage (time/date set, P1 or/and-not)
   0x01,0x07,0x09,0x0A,0x0B,0x0C,0x0D,0x10,0x12,0x16,0x17: single-stage (see §2.4 table)
   default: R7=0; return 0xFF

Baud (actual) by firmware: §2.1 table. Format 8N1.
RX from peer: at line rate; if rxCount==CAP: drop oldest; push.
TX to peer: one byte per 10/baud; CTS ignored by firmware.
Power-on: baud slot 4 (div 6), buffers empty, mode 0, statRs 0, DTR/RTS deasserted.
Ctrl+Alt+Del / cmd 0x0D: Z80 reset pulse + full controller re-init (v3.2).
```

## (b) Open questions (sources do not answer)

Second pass 2026-10-02: each item is marked **answered**, **partly** or **open**, with its
evidence. Schematic facts come from crops of the v7.10 sheet `cp7_2.pdf` (and `cp7_1.pdf`)
in [pcad/ver_7_10](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/pcad/ver_7_10/);
v8.x facts come from the netlist in the ASCII P-CAD file `АТМ Турбо 8.01+.pcb` in
[pcad/ver_8_0_zorel](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/pcad/ver_8_0_zorel/).
Forum posts are on [zx-pk thread 31077](https://zx-pk.ru/threads/31077-o-zapuske-raznykh-proshivok-na-kontrollere-pc-klaviatury-v-atm2-(v7-xx)-i-atm3-(v8-x).html)
(pages [2](https://zx-pk.ru/threads/31077-o-zapuske-raznykh-proshivok-na-kontrollere-pc-klaviatury-v-atm2-(v7-xx)-i-atm3-(v8-x)/page2.html),
[3](https://zx-pk.ru/threads/31077-o-zapuske-raznykh-proshivok-na-kontrollere-pc-klaviatury-v-atm2-(v7-xx)-i-atm3-(v8-x)/page3.html),
[4](https://zx-pk.ru/threads/31077-o-zapuske-raznykh-proshivok-na-kontrollere-pc-klaviatury-v-atm2-(v7-xx)-i-atm3-(v8-x)/page4.html))
and the 92-page [zx-pk thread 17340](https://zx-pk.ru/threads/17340-atm-turbo-2-ver-7-10-sborka-i-naladka.html)
("ATM Turbo 2+ ver 7.10 - сборка и наладка"). Both answer 200 to `curl` with a browser
User-Agent; the old `archive/index.php/t-31077.html` URL now answers 404.

1. **v4.0 / v4.1 firmware** - **answered** in §9 (sources, binaries, buffer sizes, Timer 2
   baud table, INT_T threshold). The blocked mode is item 8 below.

2. **Which firmware and clock real NedoOS ESP users run** - **partly**.
   - The author of the ATM2 ESP path, Kulich (Kulicheg), co-wrote firmware v4.0 and says
     it "works only on an 8952 and needs the 11 MHz crystal" (thread 31077, post 1135523,
     2021-11-05). His NedoOS sources call the controller "8952" throughout
     ([espnet.asm](https://raw.githubusercontent.com/alfishe/NedoOS/main/src/_sdk/espnet.asm)
     :67, :1318, :1554, :2031; [scrnet main.asm](https://raw.githubusercontent.com/alfishe/NedoOS/main/src/scrnet/main.asm)
     :234, :2825; commits of 2026-09). So the reference setup is **AT89S52 + v4.x @
     11.0592 MHz**, which is the only one where divisor 3 gives 38400.
   - Other owners: xolod runs v3.2 on an ATM 8.0 with an AT89C51 at 11 MHz (post 1035104,
     2019-11-18); Alexey_Mikhaylov found v4.0 on an AT89S52 at 11 MHz glitchy (stuck keys in
     TR-DOS, phantom typing in NedoOS) and went back to v3.2m on an AT89C51 (post 1190046,
     2023-12-04). The v4.1 notes name exactly that fix: the v3.1 scan-code reader restored.
     Earlier, many v7.xx boards ran only v2.2 because v3.x overflowed the stack on 128-byte
     8051s (fixed in 3.2m, 2019-11-15, post 1034616) and because of noise on the MCU data bus
     (fixed by 1 kOhm pull-ups on VD0-VD7, [dev_kbd.htm](http://atmturbo.nedopc.com/dev_kbd.htm)).
   - 7 MHz + v3.2 for an ESP at 38400: the rate is 36458 baud, -5.1 %. An 8N1 receiver
     samples the stop bit 9.5 bits after the start edge, so the whole budget for both ends
     is about 5 % in theory and 2-3 % in practice: **not usable** [inferred]. v3.x at
     11.0592 MHz gives 28800 for divisor 3: not usable either.
   - No post or doc names the firmware of other NedoOS ESP users. **Open:** a census.

3. **Does the front-panel reset also reset D100** - **answered: yes on v7.10, no on v8.01**.
   - v7.10 (`cp7_2` crops): the board reset line `/RS` (Z80 /RES with R2 10 kOhm + C1
     0.1 uF on `cp7_1`; reset button J33/J34 [DOC-ASM]; LPT/X3 pin `/RS` per the
     [errata page](http://nedopc.com/ATMZAK/atm710re.htm) drawing
     [atm710re18.png](http://nedopc.com/ATMZAK/atm710re18.png)) goes into the inverter
     **D80 (pin 11 -> 10)**, then **R85 (3 kOhm) / C11 (0.1 uF)** to the net `RS`, which is
     **D100 pin 9 RST**. So every board reset (power-on, button, Ctrl+Alt+Del or command
     `#0D`) resets the MCU too.
   - D100 pin 7 (P1.6, `/RES` in the firmware) is drawn on the same reset line. The overbar
     cannot be read in the PDF (it falls on a wire), but the firmware pulls P1.6 low to reset
     the computer, so it must be `/RS` [inferred]. The MCU therefore **resets itself**: v2.2
     and v3.1 rely on it ("`sjmp $` ; жду своего сброса" - wait for my own reset;
     `atm_at22.asm` :139-140, `atm_at31.asm` :224-225). Pull-down of `/RS`, then ~0.2 ms
     for R85/C11, then the MCU resets, P1 = #FF releases `/RS`, which recharges through
     R2/C1 in ~1 ms. The Z80 reset pulse is about 1 ms, not the firmware's 10 ms (v3.2m) or
     120 ms (v4.x) [inferred from the RC values].
   - After a reset the MCU starts at 0 -> `prog`: full cold init. **The COM buffers are
     flushed**, the divisor goes back to 6, keyboard mode 0, `stat_rs` 0, DTR/RTS off
     (P1 = #FF). v4.1 no longer clears the clock (`at41.txt`). The 3.2m / 4.x warm path
     ("`reset:` ... `cjne @R0,#'A',prog` ... `jmp c0main`") is reached only on boards whose
     MCU reset is decoupled. The headers of 3.2m and 4.1 say "для схемы с исправленным
     RESET" (for the circuit with the corrected RESET).
   - That circuit is on **v8.01** (Zorel netlist): D99 (I8751) RST = net `RS\``, which
     reaches only jumper header X5 pin 2. X5.1 = D80.10, the inverted private power-on RC
     (R117 to +5 V, C58 to GND, VD23 discharge diode, on D80.11 = X5.3). X5.4 = `~RS`. P1.6
     drives `~RS` through diode VD20 only. So with X5 1-2 fitted the MCU is reset at power-on
     only and survives a Z80 reset.
   - During the cold start W_ON = 1 (P1 = #FF) until `mov P1,#7Fh`: ~10 ms (v3.2m) or
     ~120 ms (v4.x, two `del_60ms`). The Z80 is already running then, and its `IN #FE` reads
     get the native port with no WAIT (§(b) item 8 for the bus value).

4. **/WAIT release timing on real hardware** - **partly** (mechanism and calculated windows;
   no oscilloscope measurement found in the 92 + 4 pages read or on the NedoPC pages).
   - Circuit (`cp7_2`): D71 (TM2) is clocked by KEYRD at the start of the read with D =
     W_ON; Q = WAIT_V, ANDed (D79.1-3) with WAIT_H into WAIT_I. /S = D79.8 = /VWR AND /RS.
     The MCU's /VWR or a reset releases the Z80.
   - Data path: the native buffer **D45** is enabled by `/KRD` = NAND(KEYRD, /VWR) (D73).
     The MCU's answer buffer **D102** (555AP6) is enabled by VEBUF = D103 output 2Y1 (pin
     10, both D79.12/13), that is /VWR = 0 with VA8 = 1 **and /KEYRD = 0** (D103 A1 =
     /KEYRD). D102 is a transceiver, not a latch: the Z80 sees the answer only while /VWR is
     low and its read is still in progress.
   - Karimov's analysis (`at32m.txt`, release note of 26/10/06, in [ver_3_2_caro](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/source/keyb_rom_805x/ver_3_2_caro/)):
     from the WAIT release to the end of the Z80 read at 3.5 MHz takes 0.4-0.7 us; a MOVX
     /WR lasts 6 oscillator clocks (0.54 us at 11.0592 MHz); driving P3.6 by hand gives
     1.1 us. So MOVX-strobed images (2.2, 3.2m, 4.0) are marginal above ~10 MHz, and 3.1 /
     4.1 strobe by hand (`en_movx equ 0`).
   - Errata "Зависание компьютера при работе с АТ клавиатурой" ([atm710re.htm](http://nedopc.com/ATMZAK/atm710re.htm)):
     a glitch on /KEYRD set D71 without reaching INT1 and hung the Z80; fix 750 pF from
     /KEYRD to +5 V (D76.9-14).
   - **Open:** a scope trace of /KEYRD -> /VWR on a real board.

5. **Polarity of the 170AP2 drivers / 170UP2 receivers** - **answered: both invert**.
   - `cp7_2` draws an inverted output (bubble, Ō) on every D104 receiver and every D105 /
     D106 driver output (ŌA, ŌB).
   - The BOM maps 170UP2 = SN75154 and 170AP2 = SN75150. TI:
     [SN75154](https://www.ti.com/lit/ds/symlink/sn75154.pdf) "Inverting Output Compatible
     With TTL"; [SN75150](https://www.ti.com/lit/ds/symlink/sn75150.pdf) "Inverting Output".
     v8.01 uses a [GD75232](https://www.ti.com/lit/ds/symlink/gd75232.pdf) (SN75188 /
     SN75189 cells, also inverting).
   - Wiring: D104 I2.1 CD -> CDV P1.0, I2.2 RX -> RXV P3.0, I2.8 CTS -> CTSV P1.1, I2.9 RI
     -> RIV P1.2. D105 / D106: TXV P3.1 -> I2.3 TX, RTSV P1.4 -> I2.7 RTS, DTRV P1.3 -> I2.4
     DTR, from +12 V / -15 V. This matches the firmware's inverted writes and reads (§1.1).
   - Note: the SN75150 is specified for 20 kbit/s into 2500 pF. 115200 baud (v4.x) relies on
     short cables [inferred].

6. **Boards 7.18 / 8.x: D100 wiring, I2 pinout, F0 clock** - **partly** (7.18) /
   **answered** (8.01).
   - **7.18** (Zorel, 2014): "fully duplicates 7.10 except the RAM (SIMM-72 chips), RAM
     jumpers removed, R55 added for a VE31 and R56 for an AT89S5x" (zorel, thread 17340, posts
     436905 and 725870). The binary P-CAD 2001 PCB in
     [pcad/ver_7_18](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/pcad/ver_7_18/)
     has no netlist that can be read here. Its strings carry the same parts (1816VE31,
     170AP2, 170UP2, D80, R85, C11, `RS` / `~RS`, `7Mhz`). The 7.10 reset circuit and I2 are
     assumed [inferred]. A user with 7.10 and 7.18 boards from Zorel sees the same reset
     behavior on both (post 822501).
   - **8.01** (Zorel, ASCII netlist): MCU D99 = **I8751** (internal ROM). I2 pins 1 DCD,
     2 RX, 3 TX, 4 DTR, 5 GND, 6 **DSR (wired here, to P2.1 / pin 22)**, 7 RTS, 8 CTS, 9 RI,
     10-11 GND - the same DE-9 numbering. Level shifter GD75232 (D100 / D101 footprints).
     Clock: crystal U3 (value "7Mhz") with C41 / C54 on XTAL1 / XTAL2, or F0 through jumper
     X20 ("7 MHz") to XTAL1 (pin 19). Reset: decoupled through X5 (item 3). P2 also carries
     an I2C RTC (SCL / SDA to PCF8583 D124) and a PS/2 mouse (DAT_M / CLK_M), which no
     firmware in the collection drives. The thread for
     [8.10 rev. 2019](https://zx-pk.ru/threads/29717-atm-turbo-8-10-rev-2019.html) was not
     read.
   - **7.10 I2** to a PC-style DE-9 male, pin n to pin n:
     [atm710re17.png](http://nedopc.com/ATMZAK/atm710re17.png) ("COM-порт (DB-9M папа)").

7. **ATM CP/M terminal / modem programs using `#55`/`#02`/`#03`** - **answered (one found)**.
   - **ZXTERM** ("Test RS232 for ATM", `ZXTERM.M80` + `MODEM.INC` + `ZXTERM.COM`), on the
     ATM site as "ZX-Terminal for DialUp (+исходники)", for "ATM2+ (+keybROM v3.x)":
     [zxterm.zip](http://atmturbo.nedopc.com/download/cpm/system/zxterm/zxterm.zip)
     (listed on [load_cpm.htm](http://atmturbo.nedopc.com/load_cpm.htm)).
   - Sequences: probe `55FE` -> `#AA`, `00FE` -> `#FF`; version `#01/#41/#81/#C1`, and **the
     first byte must be 3**, so v4.x is refused ("No work vers."). Init `55FE C3FE 06FE`
     (divisor 6, "19200"), `55FE 43FE 03FE` (DTR + RTS on). TX: wait for `#82` bit 4 (CTS)
     and `#42` bit 5 (TX empty), then `55FE 03FE dataFE`. RX: `#42` bit 0, then
     `55FE 02FE`. DCD = `#82` bit 7. It also carries a 16550 branch (Kondratyev `#F8EF`,
     Shepelev `#38BF`).
   - Probably Kamil Karimov's "primitive terminal under CP/M" that Maxagor used over a
     null-modem cable to a laptop (thread 17340, post 975047, 2018-08-10).
   - A byte scan of every disk image in the ATM CP/M collection found `55FE` sequences only in
     ZXTERM (COM) and in games that set the keyboard mode (`#08`): MINER, KING, GOBLINS,
     MAGIC SQUARES, PRINCE.

8. **What `#FE` returns in v4.x "blocked" mode** - **answered**.
   - The keys are the reverse of the release notes. `at40.txt` says Ctrl+Alt+Home blocks and
     Ctrl+Alt+Ins unblocks. The code (identical in `atm_at40.asm` and `atm_at41.asm`
     :523-549) compares the CP/M key code R2: keypad **'7' (7/Home) -> `clr P1.7`, W_ON = 0
     (unblock)**; keypad **'0' (0/Ins) -> dummy /VWR, then `setb P1.7`, W_ON = 1 (block)**.
     Ctrl+Alt+'.' (Del) is the reset. That matches the 2023 report that Ctrl+Alt+Home
     "did not block at all" (post 1190046).
   - With W_ON = 1, D71 is not set (D = 1), so **no WAIT**. D45 drives D0-D4 = mechanical
     keyboard KD1-KD5 for the read's own A15..A8 (D23 drives the matrix from the Z80
     address bus at all times, E1 = GND), D5 = Z, D6 = TIN (tape in). **D7 is not driven by
     D45** (the floating / pulled-up bus) [inferred]. That is the same value as VE1 = 1 after
     the parked answer, and as any read during the MCU cold start.
   - The MCU still gets INT1 (/KEYRD is wired straight to P3.3) and runs its handler
     3-10 us later. By then /KEYRD is high: /ACS and KRDV (D103 first half, A1 = /KEYRD)
     stay off, so its reads of A15..A8 (D108) and of the native port see the floating VD bus
     (#FF with the dev_kbd 1 kOhm pull-ups). Its /VWR answer never reaches the Z80 (VEBUF
     needs /KEYRD = 0).
   - The block lasts until Ctrl+Alt+Home or any reset (`prog` writes P1 = #7F).

**Impact on the emulation** (`core/src/emulator/io/keyboard/atm2kbc.*`):

- Unserved read (VE1 = 1 after the parked answer, W_ON = 1, v4 block, MCU cold start): the
  native port for the read's own high byte, no WAIT. Current `ReadPort` already does this
  through `_native(port)`. Keep bit 7 as the decoder's plain `#FE` read gives it [inferred].
- **Change (done 2026-10-02, `Atm2Kbc::MovxRead`, test `ABlockedControllerSeesNoEscape`):** in the `waitOff` branch the MCU's INT1 handler runs after the Z80 cycle has
  ended, so its MOVX reads of the address (D108) and of the native port (D45 via KRDV) must
  see **#FF**, not `port >> 8`. Today `_board.latchedHigh` is set from the port before the
  branch. On hardware a blocked controller cannot be armed by a `#55FE` poll; in the
  emulator it can, and it would answer out of step after Ctrl+Alt+Home.
- Reset: the current model (MCU reset on `BoardReset()` and on its own P1.6, RAM kept,
  restart at 0) is the v7.10 / 7.18 circuit. Keep it as the default. The COM buffers, divisor,
  mode and `stat_rs` reset because the firmware cold-starts. A decoupled reset (v8.01 X5) would
  be a board option; ATM710 does not need it.
- Do not hold the Z80 in reset for the firmware's own 10 / 120 ms pulse: on the board the
  MCU resets itself within ~0.2 ms and `/RS` returns within ~1 ms. Then the first ~10 ms
  (v3.2m) or ~120 ms (v4.x) of Z80 code reads the native port without WAIT, which the
  emulator gets by running the firmware.
- Blocked mode: no code change (the firmware decides). Docs, UI and automation text must
  say **Ctrl+Alt+Ins blocks, Ctrl+Alt+Home unblocks**.
- Known deviation, no change planned: the emulator always delivers the answer byte. On
  hardware MOVX-strobed images (2.2, 3.2m, 4.0) hold it for only 0.54 us at 11.0592 MHz
  (item 4).
- Test fixture: ZXTERM refuses v4.x, so it exercises the `V31-*` / `V32-*` presets.

## (c) Sources

Each URL was checked with `curl -L` on 2026-10-01. For svn.nedopc.com, `filedetails.php`
answers 200 but serves a JavaScript browser check, so the **directory listing** URLs
(real content, 200) are given. The files are under them.

| ID | What | Local path | Public URL | HTTP |
|---|---|---|---|---|
| FW32 | Keyboard/RS-232 firmware v3.2m source `atm_at32.asm` + `at32m.txt` | `svn/atmturbo/source/keyb_rom_805x/ver_3_2_caro/` | [listing ver_3_2_caro](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/source/keyb_rom_805x/ver_3_2_caro/) | 200 |
| FW31 | Firmware v3.1 source `atm_at31.asm` | `.../ver_3_1_caro/` | [listing ver_3_1_caro](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/source/keyb_rom_805x/ver_3_1_caro/) | 200 |
| FW22 | Firmware v2.2 source + readme | `.../ver_2_2_caro/` | [listing ver_2_2_caro](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/source/keyb_rom_805x/ver_2_2_caro/) | 200 |
| FWPAGE | Firmware release notes v2.2 / v3.x / **v4.0 / v4.1** | — | [atmturbo.nedopc.com atm_at.htm](http://atmturbo.nedopc.com/download/shems/roms/atm_at.htm) | 200 |
| DOC-PORTS | "Описание архитектуры и портов ATM2+.doc" (§1.2 #FE, §2 controller, App.1 i8031 programming incl. RS-232 commands) | `svn/atmturbo/doc/ver_7_10/` | [listing doc/ver_7_10](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/doc/ver_7_10/) | 200 |
| DOC-ASM | "Сборка и Наладка Турбо2+.doc" / "TURBO 2+ Assembly and Configuration Manual.doc" (versions, IBM keyboard circuit, jumpers, errata) | same | same listing | 200 |
| BOM | "Bill of material.doc" | same | same listing | 200 |
| SCH-710 | v7.10 schematics `cp7_1.pdf` (clock F0), `cp7_2.pdf` (D100, D104-D106, I2, D71, D103), `cp7.pdf` (placement) | `svn/atmturbo/pcad/ver_7_10/` | [listing pcad/ver_7_10](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/pcad/ver_7_10/); mirror [Kulicheg/ATM_Turbo Docs/schematics](https://github.com/Kulicheg/ATM_Turbo) | 200 / 200 |
| SVNROOT | atmturbo repository root (svn://svn.nedopc.com/atmturbo) | `svn/atmturbo` | [WebSVN atmturbo](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/) | 200 |
| ATMSITE | ATM-turbo description site | — | [atmturbo.nedopc.com/atmdscr.htm](http://atmturbo.nedopc.com/atmdscr.htm) | 200 |
| ESPCOM | NedoOS `src/kapps/common/esp-com.c` (upstream svn://nedoos.ru/nedoos, local `svn/nedoos`) | `github/NedoOS/src/kapps/common/esp-com.c` | [alfishe/NedoOS esp-com.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/esp-com.c) | 200 |
| ESPNET | NedoOS `src/_sdk/espnet.asm` | `github/NedoOS/src/_sdk/espnet.asm` | [espnet.asm](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/espnet.asm) | 200 |
| KBAT | ATM2 ESP kernel build script | `github/NedoOS/src/kernel/build_kernel_atm2_esp.bat` | [build_kernel_atm2_esp.bat](https://github.com/alfishe/NedoOS/blob/main/src/kernel/build_kernel_atm2_esp.bat) | 200 |
| INI | `release/ini/espcom.ini` (comType list) | `github/NedoOS/release/ini/espcom.ini` | [espcom.ini](https://github.com/alfishe/NedoOS/blob/main/release/ini/espcom.ini) | 200 |
| ESPCFG | NedoOS espcfg uart_init (comType 1) | `github/NedoOS/src/kapps/espcfg/main.c:118-128` | [espcfg/main.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/espcfg/main.c) | 200 |
| TIME2 | NedoOS time2 RTC via controller | `github/NedoOS/src/kapps/time2/main.c:120-145` | [time2/main.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/time2/main.c) | 200 |
| MR | Moon Rabbit `drivers/atm-uart.asm` (NedoOS tree; identical in `svn/nedoos`) | `github/NedoOS/src/moon-rabbit-zx/drivers/atm-uart.asm` | [atm-uart.asm](https://github.com/alfishe/NedoOS/blob/main/src/moon-rabbit-zx/drivers/atm-uart.asm) | 200 |
| MR-K | Kulicheg Moon Rabbit fusion `uart-atm.asm` | — | [uart-atm.asm](https://github.com/Kulicheg/ATM_Turbo/blob/main/NedoOS/mrabbit-fusion/drivers/uart-atm.asm) | 200 |
| IOESP | ATM2IOESP hardware (TL16C550C + ESP32, GAL, manual) | — | [Kulicheg/ATM2IOESP](https://github.com/Kulicheg/ATM2IOESP) | 200 |
| ZXPK-36278 | zx-pk.ru "ATM2IOESP - WiFi модем для ATM2" | — | [zx-pk thread 36278](https://zx-pk.ru/threads/36278-atm2ioesp-wifi-modem-dlya-atm2-v-formate-io-expansion.html) | **403** to curl (read via WebFetch) |
| ZXPK-31077 | zx-pk.ru "О запуске разных прошивок на контроллере PC-клавиатуры в ATM2+ (v7.xx) и ATM3 (v8.x)" | — | [zx-pk archive t-31077](https://zx-pk.ru/archive/index.php/t-31077.html) | **403** (not read; needs a browser) |
| EMU-UNREAL | UnrealSpeccy ATM_KBD (`input.cpp` 411-417 gate, 467-550 read) | `github/unreal-speccy/input.cpp` | [alfishe/unreal-speccy input.cpp](https://github.com/alfishe/unreal-speccy/blob/master/input.cpp) | 200 |
| EMU-UNREAL (nedopc 0.39, NS) | `tools/unreal_fix/0.39.0/nedopc/input.cpp:496`, `Unreal_NS/SRC/input.cpp:927` | `github/zxevo.pentevo/...` | [nedopc input.cpp](https://github.com/aaydev/zxevo.pentevo/blob/main/tools/unreal_fix/0.39.0/nedopc/input.cpp), [Unreal_NS input.cpp](https://github.com/aaydev/zxevo.pentevo/blob/main/tools/unreal_fix/0.39.0/Unreal_NS/SRC/input.cpp) | 200 / 200 |
| EMU-UNREAL (TSLabs) | `Unreal/input.cpp:400,450` | `github/zx-evo-unreal` | [tslabs input.cpp](https://github.com/tslabs/zx-evo-unreal/blob/master/Unreal/input.cpp) | 200 |
| EMU-XPECCY | `src/libxpeccy/hardware/atm2.c:118-232` | `github/Xpeccy` | [atm2.c](https://github.com/samstyle/Xpeccy/blob/master/src/libxpeccy/hardware/atm2.c) | 200 |
| EMU-MAME | `src/mame/sinclair/atm.cpp:568-602` | `github/mame` | [atm.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/atm.cpp) | 200 |
| EMU-ZXMAK2 | `src/ZXMAK2.Hardware/Atm/` | `github/ZXMAK2` | [zxmak/ZXMAK2](https://github.com/zxmak/ZXMAK2) | 200 |
| EMU-FUSE | model list (no ATM) | — | [fuse-emulator.sourceforge.net](https://fuse-emulator.sourceforge.net/) | 200 |
| EMU-ZESARUX | README machine list (no ATM2) | — | [zesarux README](https://github.com/chernandezba/zesarux/blob/main/README.md) | 200 |
| EMU-SPECT | Spectaculator home (no ATM model found) | — | [spectaculator.com](https://www.spectaculator.com/) | 200 |

Tools used: `iconv` (CP866), `textutil` (.doc -> txt), Python PyMuPDF / PIL (schematic
crops), `qpdf`, `curl`. Nothing was installed.

---

## 9. Firmware v4.0 / v4.1 (found after the first pass)

The archives are linked only from the ATM-turbo news page
([oldnews9.htm](http://atmturbo.nedopc.com/oldnews9.htm): "исходники прилагаются
в архиве"), not from the firmware page. Both ship the assembly source, a
`.bin`, a `.hex` and release notes:

| Archive | Contents | Date |
|---|---|---|
| [atm_at40.zip](http://atmturbo.nedopc.com/download/shems/roms/atm_at40.zip) | `atm_at40.asm`, `SFRs.asm`, `MOD52`, `at40.bin` (1923 bytes), `at40.hex`, `at40.txt` | 2021-11-04 |
| [atm_at41.zip](http://atmturbo.nedopc.com/download/shems/roms/atm_at41.zip) | `atm_at41.asm`, `SFRs.asm`, `MOD52`, `at41.bin` (2003 bytes), `at41.hex`, `at41.txt` | 2023-01-27 build, notes 2023-03-05 |
| [atm_at32.zip](http://atmturbo.nedopc.com/download/shems/roms/atm_at32.zip) | v3.2m source, `AT32m07n.HEX`, `AT32m11n.HEX` | 2019-12-07 |
| [atm_at22.zip](http://atmturbo.nedopc.com/download/shems/roms/atm_at22.zip) | v2.2 `.HEX` and `.rom` for 7 / 11.0592 / 12 MHz, `atm_xt.txt` | 2005-03 |

Local copy in the emulator source collection (outside svn, added 2026-10-01,
each folder with a `SOURCE.md` naming the origin): `svn/atmturbo/source/keyb_rom_805x/ver_4_0_lvd/`
and `.../ver_4_1_maxagor/`, the archives unchanged and unpacked.

How the images answer the Z80 (found by running them, 2026-10-01): 2.2, 3.2m and
4.0 write the answer with `MOVX @DPTR` (P2.0 = 1); **3.1 and 4.1 drive P0 and
strobe /VWR (P3.6) by hand** (`en_movx equ 0`). For 3.1 the source that matches
the shipped image is the svn one ([ver_3_1_caro](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/source/keyb_rom_805x/ver_3_1_caro/),
`en_movx equ 0`, plus `debug` and 12 / 24 MHz options); the site's `atm_at3x.zip`
carries an edit with `en_movx equ 1` and the same HEX images. The `.BIN`
images of 3.1 and 3.2m exist only in svn (`at31.zip`, `V32.zip`); the site's
archives have HEX.

What v4 changes, from its source (`atm_at41.asm`):

- **An 8052 (AT89S52), not an 8031:** 256 bytes of internal RAM, the upper 128
  reached indirectly. The TX buffer is `$80..$BF` and the RX buffer `$C0..$FF`,
  **64 bytes each** (`len_bwr`, `len_brd`, :57-58, :154-155).
- **Timer 2 makes the baud rate** (`T2CON = #34`, :262): baud = 11059200 / 32 /
  (65536 - RCAP2). `set_speed` (:1270-1320) takes the divisors **1, 2, 3, 4, 6,
  8, 12, 24, 48, 96** and loads 65536 - 3 x divisor, so **baud = 115200 /
  divisor exactly**. Divisor 0 or any other value leaves the rate alone and
  flushes both buffers, as before.
- 11.0592 MHz only (version bytes 4,0,1,1 / 4,1,1,1 per the notes).
- `at40.txt` says Ctrl+Alt+Home blocks the controller (no answer, no WAIT: the
  native port is read, tape works) and Ctrl+Alt+Ins unblocks it; the code does
  the reverse - **Ctrl+Alt+Ins blocks, Ctrl+Alt+Home unblocks** ((b) item 8).
- v4.1 (Maxagor): the clock is not cleared at a (re)start; the v3.1 scan-code
  reader is back; the Z80 interrupt on an RX buffer overflow is enabled
  (`len_ird = len_brd - 4`, :59).

So open questions 1 and 2 in (b) are answered: the NedoOS "ATM2 COM 38400"
(divisor 3) is exact on v4.x, and the emulator can run the real v4.1 ROM.
