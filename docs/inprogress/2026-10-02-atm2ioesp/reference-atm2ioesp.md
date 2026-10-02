# ATM2IOESP: reference for emulation

Research for emulating the ATM2IOESP Wi-Fi card on the ATM Turbo 2+ (v7.10). Everything a faithful
model needs: the hardware, the Z80 interface, how NedoOS drives it, the sources, and how it fits into
our network infrastructure. Statements marked **[inferred]** are conclusions drawn from the sources,
not something a source says outright.

Related: [network adapters TDD](../2026-09-30-nedoos-integration/tdd-network.md),
[ESP modules reference](../2026-09-30-nedoos-integration/reference-esp-modules.md),
[ATM2 keyboard controller reference](../2026-10-01-atm2-keyboard-controller/reference-atm2-kbc.md)
(the "ATM2 COM" port, comType 1).

## Summary

- **What it is.** A small card by Kulich (Kulicheg on GitHub, also the author of the NedoOS network apps
  and ESPNET). It plugs into the ATM Turbo 2+ **INTERNAL I/O** connector, not the ZX-Bus. It carries a
  **TL16C550C** UART (PLCC-44, labeled 16C550CFN on the PCB), a **1.8432 MHz** crystal, a
  **GAL16V8** address comparator and a **Wemos D1 Mini ESP32** module. Released in 2025 (Rev 1.0,
  Rev 1.5, Rev 2.0).
- **How the Z80 reaches it.** Through the ATM mainboard's "external devices port". **OUT (#FB)** latches
  an 8-bit device address onto the CT0..CT7 bus. That is the same latch that drives the printer data
  lines and the Covox DAC. **IN/OUT (#FA)** then strobes IORD'/IOWR' on the connector. The card
  compares CT7..CT4 with four jumpers and checks CT3 (0 on Rev 1.5/2.0, 1 on Rev 1.0). CT2..CT0 select
  the 16550 register. Default addresses: **#F0..#F7** (Rev 1.5/2.0) or **#F8..#FF** (Rev 1.0).
- **The index is a full 8-bit bus address, not 0..7.** Software writes `#F0 + reg` to #FB. NedoOS keeps
  these addresses in `espcom.ini` (`RBR_THR = 0xF0` ... `SR = 0xF7`, comType 3).
- **No interrupt line and no wait states** on the connector [inferred from the connector pinout]. Flow
  control is done in software: the drivers pulse MCR RTS (write 2, then 0) while waiting for data, and
  leave AFE off.
- **Prerequisite in our ATM710 decoder:** port #FE must decode as A0=0, A1=1, A2=1, as the ATM
  documentation says. Today we decode #FE on A0 alone. With that, `IN A,(#FA)` reaches the keyboard
  controller (as a command, with a Z80 wait) and `OUT (#FA)` changes the border. The card could not
  work.
- **No other emulator emulates it.** Xpeccy and MAME at least keep #FA apart from #FE. Unreal Speccy and
  ZXMAK2 do not.

## Hardware

### The card

| Item | Value | Source |
|---|---|---|
| Author | Kulich (GitHub `Kulicheg`), 2025 | repo, forum thread |
| Slot | ATM Turbo 2+ **INTERNAL I/O** connector (2 x 12 pins, rows A12/B12 on the silkscreen) | manual p.3-4, PCB |
| UART | TL16C550C, PLCC-44 (silkscreen "DD2 - 16C550CFN") | PCB image, datasheet in the repo |
| UART clock | 1.8432 MHz crystal, 22 pF / 50 pF, 1 MΩ bias resistor | PCB image |
| Baud | 1843200 / (16 x divisor): divisor 1 = 115200, 2 = 57600, 3 = 38400 ... | manual p.6 ("Делитель частоты 1 = 115200; 2 = 57600; 3 = 38400") |
| Address decode | GAL16V8 (DD1): CT7..CT4 vs jumpers SET7..SET4, plus CT3 | `ATM2-ESP32.PLD` |
| Radio | Wemos D1 Mini ESP32 (ESP32-WROOM): TX/RX/RTS/CTS wired to the UART | PCB image, `wemos_D1_Mini_ESP32_pinout.jpg` |
| ESP firmware as shipped | Espressif AT firmware. Setup is `AT+UART=115200,8,1,0,3` (RTS+CTS), `AT+CWMODE=1`, `AT+CWJAP=...` | manual p.7 |
| Pass-through | A second header on the card carries the bus on to more cards | forum post ("Имеет пасстру"), PCB |
| Speed claim | "More than 3 times faster than the standard ATM2 port" (the KBC RS-232 at 38400) | forum post |
| Compatibility | "Compatible with UniProg and Analytic". Rev 1.5 moved the address from #F8 to #F0 "for compatibility with UniProg" | forum post, manual p.13 |

Revisions (manual appendix G):

| Rev | Difference | Default addresses |
|---|---|---|
| 1.0 | First public version. The GAL wants CT3 = 1 | #F8..#FF |
| 1.5 | Only the GAL changed (CT3 = 0) | #F0..#F7 |
| 2.0 | Cosmetic PCB changes | #F0..#F7 |

The GitHub commit "Смена порта с F8 на F0" (2025-05-07) holds both GAL builds, `ATM2-ESP32-F8.jed` and
`ATM2-ESP32-F0.jed`. The two PLD sources differ only in the CT3 polarity.

### GAL equations (Rev 1.5/2.0, `GAL/ATMIO-ESP32-F0.zip`, also manual appendix A)

```
PIN 2..5  = SET7..SET4       /* jumper block: the high nibble of the address */
PIN 6..9  = CTS7..CTS4       /* CT7..CT4 from the ATM's #FB latch */
PIN 16    = !CTS3            /* Rev 1.0 (F8 build): PIN 16 = CTS3 */
PIN 11    = !RESET           /* RS line of the connector */
PIN 12 = MR; PIN 13 = !CS0; PIN 14 = !CS1; PIN 15 = !CS2;
MR  = RESET;
CS0 = !(SET7$CTS7) & !(SET6$CTS6);
CS1 = !(SET5$CTS5) & !(SET4$CTS4);
CSA = CS0 & CS1 & CTS3;
CS2 = CSA;
```

So the UART is selected when `CT[7:4] == jumpers` and CT3 has the revision's level. The jumpers are all
1 by default, giving the high nibble F. The UART's A0..A2 come from CT0..CT2 (traced on the Rev 2
gerbers, open question 4). The connector's RS line
(system reset) drives MR, so **a machine reset resets the UART**.

### The ATM side: INTERNAL I/O and ports #FA / #FB

From "Описание архитектуры и портов ATM2+.doc" (NedoPC atmturbo SVN, `doc/ver_7_10`), section 4
"Порт внешних устройств", and the assembly manual "Сборка и Наладка Турбо2+.doc" / "TURBO 2+ Assembly
and Configuration Manual.doc":

- `in/out #nnFA = %nnnnnnnn11111010 (A0=A2=0, A1=1)`. #FA is the data port: IORD'/IOWR' strobes on
  the INTERNAL and EXTERNAL I/O connectors. The value written to #FB acts as an 8-bit address bus, so
  "up to 256 external devices" can be addressed. Historically this port served the UniProg programmer
  and the Z-Contact 1200 Hayes modem.
- `out #nnxB = %nnnnnnnnx1111011 (A2=0, A0=A1=1)`. #FB is the printer data latch (D51). With A7=0
  (#7B) it also strobes the printer. "The CTS0-CTS7 bus is both the printer data bus and the address
  bus for external devices." The same CT lines feed the **Covox DAC** (572PA1). Every index write is
  therefore also a Covox sample. The card's manual warns that the data "may be heard in the speakers".
- `in #nnFB`: printer status. D7 = BUSY (1 = not ready), D0..D6 = 1. **The address latch cannot be
  read back.**
- `#FE = %nnnnnnnn1111x110 (A0=0, A1=A2=1)`, implemented on D16/D17 (signals BRDWR'/KEYRD'). The
  partial decode (open ports) is on **A2..A0**: #FA = 010, #FB = 011, #FE = 110, #FF = 111. #7FFD,
  #FFFD and #BFFD carry A1=0. #FA therefore collides with nothing on the real board.
- The external ports are "open" (not shadow): no DOSEN / CPM gating.

Connector pinout as printed on the card (rows A12/B12): `12V 5V GND GND D7 D6 D5 D4 D3 D2 D1 D0` and
`WR -12 RD CT7 CT6 CT5 CT4 CT3 CT2 CT1 CT0 RS`. There is **no INT and no WAIT pin**. The TL16C550C
INTRPT output is not connected at all (open question 5), and accesses run at full speed
[inferred]. The manual warns against plugging a Centronics printer into the card: ±12 V sit where
STROBE and BUSY would be.

## Z80 interface

### Ports

| Port | Dir | Decode (ATM 7.10) | Function for ATM2IOESP |
|---|---|---|---|
| #FB (and any port with A2..A0 = 011, e.g. #7B) | OUT | A2=0, A1=A0=1; A7=0 also strobes the printer | Latch CT7..CT0 = device address (`#F0 + reg`). Also the Covox sample and the printer data |
| #FB | IN | same | Printer BUSY in D7. Nothing from the card |
| #FA (A2..A0 = 010) | IN | A2=0, A1=1, A0=0 | IORD': the selected device drives D0..D7 (the 16550 register CT2..CT0) |
| #FA | OUT | same | IOWR': the selected device latches D0..D7 |

Higher address bits (A3..A15) are not part of the #FA/#FB decode, per the ATM docs and Xpeccy's mask
0x0007. MAME decodes #FB on the full low byte. Software only ever uses #FA/#FB.

A read of #FA with no matching device returns #FF: the IO lines float into the LS-TTL buffer D87
(open question 1, answered from the schematic).

### Register index (bus address = base + reg; base #F0, or #F8 on Rev 1.0)

| Bus addr | Reg | Read | Write |
|---|---|---|---|
| #F0 | 0 | RBR (DLAB=0) / DLL (DLAB=1) | THR / DLL |
| #F1 | 1 | IER / DLM | IER / DLM |
| #F2 | 2 | IIR | FCR |
| #F3 | 3 | LCR | LCR |
| #F4 | 4 | MCR | MCR (bit 1 RTS, bit 5 AFE) |
| #F5 | 5 | LSR (bit 0 DR, bit 5 THRE) | - |
| #F6 | 6 | MSR (bit 4 CTS) | - |
| #F7 | 7 | SCR | SCR |

This is a plain TL16C550C register set: 16-byte FIFOs, AFE (auto-RTS/CTS) in MCR bit 5. Nothing
extra. The manual (appendix B) says auto-RTS needs a genuine TI chip. Many re-marked 16550s are on
the market, so "the card's software does not use it, and that mode is not guaranteed".

### Bus sequence

```
OUT (#FB), #F0+reg     ; CT bus = address; GAL selects the UART (and Covox hears it)
IN  A, (#FA)           ; IORD' -> UART RD, A0..A2 = CT0..CT2
OUT (#FA), data        ; IOWR' -> UART WR
```

The latch holds, so repeated #FA accesses reach the same register without rewriting #FB. The NedoOS
receive loop relies on this: "LSR stays selected between bytes". Pairs are wrapped in DI/EI because
an interrupt handler might change the latch, for example by playing Covox.

### Interrupts, waits, flow control

- Interrupts: none reach the Z80 [inferred: no INT pin]. Software writes IER = 0.
- Waits: none [inferred]. The only WAIT source on the ATM 7.10 bus path is the keyboard controller
  on #FE reads.
- Flow control: init writes MCR = #22 (AFE + RTS) and then immediately MCR = 0 (`esp_setrts(0)` /
  `uart_setrts(0)`). The final state is AFE off and RTS deasserted. While polling for data the
  drivers pulse RTS: `MCR = 2; MCR = 0`. The ESP (hardware flow control on via `AT+UART=...,3`) may
  then start sending. The transmit side polls LSR.THRE and, in ESPNET, MSR.CTS (bit 4) before each
  byte.

## Software usage

All code is quoted from NedoOS (`alfishe/NedoOS` mirror, branch `main` at 44049473, the same files as
online). The ATM2IOESP code is by Kulich and was first added to `esp-com.c` in commit 9f6c7880
(2024-12-21).

### espcom.ini

[`release/ini/espcom.ini`](https://raw.githubusercontent.com/alfishe/NedoOS/main/release/ini/espcom.ini)
lines 1-5 list the types:

```
; comType: 0 Kondratyev w/o AFC 115200
;          1 ATM2 COM 38400
;          2 Kondratyev AFC 115200
;          3 ATM2IOESP w/o AFC 115200
```

The shipped defaults are the #xxEF (Kondratyev / ZX-WiFi) ports with comType 0. For the card, the
manual (p.6) gives the register entries as `0xF0 ... 0xF7` (Rev 1.0: `0xF8 ... 0xFF`), divider 1,
comType 3.

### C library: `src/kapps/common/esp-com.c` (getpic, gopher, updater, zxartrad, time2, cuart, calendar, zxdb)

[esp-com.c](https://raw.githubusercontent.com/alfishe/NedoOS/main/src/kapps/common/esp-com.c):

```c
// esp-com.c:46-61
void portOutput(char port, char data)
{ disable_interrupt(); output(0xfb, port); output(0xfa, data); enable_interrupt(); }
char portInput(char port)
{ char byte; disable_interrupt(); output(0xfb, port); byte = input(0xfa); enable_interrupt(); return byte; }
```

`port` is a `char`, so the ini value's low byte (#F0..#F7) is what reaches #FB.

```c
// esp-com.c:87-95  uart_write, case 3
while ((portInput(LSR) & 32) == 0) { }
disable_interrupt(); output(0xfb, RBR_THR); output(0xfa, data); enable_interrupt();

// esp-com.c:205-216  uart_init, case 3
portOutput(IIR_FCR, 0x87);   // FIFO on, reset both, trigger 8
portOutput(LCR, 0x83);       // 8N1, DLAB=1
portOutput(RBR_THR, divisor);// DLL
portOutput(IER, 0x00);       // DLM = 0
portOutput(LCR, 0x03);       // DLAB=0
portOutput(IER, 0x00);       // no interrupts
portOutput(MCR, 0x22);       // AFE + RTS ...
enable_interrupt();
uart_setrts(0);              // ... then MCR = 0

// esp-com.c:253-258  uart_read, case 3
output(0xfb, RBR_THR); data = input(0xfa); output(0xfb, 0x00);
```

`uart_read` parks the latch at #00 afterwards. That silences the Covox level and deselects the card.
The RTS helpers (`esp-com.c:155-178`) write MCR = 2 or 0, or the pulse 2, 0. `uartReadBlock`
(`esp-com.c:366-385`) and `getdataEsp` (`esp-com.c:468-496`) poll LSR.DR, pulse RTS while it is empty,
and then read RBR. `uartBench` (`esp-com.c:681-695`) times the same sequence to scale timeouts.
[`esp-com2.c:56-75`](https://raw.githubusercontent.com/alfishe/NedoOS/main/src/kapps/common/esp-com2.c)
(GIRC) does the same burst read: RTS pulse, LSR, RBR.

### ESPNET driver: `src/_sdk/espnet.asm` (userland, and the kernel via `src/kernel/espnet.asm`)

[espnet.asm](https://raw.githubusercontent.com/alfishe/NedoOS/main/src/_sdk/espnet.asm):

```asm
; espnet.asm:14-15
; comType 0 Kondratyev no AFC, 1 ATM2 COM, 2 Kondratyev AFC, 3 ATM2IOESP.
; Type 3 uses 16550 indices 0..7 (from ini high byte-0xF8, or low byte if <0xF8).

; espnet.asm:2265-2273  ini port -> #FB value
esp_reg8
        ld a,h
        cp 0xf8
        jr c,esp_reg8lo
        sub 0xf8          ; #F8EF.. -> 0..7
        ret
esp_reg8lo
        ld a,l            ; 0x00F0.. -> #F0..
        ret
```

With the card's ini values (0x00F0..0x00F7), `esp_reg8` yields #F0..#F7, which is correct. The
shipped #xxEF defaults give 0..7, and a real card does not answer those addresses (CT7..CT4 = 0 does
not match the jumpers). The emulation must behave the same way.

```asm
; espnet.asm:1655-1700  esp_fill3: block receive
esp_fill3  di / ld a,(esp_rLSR) / out (0xfb),a          ; LSR selected once
esp_f3_next: in a,(0xfa) / rrca / jr c,esp_f3_read      ; DR?
  ... empty: out (0xfb),MCR / out (0xfa),2 / out (0xfa),0 / out (0xfb),LSR / in a,(0xfa) ...
esp_f3_read: out (0xfb),RBR / in a,(0xfa) / (ix)=a / out (0xfb),LSR

; espnet.asm:1772-1797  esp_wr3 / esp_in3: transmit after LSR.THRE (bounded by esp_factor)
; espnet.asm:1829-1834  esp_cts3: MSR bit 4 (CTS) checked before each byte
; espnet.asm:1894-1925  esp_rts3: MCR 2 / 0 / pulse
; espnet.asm:2036-2068  esp_ui3 / esp_out3: FCR 87, LCR 83, DLL div, DLM 0, LCR 03, IER 0, MCR 22, then setrts(0)
; espnet.asm:2093-2116  esp_rp3: one poll (LSR, RBR, or the RTS pulse when empty)
; espnet.asm:2240-2263  esp_ports_ready: esp_reg8 for every register
```

The kernel build for `atm==2/3` keeps both ATM UARTs, type 1 and type 3 (`espnet.asm:85-98`). The
kernel defaults to type 1. `espcfg` (`src/kapps/enet/main.c:181-193`, `com_name`) switches type
through `OS_SETUART`. `scrnet` (`src/scrnet/main.asm:2826`) mentions the card only in a comment.

### Other software

- The forum post lists NedoOS calendar, cuart, getpic, gopher, time2 (`-e`), updater (`E`/`e`),
  zxartrad and zxdb (all through `esp-com.c`), plus TASiS `tasiterm.com`, a terminal. No TASiS
  sources were available locally.
- The UniProg programmer and the Z-Contact 1200 modem share the INTERNAL I/O bus (#FA/#FB). They use
  other bus addresses: the Rev 1.5 move to #F0 was made to avoid UniProg [inferred: UniProg uses the
  #F8..#FF range].

## Sources

Online (each returned HTTP 200 with curl on 2026-10-02):

- [zx-pk.ru thread 36278 "ATM2IOESP - WiFi модем для ATM2 в формате IO expansion"](https://zx-pk.ru/threads/36278-atm2ioesp-wifi-modem-dlya-atm2-v-formate-io-expansion.html):
  the announcement, the software list and the repo link. The thread has a single post.
- [Kulicheg/ATM2IOESP on GitHub](https://github.com/Kulicheg/ATM2IOESP): PCB (Sprint Layout `.lay6`,
  gerbers), GAL sources and JEDs, datasheets, manual.
  - [ATM2IOESP manual (PDF, Russian)](https://raw.githubusercontent.com/Kulicheg/ATM2IOESP/main/ATM2IOESP%20manual.pdf):
    installation, addresses, espcom.ini, AT setup, appendix A (GAL), B (programming), G (revisions).
  - [GAL folder](https://github.com/Kulicheg/ATM2IOESP/tree/main/GAL): `ATMIO-ESP32-F0.zip` /
    `ATMIO-ESP32-F8.zip` (PLD, JED, simulation).
  - [Commit "Смена порта с F8 на F0"](https://github.com/Kulicheg/ATM2IOESP/commit/71c2fb9927ef57134c5024109280d8b3dc6704ee).
  - [PCB Rev 02 image](https://raw.githubusercontent.com/Kulicheg/ATM2IOESP/main/PCB/pcb_rev02.PNG):
    component values, connector pinout.
  - [TL16C550C datasheet (repo copy)](https://raw.githubusercontent.com/Kulicheg/ATM2IOESP/main/Datasheets/tl16c550c.pdf).
- NedoOS sources (alfishe mirror):
  [espnet.asm](https://raw.githubusercontent.com/alfishe/NedoOS/main/src/_sdk/espnet.asm),
  [esp-com.c](https://raw.githubusercontent.com/alfishe/NedoOS/main/src/kapps/common/esp-com.c),
  [esp-com2.c](https://raw.githubusercontent.com/alfishe/NedoOS/main/src/kapps/common/esp-com2.c),
  [espcom.ini](https://raw.githubusercontent.com/alfishe/NedoOS/main/release/ini/espcom.ini),
  [commit 9f6c7880 "Добавлена поддержка ATM2IOESP"](https://github.com/alfishe/NedoOS/commit/9f6c78806f68d001a54eee6f7c08e22086a4a7ad).
- [NedoOS site](http://nedoos.ru/), [ATM Turbo site (NedoPC)](http://atmturbo.nedopc.com/),
  [ATM Turbo schematics page](http://atmturbo.nedopc.com/atmshem.htm).
- Emulator code:
  [Xpeccy atm2.c](https://raw.githubusercontent.com/samstyle/Xpeccy/master/src/libxpeccy/hardware/atm2.c),
  [MAME atm.cpp](https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sinclair/atm.cpp),
  [Unreal Speccy io.cpp (alfishe mirror)](https://raw.githubusercontent.com/alfishe/unreal-speccy/master/io.cpp),
  [ZXMAK2 MemoryAtm710.cs](https://raw.githubusercontent.com/zxmak/ZXMAK2/master/src/ZXMAK2.Hardware/Atm/MemoryAtm710.cs).

Local only (NedoPC SVN `svn://svn.nedopc.com/atmturbo`, which curl cannot check):

- `doc/ver_7_10/Описание архитектуры и портов ATM2+.doc`: port sections 1.4 (Covox, #FB),
  3 (Centronics, #FB/#7B), 4 (external devices port, #FA), the #FE decode, and the port summary table.
- `doc/ver_7_10/Сборка и Наладка Турбо2+.doc` / `TURBO 2+ Assembly and Configuration Manual.doc`:
  "IOWR', IORD' (#FA) - read/write external ports (connectors INTERNAL and EXTERNAL I/O)" from D16/D17,
  and the CT0..CT7 bus shared by the printer, the external address bus and the DAC.
- `pcad/ver_7_10/cp7_1.pdf` (main sheet: D16/D36 decode, D87 IO buffer, D51 CT latch, D70/R39 BUSY,
  INTERNAL I/O table), `cp7_2.pdf` (parts list: R39 = 10 kΩ), `bom.pdf` (555АП6 = 74(LS,ALS)245 only).
  Rendered with PyMuPDF and read as images (open questions 1-3).
- `pcad/ver_8_0_zorel/АТМ Турбо 8.01+.pcb` (P-CAD ASCII, with a netlist): same reference designators as
  7.10, used to confirm every 7.10 net reading (D16, D36, D70, D87, D97, D51, R97, X3, I1).
- `pcad/ver_7_18/cp718pcb_T1.pcb`, `cp718pcb_T2.pcb`, `pcad/ver_7_10/ver7_10.PCB` (binary P-CAD):
  only the net names were checked (`~IORD`, `~IOWR`, `CTS0..7`, `IO0..7`).

Added 2026-10-02 for the open questions (HTTP 200 with curl unless noted):

- Card PCB, Rev 2 gerbers: [ATM2IOESPv2.zip](https://raw.githubusercontent.com/Kulicheg/ATM2IOESP/main/PCB/ATM2IOESPv2.zip)
  (the `github.com/.../blob/...` page answered 503 to curl). Last changed in
  [commit 8ecb00e](https://github.com/Kulicheg/ATM2IOESP/commit/8ecb00e), 2025-05-24. Rendered and
  net-traced (open questions 4, 5). The resistor values come from
  [pcb_rev02.PNG](https://raw.githubusercontent.com/Kulicheg/ATM2IOESP/main/PCB/pcb_rev02.PNG).
- MicroArt manual "Многофункциональный компьютер ATM Turbo" (1991) on ZXPRESS:
  [book index](https://zxpress.ru/book.php?id=170),
  [D16 decoder description](https://zxpress.ru/ru/books/chapter/2349),
  [connectors: "Внешний порт"](https://zxpress.ru/ru/books/chapter/2353),
  [appendix 2 port table](https://zxpress.ru/ru/books/chapter/2356),
  [Ver 4.50 schematic scan](https://zxpress.ru/chapters_images/atmturbo-6.jpg) (open question 6).
- UniProg on ATM: [zx-pk.ru thread 14779, page 3](https://zx-pk.ru/threads/14779-programmator-uniprog-(by-microart)/page3.html)
  and [page 4](https://zx-pk.ru/threads/14779-programmator-uniprog-(by-microart)/page4.html) (open question 7).
- TASiTERM: [description](http://atmturbo.nedopc.com/download/isdos/tasiterm/tasiterm.htm),
  [archive ttrm0971.ipc](http://atmturbo.nedopc.com/download/isdos/tasiterm/ttrm0971.ipc),
  [iS-DOS/TASiS download page](http://atmturbo.nedopc.com/load_isdos.htm),
  [ATM news (v0.97, v0.971 entries)](http://atmturbo.nedopc.com/atmnews.htm) (open question 8).

## Other emulators

| Emulator | ATM2IOESP | #FA on ATM 7.10 | #FE decode |
|---|---|---|---|
| Unreal Speccy (incl. the TS-Labs zx-evo-unreal fork) | no | not decoded. A read falls to #FE (`io.cpp:444 pFE = !(port & 1)`). Covox on any write with A2=0 (`io.cpp:631`) | A0 only |
| ZXMAK2 | no | not decoded (`MemoryAtm710.cs:63` subscribes #FE reads on mask 0x0001) | A0 only |
| Xpeccy | no | decoded and **ignored** (`atm2.c:227-228`: `{0x0007,0x00fe,...}`, `{0x0007,0x00fa,...,NULL,NULL}`) | A2..A0 = 110 |
| MAME (`atm.cpp`) | no | not mapped. #FB = Centronics latch (`atm.cpp:380`) | low byte `1111x110` (`atm.cpp:379`) |
| ZEsarUX, Fuse | no ATM Turbo 2+ machine [inferred, not checked in source] | - | - |
| zx-evo-unreal `zifi32` | ZiFi / ESP32 on ZX-Evo ports, not ATM INTERNAL I/O | - | - |

No emulator models the INTERNAL I/O bus or any card on it. Our model will be the first. The
reference for behavior is the hardware documentation, not another emulator.

## Proposed emulation

### 1. Fix the ATM710 #FE decode first

`PortDecoder_ATM710::IsPort_FE` (`core/src/emulator/ports/models/portdecoder_atm710.cpp`) tests
`(port & 0x0001) == 0`. The ATM documentation (#FE = A0=0, A1=A2=1), Xpeccy (mask 0x0007 = 0x0006) and
MAME (stricter still) agree on A2..A0 = 110. The A0-only decode sends `IN A,(#FA)` to the keyboard
controller: the 8031 gets an interrupt, the high byte becomes a command, the Z80 waits. It sends
`OUT (#FA),x` to the border and beeper. On the real board neither happens. Change it to
`(port & 0x0007) == 0x0006` and check the existing ATM suites. Even ports other than A2..A0 = 110 stop
being keyboard reads, as on the real machine. #7FFD, #FFFD and #BFFD already carry their own masks.

### 2. Model the INTERNAL I/O bus on the mainboard, then the card on it

The #FB latch and the #FA strobes belong to the ATM board, not to the card, so model them in the
ATM710 decoder:

- `#FB` write (A2..A0 = 011): store `ctBus = value` (motherboard state, saved in TTD). Also forward it
  to the Covox DAC, which is driven by the same latch, and to the printer if one is ever emulated.
  A7=0 is the printer strobe.
- `#FB` read: printer status. #FF with no printer (pull-up R39 on `CTBUSY'`, D0..D6 undriven; open
  question 3).
- `#FA` read/write (A2..A0 = 010): offer `(ctBus, data)` to the devices on the INTERNAL I/O bus. The
  first device whose address matches drives the read. With no match, the read returns #FF (open
  question 1).
- Machine reset: pulse RS to every bus device. ATM2IOESP: UART MR.

The bus takes a small device interface: `bool Matches(uint8_t ctBus)`, `uint8_t Read(uint8_t ctBus)`,
`void Write(uint8_t ctBus, uint8_t value)`, `void Reset()`. This leaves room for UniProg or Z-Contact
later. It is the "machine declares buses, devices plug in" direction, with INTERNAL I/O as a second
ATM bus beside the ZX-Bus.

Alternative: register low bytes #FA/#FB as full-decode ports, the way `ComPort` claims #xxEF. This is
rejected. It would steal the Covox write on #FB, miss the A2..A0 mirrors, and put a mainboard latch
inside a card.

### 3. The ATM2IOESP device

- Address match: `(ctBus & 0xF8) == base`, with `base = (jumpers << 4) | (rev10 ? 0x08 : 0x00)`. The
  register is `ctBus & 7`.
- UART: `Uart16550` with `Flavor::Chip16550`, `uartClockHz = 1843200`, `mcrMask = 0x3F` (AFE exists on
  a genuine TL16C550C), 16-byte FIFOs. `InterruptActive()` is not wired anywhere. No access wait.
  Time is base T-states, as `ComPort::Now()`.
- Peer: the same `ComPortSpec` peers (`ESPNET`, `AT`, `TCP:`, `SERIAL:`, `LOOPBACK`). The default is
  `AT` at 115200, matching the shipped module: AT firmware with `AT+UART=115200,8,1,0,3`. `ESPNET` is
  for the ESPNET kernel. The module is an ESP32 (`EspChip=ESP32`).
- `ComPort` is hard-wired to #xxEF and `A10..A8`. Either make it take an access adapter, or write a
  small `Atm2IoEsp` class that owns a `Uart16550` and a peer and reuses `ComPort::SavePeer/LoadPeer`
  for TTD.

### 4. Configuration and NetworkManager

```ini
[NETWORK]
Card=ATM2IOESP              ; may be combined: ZXWIFI,ATM2IOESP / ZXNETUSB,ATM2IOESP
Atm2IoEsp=AT                ; peer spec, as ZxWifi= (AT | ESPNET | TCP:... | SERIAL:... | LOOPBACK)
Atm2IoEspAddress=0xF0       ; bus base: 0xF0 = Rev 1.5/2.0 default, 0xF8 = Rev 1.0; must be a multiple of 8
```

- `networkspec`: add `kCardAtm2IoEsp = 0x04` and the name `ATM2IOESP` (alias `ATM2-IO-ESP`).
- `PortDecoder::NetworkCapabilities`: add `internalIo` (true for ATM710 only, for now). `MakePlan`
  fits the card only there, and otherwise adds the note "ATM2IOESP: the machine has no ATM INTERNAL
  I/O connector". This check is independent of `caps.zxBus`.
- Coexistence:
  - **ATM2 COM** (keyboard controller RS-232, `ComPort=`, `plan.machineSerial`) is a different port
    and a different peer. Both can be fitted.
  - **ZX-WiFi** (#F8EF..#FFEF on the ZX-Bus) is a different port. Both can be fitted if the ATM decode
    leaves #xxEF free. Note that ATM 7.10's own IDE uses #FEEF/#FFEF, so `ReservesLowByte(0xEF)` may
    already refuse ZX-WiFi on this machine.
  - All three are separate `ISerialPeer`s on the one virtual network.
  - `NetworkManager` today has a single `_com` (#xxEF). Add an `_atmIo` slot for the card and include
    its peer in `ComPort::SerialNetGuests`, so its sockets show in network status and debugging.
- Status and automation: per the automation-parity rule, every surface reports `cards` with
  `ATM2IOESP`, the base address, the UART view (`Uart16550::View`) and the peer kind. The settings
  `atm2ioesp` / `atm2ioesp_address` go through the same `SetSettings` path as `zx_wifi`.
- TTD: the #FB latch (mainboard), the card's `Uart16550::State`, and its peer (`netstate::Com`). This
  is a format change, so re-record the TTD fixtures afterwards.

### 5. What to verify by tests

1. Decode: `IN A,(#FA)` with the card absent returns #FF. It does not touch the keyboard controller:
   no KBC command, no wait. `OUT (#FA),x` leaves the border and beeper alone. #FE still works on
   #xxFE and its A2..A0 = 110 mirrors.
2. #FB write: the latch reaches the Covox DAC (sample value) and the INTERNAL I/O bus. `IN (#FB)`
   does not return the latch.
3. Address match: base #F0 answers #F0..#F7 and ignores #F8..#FF and #00..#07. Rev 1.0 base #F8 is the
   reverse. Default NedoOS ini values with comType 3 (ESPNET's `esp_reg8` gives indices 0..7) select
   nothing, as on hardware.
4. Register file through #FB/#FA: DLAB divisor, LSR.THRE after a THR write, SCR round-trip, IER = 0
   with no INT effect, FCR #87 resets the FIFOs.
5. Baud: divisor 1 at 1.8432 MHz is 115200, and one character takes 10 bits in T-states at 3.5 and
   7 MHz.
6. RTS pulse flow (MCR 2 then 0, AFE off): with an ESP peer that honors RTS, at least one byte arrives
   per pulse and nothing is lost when the program stops pulsing. This is the `esp_fill3` /
   `uartReadBlock` pattern.
7. Machine reset clears the UART (MR) but not the peer's link.
8. TTD round-trip with the latch and the UART mid-transfer.
9. End to end: NedoOS (ATM2 kernel built with ESPNET) with `espcom.ini` comType 3, addresses
   0xF0..0xF7, divider 1. `espcfg` shows "ATM2IOESP" and an `ESPNET` INFO succeeds. Then `getpic` /
   `gopher` with `network.ini currentNetwork=1` over an `AT` peer.

## Open questions

Status after the 2026-10-02 research pass. Schematic evidence comes from the NedoPC atmturbo SVN
(`pcad/ver_7_10/cp7_1.pdf`, rendered and cropped; the machine-readable netlist of the Zorel 8.0+
derivative, `pcad/ver_8_0_zorel/АТМ Турбо 8.01+.pcb`, which keeps the 7.10 reference designators and
was used to confirm each 7.10 reading). Card evidence comes from the Rev 2 gerbers
(`PCB/ATM2IOESPv2.zip`, last changed 2025-05-24), rendered and traced for copper connectivity
(top + bottom + drill), with the TL16C550C FN (PLCC-44) pin numbers from the datasheet in the repo.

1. **Unselected #FA read - answered: #FF.** The INTERNAL I/O data lines IO0..IO7 reach the board
   only through **D87 (555АП6 = 74LS245)**: pin 19 (OE) is tied to GND, so the buffer is always on,
   and pin 1 (direction, `T`) is driven by `IORD'` (cp7_1, the D87 block next to the INTERNAL I/O
   table; 8.0+ netlist: `D87.1 = ~IORD`, `D87.19 = GND`, `D87.11..18 = IO7..IO0`). During
   `IN (#FA)` the B side (IO0..IO7) becomes the input. The IO nets have no pull-up or pull-down
   (8.0+ nets IO0..IO7 contain only X3, I1 and D87). The 7.10 BOM lists D34, D87, D97, D102 as
   "74(LS,ALS)245" only, unlike most other parts it allows as HCT/ACT, so the open inputs are TTL
   inputs and read as 1. The CPU sees the internal D bus through **D97** (also 74LS245, direction
   `RD'`; netlist `D97.1 = ~RD`, A side Z0..Z7 at the Z80 with 10 kΩ RS1 pull-ups). With no card
   driving IO0..IO7, `IN (#FA)` returns **#FF**. The same holds when a card is fitted but its
   address does not match (the TL16C550C data pins are tri-state while not selected). The
   "buffered data bus (ID0-ID7)" wording in the assembly manual (`Сборка и Наладка Турбо2+.doc`,
   connector I1 paragraph) agrees. Caveat: this is the standard behavior of a floating LS-TTL
   input, not a measurement on a real board.
2. **Full #FA/#FB decode - answered: A2..A0 with A1 = 1, A3..A15 ignored.** The ports come from
   **D16 (555ИД7 = 74LS138)**. Select inputs: A (pin 1) = `A0`, B (pin 2) = `A2`, C (pin 3) =
   `WR'`. Enables: G2A' (pin 4) = `A1` through the inverter **D36** (pins 1→2), G2B' (pin 5) =
   `IORQ'`, G1 (pin 6) = `M1'`. Outputs: Y0 `IOWR'` (#FA write), Y1 `PRWR'` (#FB write), Y2
   `BRDWR'` (#FE write), Y3 `FFWR'`, Y4 `IORD'` (#FA read), Y5 `PRRD'` (#FB read), Y6 `KEYRD'` (#FE
   read), Y7 `FFRD'`. Read from the cp7_1 drawing and confirmed net by net in the 8.0+ netlist
   (`D16.1 = A0`, `D16.2 = A2`, `D16.3 = ~WR`, `D16.4 = D36.2` with `D36.1 = A1`,
   `D16.5 = ~IRQ` [the IORQ net], `D16.6 = ~M1`). No A3..A7 term exists anywhere in the chain, so
   Xpeccy's mask `0x0007` is right and MAME's full-low-byte #FB decode is stricter than the
   hardware. `M1'` on G1 also keeps interrupt-acknowledge cycles off these ports. The doc's
   `%nnnnnnnn11111010` pattern (`Описание архитектуры и портов ATM2+.doc`, section 4) names the
   canonical port, not a full decode.
3. **#FB read with no printer - answered: #FF.** `PRRD'` enables one gate of **D70 (555ЛП8 =
   74LS125)** whose input is `CTBUSY'` and whose output is D7 (cp7_1: D70 pins 4/5/6; netlist
   `D70.4 = ~PRRD`, `D70.5 = ~CTBUSY`, `D70.6 = D7`). `CTBUSY'` has a pull-up to +5 V: **R39**
   on 7.10 (10 kΩ per the cp7_2 parts list), R97 on 8.0+. Nothing drives D0..D6, so they float
   high through D97. With no printer the read is **#FF**. The ATM2+ port doc says the same in
   words: D0-D6 "Всегда установлены в 1", and D7 = 1 means the printer is not ready, "или просто
   отключен" (section on #FB input). `CTBUSY'` exists only on the Centronics/EXTERNAL I/O connector
   X3, not on INTERNAL I/O, so the card can never affect a #FB read.
4. **UART CS wiring - answered.** TL16C550C **CS0 (pin 14) and CS1 (pin 15) are tied to +5 V**
   (same copper net as VCC pin 44). **CS2' (pin 16) is the GAL's pin 15** (`!CS2`), silk "CS".
   The GAL's pin 13/14 outputs (`!CS0`/`!CS1`) are intermediate terms and reach nothing. The
   other control pins: **ADS' (28) = GND** (address latch transparent), **RD2 (25) and WR2 (21) =
   GND** (inactive), **RD1' (24) = connector `RD` (IORD')**, **WR1' (20) = connector `WR`
   (IOWR')**, **MR (39) = GAL pin 12**. **A0/A1/A2 (31/30/29) = CT0/CT1/CT2** of the connector,
   which turns the earlier [inferred] into a traced fact. The behavior matches the GAL equations
   above.
5. **INTRPT and RTS/CTS - answered.** **INTRPT (pin 33) has no copper connection**, nor do RXRDY
   (32), TXRDY (27), DDIS (26), OUT1 (38), OUT2 (35), DTR (37) and BAUDOUT/RCLK beyond their
   mutual link (17-10). Serial nets, each through a series resistor (values from the Rev 2
   silkscreen in `pcb_rev02.PNG`):
   - UART SOUT (13) → 470 Ω → ESP pin "RX";
   - ESP "TX" → 47 Ω → UART SIN (11);
   - **UART RTS' (36) → 47 Ω → ESP pin "CTS"**;
   - **ESP pin "RTS" → 470 Ω → UART CTS' (40)**.
   The modem inputs are strapped: **DCD' (42) and DSR' (41) to GND** (always asserted), **RI'
   (43) to +5 V** (never ringing).
6. **Other ATM boards - partly answered.**
   - **ATM Turbo 2+ 7.18 (Tetroid):** the binary P-CAD boards `pcad/ver_7_18/cp718pcb_T1.pcb` and
     `T2.pcb` carry the same net names (`~IORD`, `~IOWR`, `CTS0..CTS7`, `IO0..IO7`) as
     `ver7_10.PCB`. The bus is present. The binary netlist was not parsed further.
   - **ATM Turbo 8.0+/8.01+ (Zorel):** the ASCII netlist shows D16, D36, D51, D70, D87 and both
     24-pin connectors wired exactly as on 7.10. Connector I1 (INTERNAL I/O) pins: 1 +12 V,
     2 `IOWR'`, 3 +5 V, 4 -12 V, 5/7 GND, 6 `IORD'`, 8..23 CTS7/IO7 ... CTS0/IO0 interleaved,
     24 `RS'`. X3 (EXTERNAL I/O / Centronics) is the same bus with `STROBE'` and `CTBUSY'` in
     place of ±12 V. The card works there unchanged.
   - **ATM Turbo (1) v4.50 (our `ATM450`):** the bus exists, under another name and with another
     decode. The MicroArt manual calls the connector the **"External port"** ("Внешний порт",
     ОНП-24): GND, +5 V, +12 V, D0-D7, "CTS0-CTS7 - адресное пространство внешней шины", IORD,
     IOWR, RS. MicroArt designed UniProg for it
     ([chapter "Периферия, ее подключение и разъемы"](https://zxpress.ru/ru/books/chapter/2353)).
     The decoder is again D16 ("D16 - IOWR, IORD - запись, чтение внешних устройств",
     [chapter 2349](https://zxpress.ru/ru/books/chapter/2349)). Appendix 2 gives "EXTERNAL PORTS:
     IN & OUT WITH A2=A0=0" ([chapter 2356](https://zxpress.ru/ru/books/chapter/2356)). On the
     4.50 schematic scan ([atmturbo-6.jpg](https://zxpress.ru/chapters_images/atmturbo-6.jpg)),
     D16's V1/V2 appear joined and V3 goes to M1, with no A1 input. That suggests **A1 is not
     decoded on 4.50**, so #F8/#FA (and every A2=A0=0 port) strobe IOWR/IORD [inferred from a
     low-resolution scan]. Still open: the connector's pin order (the text lists no -12 V) and
     whether the 4.50 IO data lines are buffered as on 7.10.
7. **UniProg and "Analytic" addresses - partly answered.** "Analytic" is the company
   **Аналитик-ТС**, which co-developed the **Z-Contact 1200** V.22 modem with MicroArt. The modem
   mounts "вторым этажом" on I1 (assembly manual, I1 paragraph and the Z-Contact advertisement in
   `Сборка и Наладка Турбо2+.doc`). So "compatible with UniProg and Analytic" means the UniProg
   programmer and the Z-Contact modem. NedoPC's Maksagor reports that the UniProg ZX software works
   on ATM "через его порт внешних устройств #FFFA" and the printer port
   ([zx-pk.ru thread 14779, page 4](https://zx-pk.ru/threads/14779-programmator-uniprog-(by-microart)/page4.html);
   see also [page 3](https://zx-pk.ru/threads/14779-programmator-uniprog-(by-microart)/page3.html)).
   **The CT bus addresses that UniProg and Z-Contact decode were not found.** The card's own
   manual (appendix G) only implies that UniProg sits in the #F8..#FF range, because Rev 1.5 left
   it "для совместимости с UniProg". The UniProg 1.2 article ("Радиолюбитель" No. 9, 1993) and the
   Z-Contact manual were not available.
8. **TASiS `tasiterm.com` - partly answered.** TASiTERM is by Maksagor (NedoPC). v0.971
   (31.12.2024) "внесен экспериментальный драйвер под разъем внешних IO-портов АТМ", a driver that
   "still awaits its testers" ([ATM news page](http://atmturbo.nedopc.com/atmnews.htm), New Year
   2025 entry). Drivers are separate files: the user renames the chosen one to `tasiterm.drv`, and
   settings live in `tasiterm.set` (same page, v0.97 entry). NedoOS's `espcom.ini` comType does
   not apply to it. The archive
   ([ttrm0971.ipc](http://atmturbo.nedopc.com/download/isdos/tasiterm/ttrm0971.ipc)) is
   iS-DOS-packed and could not be unpacked here, so **the bus addresses its driver uses remain
   open**. A TASiS + TASiTERM run in the emulator, with an IN/OUT trace on #FA/#FB, would answer it.

### Impact on the emulation

- **Unselected #FA read = #FF.** This confirms §2 of the proposal and drops the [inferred]. The
  value is the same with no card, with a card at another address, and for every A2..A0 = 010 port.
- **Decode on ATM710 = A2..A0 only, A1 = 1 required, M1 cycles excluded:** #FA = `(port & 7) == 2`,
  #FB = `(port & 7) == 3`, #FE = `(port & 7) == 6`, #FF = `(port & 7) == 7`. A3..A15 are don't-care.
  Do not copy MAME's full-low-byte #FB decode.
- **#FB read = #FF** with no printer. This drops the [inferred] in §2 of the proposal.
- **Card MSR:** DCD and DSR are strapped asserted and RI deasserted, so MSR bits 7/5 read 1 and
  bit 6 reads 0, with only CTS (bit 4, from the ESP's RTS) changing. Model these as fixed modem
  inputs of the card's `Uart16550` and do not take them from the peer. Delta bits other than ΔCTS
  never set.
- **No interrupt and no outputs:** INTRPT, OUT1, OUT2 and DTR go nowhere. IER and those MCR bits
  are register state only.
- **Flow control wiring:** UART RTS' → ESP CTS and ESP RTS → UART CTS'. An `AT` peer modeling
  `AT+UART=...,3` must hold its transmission while the UART's MCR RTS is off and must report its
  own receive readiness as the UART's CTS.
- **ATM450:** if the card is ever offered there, the external port decodes on A2 = A0 = 0
  (A1 ignored) [inferred], and connector compatibility is unverified. Keep `internalIo = false`
  for ATM450 until the 4.50 connector pinout is confirmed.
