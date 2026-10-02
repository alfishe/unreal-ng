# Sprinter network adapters: research

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Status** | Research for the design in [tdd.md](tdd.md); owner decisions still open: [open-questions.md](open-questions.md) |
| **Machine** | Peters Plus Sprinter Sp2000 (`SPRINTER` model in unreal-ng) |
| **Builds on** | the ISA slot design [2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/research.md) (bus, window 3, `#9FBD`, PIO port B interrupts) and the network stack of [2026-09-30-nedoos-integration](../2026-09-30-nedoos-integration/tdd-network.md) |
| **Materials** | downloaded copies are kept outside the repository, in the owner's local Sprinter collection (`network/` folder with its own `INDEX.txt`); this document cites the upstream links |

## Contents

1. [In short](#1-in-short)
2. [Glossary](#2-glossary)
3. [Sources](#3-sources)
4. [How a Sprinter program reaches a network card](#4-how-a-sprinter-program-reaches-a-network-card)
5. [The adapters, one by one](#5-the-adapters-one-by-one)
6. [Software](#6-software)
7. [Development activity](#7-development-activity)
8. [Other emulators](#8-other-emulators)
9. [What unreal-ng already has](#9-what-unreal-ng-already-has)
10. [Corrections to earlier documents](#10-corrections-to-earlier-documents)

## 1. In short

- **The Sprinter has no network hardware on the main board.** Both serial channels of the Z84C15 CPU are taken:
  channel A by the keyboard, channel B by the serial mouse ("These ports are not standard RS-232", Peters Plus
  FAQ). Every network adapter is an **ISA-8 card** in one of the two slots.
- **2026 is the busiest year the Sprinter network has ever had.** Dmitry Mikhalchenkov (`witchcraft2001`)
  published, between April and September 2026, three complete network kits for Estex DSS: one for
  **NE2000-class Ethernet (RTL8019AS)**, one for the **3Com 3C509B**, and one for the **SprinterESP Wi-Fi card**.
  All three offer the same programs (DHCP, ping, DNS, NTP, TFTP, wget, FTP, telnet with Zmodem) and a shared
  library interface ("UNET") so that one program, such as the Gopher browser, runs on any of them.
- **Ranking for emulation** (scoring in [tdd.md](tdd.md) §2):

| Adapter | Bus / chip | Users | Activity | Recommendation |
|---|---|---|---|---|
| **NE2000-class Ethernet** (RTL8019AS, UM9003 and clones) | ISA I/O `#200-#3E0`, DP8390 core | cheap second-hand PC cards; 4 board types verified on a real Sprinter; often asked about in the Sprinter Telegram group | kit 0.3.x, 104 commits, May-Sep 2026 | **build first** (owner decision 2026-10-02) |
| **SprinterESP Wi-Fi** (romych) with the **Sprinter ESP Network Kit** (`sprinter_wifi`, `UNETESP.DLL`) | ISA I/O `#3E8`, TL16C550C + ESP8266 (ESP-AT 2.2.2 / 2.2.1) | open hardware since 2022; ESPT and wterm on the 2025 DSS system disk | card 2022-2024; kit 0.2-0.3, 121 commits, Apr-Sep 2026 | **build second** (cheapest: every chip already in unreal-ng; owner: must be supported) |
| **3Com 3C509B** EtherLink III | ISA I/O by ID port `#110` | 3 boards verified | kit 0.1.x, Aug-Sep 2026 | build third (same apps, new chip) |
| ISA Hayes modem (period cards) | 16450 / 16550 at `#3F8/#2F8/#3E8/#2E8` | the reason ISA was put on the board (2000); BC-Term 1.11 on the Peters CD and in the 2025 disk set | frozen since 2002 | build as a preset of the UART card plus a generic Hayes modem peer |
| SprinterSerial (romych) | PC16552D dual UART at `#3F8` (USB) and `#2F8` (RS-232) | open hardware, 2022 | finished 2022 | preset of the UART card |
| SprinterNet (Shaos) | ISA, WIZnet W5100 | 3 prototypes, none released | stalled since 2024 | not planned (no released hardware) |
| ZX-bus network cards through the ZX-bus adapter (ZXNETUSB / W5300, Spectranet) | ZX-bus | no Sprinter software | - | falls out of PLAN #82 (bus unification); not a Sprinter phase |

- **unreal-ng has most of the parts.** The 16550 UART, the ESP AT module, the virtual network with its host
  bridge and TTD journal, the network slot settings and the Qt Network window all exist (they serve the ZX-Evo,
  ATM Turbo 2+ and ZXNETUSB). Missing: the ISA bus itself (ISA phase I1), the ISA interrupt lines (I4), a
  DP8390 / NE2000 chip, a 3C509B chip, and a way for a card that speaks **Ethernet frames** (not sockets) to use the
  virtual network: an Ethernet gateway, the subject of most of [tdd.md](tdd.md).

## 2. Glossary

| Term | Meaning |
|---|---|
| **ISA, ISA-8** | The expansion bus of the IBM PC / XT; the Sprinter has two 8-bit slots. Details: [ISA research](../2026-10-02-sprinter-isa/research.md) §2 |
| **ISA I/O address** | The address a PC program uses with `IN` / `OUT`. On the Sprinter a program maps the slot's I/O space into window 3 and reads memory: I/O address `#300` is CPU address `#C300` |
| **NIC** | Network interface card |
| **Ethernet frame** | The unit an Ethernet card sends and receives: destination MAC, source MAC, a type (`#0800` IPv4, `#0806` ARP), up to 1 500 data bytes, then a checksum the card adds or checks |
| **MAC address** | A card's 6-byte hardware address, stored in its PROM or EEPROM |
| **NE2000, NE1000** | Novell's reference Ethernet cards (16-bit and 8-bit); "NE2000-compatible" became the standard register interface for cheap ISA network cards |
| **DP8390** | National Semiconductor's Ethernet controller inside the NE2000; every NE2000 clone copies its registers |
| **RTL8019AS** | Realtek's single-chip NE2000 clone (1990s-2000s, still made), with 16 KB on-chip packet memory, Plug and Play and a 93C46 configuration EEPROM; works in 8-bit slots |
| **UM9003** | UMC's NE2000 clone; it lacks the Realtek extensions |
| **Packet RAM, remote DMA** | The card keeps frames in its own memory. The CPU reads or writes that memory one byte at a time through the card's **data port** after setting a start address and a count: "remote DMA" (no PC DMA controller involved) |
| **Receive ring** | The part of packet RAM where the card stores incoming frames, between pages `PSTART` and `PSTOP` (256 bytes per page). The card writes at `CURR`; the program frees pages up to `BNRY` |
| **PROM** | A small read-only memory on an NE2000 card holding the MAC address; read through remote DMA from address 0 |
| **3C509B, EtherLink III** | 3Com's ISA Ethernet card (1994). Not NE2000-compatible: FIFOs instead of a ring, registers in "windows", and an **ID port** (`#110`) through which software finds and activates the card |
| **UART, 16550** | The PC serial port chip: 8 registers, 16-byte receive and transmit buffers (FIFOs). TL16C550C, PC16552D (two in one package), 16450 (no FIFO) are variants |
| **Baud divisor** | The 16550 divides its clock by 16 x divisor: 1.8432 MHz / 16 / 1 = 115 200 baud; SprinterESP uses 14.7456 MHz / 16 / 8 = 115 200 |
| **AFE** | "Automatic flow enable" (16750 / TL16C550C): the UART drives RTS and obeys CTS by itself |
| **OUT1, OUT2** | Two spare output lines of a 16550 (register MCR). PC cards use OUT2 to enable the interrupt; SprinterESP uses OUT1 to reset the ESP and OUT2 for its flash-mode pin |
| **ESP8266, ESP32** | Wi-Fi microcontrollers from Espressif. With the **AT firmware** they act as a modem: the host sends text commands (`AT+CWJAP="ssid","pw"`, `AT+CIPSTART="TCP","host",80`) over the serial line |
| **Hayes modem, AT commands** | The classic telephone modem command set: `ATDT 5551234` dials, `CONNECT 2400` answers, `+++` returns to command mode, `ATH` hangs up. Same "AT" prefix, different commands from the ESP |
| **IRQ** | Interrupt request from a card. On the Sprinter each slot has one IRQ line, read through the Z84C15 PIO port B (bit 0 = slot 1, bit 1 = slot 2) |
| **Polling** | The program asks the card in a loop instead of waiting for an interrupt. All three 2026 kits poll |
| **UNET** | The 2026 kits' shared program interface: a DSS loadable library (`UNETRTL.DLL`, `UNET509B.DLL`, `UNETESP.DLL`) chosen by the `NET` environment variable, with the same numbered calls (TCP, UDP, DNS, ping) for every card |
| **DSS, Estex DSS** | The Sprinter's disk operating system (MS-DOS-like, `.EXE` programs) |
| **DHCP, DNS, ARP, ICMP** | Network services: DHCP hands out an address; DNS turns a name into an address; ARP finds the MAC of an address on the local network; ICMP carries ping |
| **NAT, user-mode gateway** | A program that pretends to be a home router: it answers the guest's ARP, DHCP and DNS and turns the guest's TCP / UDP traffic into ordinary host sockets. QEMU's "slirp" is the best-known one |
| **TAP, pcap, bridged mode** | Ways to put raw Ethernet frames on the host's real network: a TAP device (Linux, BSD), the pcap / Npcap capture library, Apple's vmnet. They need administrator rights or drivers |
| **Virtual network** | unreal-ng's existing user-mode network (`VirtualNetwork`): a virtual router at `10.0.2.2`, DNS `10.0.2.3`, DHCP leases from `10.0.2.15`, socket-level, journaled for TTD |
| **TTD, journal** | unreal-ng's time-travel debugger. Every input from outside the machine is written to a journal so a replay is exact without the host |

## 3. Sources

All links checked on 2026-10-02 (HTTP 200 unless noted).

| Short name | What | Link |
|---|---|---|
| **RTLKIT** | Sprinter RTL8019AS Network Kit, Dmitry Mikhalchenkov, commits 2026-05-03 .. 2026-09-21 (`a0c53bc`), releases 0.2.56 / 0.3.8 | [witchcraft2001/sprinter-rtl8019a](https://github.com/witchcraft2001/sprinter-rtl8019a), [release 0.3.8](https://github.com/witchcraft2001/sprinter-rtl8019a/releases/tag/0.3.8) |
| **WIFIKIT** | Sprinter ESP Network Kit, Mikhalchenkov on Roman Boykov's code, 2026-04-28 .. 2026-09-20 (`909330f`), BSD-3 | [witchcraft2001/sprinter_wifi](https://github.com/witchcraft2001/sprinter_wifi) |
| **EL3KIT** | Sprinter 3C509B Network Kit, 2026-08-29 .. 2026-09-25 (`b70ed55`), BSD-3 | [witchcraft2001/sprinter-3C509B](https://github.com/witchcraft2001/sprinter-3C509B) |
| **UNET** | the shared library interface and prebuilt DLLs; assembler and C bindings | [unet_libs_core](https://github.com/witchcraft2001/unet_libs_core), [sprinter_unet_libs_asm](https://github.com/witchcraft2001/sprinter_unet_libs_asm), [unet_libs_c](https://github.com/witchcraft2001/unet_libs_c) |
| **GOPHER** | Gopher browser for the Sprinter (port of nihirash's Next browser), 2026-06-07 .. 2026-09-23; weather forecast over Gopher | [sprinter_gopher_browser](https://github.com/witchcraft2001/sprinter_gopher_browser), [sprinter-weather-forecast](https://github.com/witchcraft2001/sprinter-weather-forecast) |
| **ESPCARD** | SprinterESP ISA-8 Wi-Fi card, Roman Boykov (romych), rev 1.0.5, 2022-04-26 .. 2024-07-11, BSD-3 | [zxgit.org/romych/SprinterESP](https://zxgit.org/romych/SprinterESP), mirror [github.com/romychs/SprinterESP](https://github.com/romychs/SprinterESP) |
| **ESPKIT** | romych's DSS / FreeDOS ESP library and tools (wterm, espset, wtime, TFTP), 2023-03-07 .. 2025-05-19 | [zxgit.org/romych/ESPKit](https://zxgit.org/romych/ESPKit) |
| **SERCARD** | SprinterSerial ISA-8 dual UART card, romych, rev 1.1.1, 2022 | [zxgit.org/romych/SprinterSerial](https://zxgit.org/romych/SprinterSerial), mirror [github.com/romychs/SprinterSerial](https://github.com/romychs/SprinterSerial) |
| **SNET** | SprinterNet: Shaos's forum thread (2021-01 .. 2024-10), his network repository and the SprintEm emulator's `network.cpp` | [nedopc.org t=20283](http://www.nedopc.org/forum/viewtopic.php?t=20283), [gitlab.com/nedopc/net](https://gitlab.com/nedopc/net), [gitlab.com/nedopc/sprintem](https://gitlab.com/nedopc/sprintem) |
| **PP-WEB** | Peters Plus web site, archived: the Sprinter page, the "comtool" software list (Black Cat Terminal, 19.04.2002), the FAQ (serial ports) | [sprinter page 2003](https://web.archive.org/web/20030208004427/http://www.petersplus.com/sprinter/), [comtool list](https://web.archive.org/web/20030518080213/http://www.petersplus.com:80/sprinter/software.php?cmd=list&group=comtool), [FAQ 2002](https://web.archive.org/web/20020307071522/http://www.petersplus.com:80/sprinter/showfaq.htm?sec=faq) |
| **SC05** | Sinclair Club #05 Sprinter FAQ ("ISA ... to allow using ISA modems") | [zxpress.ru](https://zxpress.ru/en/ezines/sinclair-club/05/sprinter-is-a-universal-z80-based-computer-with-pld-architecture-this-faq-covers-specifications) |
| **DOC-RU** | doc.sprinter.ru: ISA interrupts on PIO port B; the mouse on SIO B | [isa-interrupts](https://doc.sprinter.ru/blocks/z84c15/isa-interrupts), [mouse](https://doc.sprinter.ru/blocks/z84c15/mouse) |
| **MAME** | upstream `sprinter.cpp` (two ISA-8 slots, any PC card), `dp8390.cpp` (DP8390 + RTL8019A), `ne1000.cpp`; PR #16206 "Realtek RTL8019AS ISA8 ethernet adapter" (opened 2026-09-20, open) | [sprinter.cpp](https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sinclair/sprinter.cpp), [dp8390.cpp](https://raw.githubusercontent.com/mamedev/mame/master/src/devices/machine/dp8390.cpp), [ne1000.cpp](https://raw.githubusercontent.com/mamedev/mame/master/src/devices/bus/isa/ne1000.cpp), [PR #16206](https://github.com/mamedev/mame/pull/16206) |
| **MAME-FORK** | Mikhalchenkov's MAME fork: branch `sprinter-isa8-rtl8019a` (SprinterESP, RTL8019AS, 3C509B, ISA COM card), branch `enable-isa8-net` (NE1000, pcap) | [witchcraft2001/mame_sprinter](https://github.com/witchcraft2001/mame_sprinter), [sprinter-isa8-rtl8019a](https://github.com/witchcraft2001/mame_sprinter/tree/sprinter-isa8-rtl8019a), [enable-isa8-net](https://github.com/witchcraft2001/mame_sprinter/tree/enable-isa8-net) |
| **DS-8019** | RTL8019AS datasheet (Realtek, 2005-08-26, 51 pages) | [cryptomuseum.com copy](https://www.cryptomuseum.com/covert/rec/diloger/files/RTL8019AS.pdf) |
| **DS-8390** | DP8390 in National's 1988 Data Communications / LAN / UARTs handbook | [bitsavers](http://www.bitsavers.org/components/national/_dataBooks/1988_National_Data_Communications_Local_Area_Networks_UARTs_Handbook.pdf) |
| **DS-550** | TL16C550C datasheet (AFE, OUT1 / OUT2) | [ti.com](https://www.ti.com/lit/ds/symlink/tl16c550c.pdf) |
| **EL3** | 3C509 references: Linux `3c509.c`, the EtherLink III ISA user guide, overview | [3c509.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/3com/3c509.c), [user guide 09-1310-000](https://archive.org/details/09-1310-000), [Wikipedia](https://en.wikipedia.org/wiki/3Com_3c509) |
| **NE-LINUX** | Linux NE1000 / NE2000 probe driver (PROM layout, reset port behavior on clones) | [ne.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/8390/ne.c) |
| **ESP-AT** | Espressif ESP-AT firmware and its ESP8266 2.2 documentation | [espressif/esp-at](https://github.com/espressif/esp-at), [ESP8266 AT 2.2 docs](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/) |
| **HOSTNET** | Host-side options for raw frames: libslirp, pcap, Npcap, vmnet, TUN/TAP | [libslirp](https://gitlab.freedesktop.org/slirp/libslirp), [tcpdump / libpcap](https://www.tcpdump.org/), [Npcap](https://npcap.com/), [vmnet](https://developer.apple.com/documentation/vmnet), [TUN/TAP](https://www.kernel.org/doc/html/latest/networking/tuntap.html), [lwIP](https://github.com/lwip-tcpip/lwip) |
| **DISK** | The owner's MAME-pack and ZXMAK2 hard disks and the Peters Plus CD (BC-Term 1.10 / 1.11, ESPT, wterm, ISACHK) | owner's local collection, not public |
| **COMMUNITY** | Sprinter Telegram group (a group: no public message preview), Discord, zx-pk forum section | [t.me/zx_sprinter](https://t.me/zx_sprinter), [zx-pk.ru Sprinter](https://zx-pk.ru/forums/121-sprinter.html), [nedopc forum f=60](http://www.nedopc.org/forum/viewforum.php?f=60) |

Not reachable: `sprinternet.io` (no answer). The Telegram group and Discord need an account; their content was not
searched (the owner's statement that NE2000 Ethernet is a frequent topic there is the source for that point).

## 4. How a Sprinter program reaches a network card

Every network program found uses the ISA window of [ISA research](../2026-10-02-sprinter-isa/research.md) §4:

| Step | Instruction | Effect |
|---|---|---|
| 1 | `#1FFD` <- `#11` | pages `#D0-#DF` in window 3 mean ISA |
| 2 | `OUT (#E2),#D4` (slot 1) or `#D6` (slot 2) | window 3 = the slot's ISA **I/O** space |
| 3 | `#9FBD` <- `#00` | ISA address bits A19-A14 = 0 (and AEN = 0, RESET = 0) |
| 4 | `LD A,(#C300)` | one ISA I/O read cycle at address `#300` |

**Worked example: the RTL8019AS kit reads the chip ID.** The card sits in slot 1 at I/O base `#300`. `IFUP` opens
the window as above, writes `#21` to `#C300` (command register: page 0, stop), then reads `#C30A` and `#C30B`.
A Realtek chip answers `#50` and `#70` (`'P'`, `'p'`); only then does the driver claim the card (RTLKIT
`src/include/rtl8019.inc`, README "Cards without the Realtek ID").

**ISA reset.** `#9FBD` bit 7 drives the slots' RESET line. ESPKIT and the Wi-Fi kit pulse it (`#C0`, about 1 ms,
`#00`); BIOS 2.13 pulses it at boot (`#FF`, then `#00`). On the real board the pulse reaches both slots.

**Interrupts.** Only BC-Term uses them: IM 2, PIO port B in bit-control mode, slot 1 on bit 0, slot 2 on bit 1
(BC-Term 1.11 disassembly; DOC-RU). All three 2026 kits, ESPT and wterm **poll**.

**Bus timing.** The RTL kit fixed "missed ISA bus cycles" on real hardware (commit `59e45fd`, 2026-09-19: page
switches and BNRY handling hardened). The Sprinter's ISA cycle in turbo is far shorter than a PC's (ISA research
§5), so some clone cards miss writes. The emulator models an ideal bus; the kit's hardening makes it work on both.

## 5. The adapters, one by one

### 5.1 NE2000-class Ethernet (RTL8019AS and clones)

| Item | Fact | Source |
|---|---|---|
| Chips | RTL8019AS (jumperless, 93C46 EEPROM, 16 KB packet RAM); UMC UM9003AF; any NE2000 core | RTLKIT README |
| Boards verified on a real Sprinter | Realtek P/N 142091-401 (RJ45 + BNC); **CUBIK x86 ISA LAN+USB** (current production); an RTL8019AS green combo card; UMC UM9003AF ver 1.0 | RTLKIT README, with photos |
| Bus | 8-bit ISA; DCR = `#48` (byte-wide transfers, normal mode, FIFO threshold 8 bytes) | RTLKIT `rtl8019.inc` `DCR_INIT` |
| I/O base | auto-scan of both slots and the 16 bases `#200..#3E0` (32-byte steps); default `#300`; pin with `RTL_HW=0/#300` | RTLKIT |
| Registers | 32 bytes: `+#00-#0F` DP8390 registers in 4 pages (CR bits 7-6), `+#10` data port (remote DMA), `+#1F` reset port | RTLKIT, DS-8390, DS-8019 |
| Packet RAM layout used | transmit buffer pages `#40-#45`, receive ring `#46` up to `PSTOP` (26 pages, about 6.6 KB) | RTLKIT `rtl8019.inc`, `memmap.inc` |
| Realtek extras used | ID bytes `'P' 'p'` on page 0 `#0A/#0B`; page 3 (9346CR, CONFIG0-4, medium) - only after the ID matched; `NICEEP` dumps the 93C46 EEPROM read-only | RTLKIT |
| Clone quirks | UM9003: reading the reset port `+#1F` stalls the ISA cycle and freezes the machine; page 3 mirrors page 1; undefined register bits float; selecting page 3 near a transmit lost 17-27 % of frames | RTLKIT README (measured 2026-09-17) |
| IRQ | `RTL_IRQ` is parsed but not used; the driver polls ISR | RTLKIT README |
| Who makes / sells | no Sprinter-specific board: users buy second-hand ISA cards; CUBIK boards are still produced for retro PCs | RTLKIT README |
| How many | every user with an ISA network card; four board types verified (inference: the most available Ethernet option) | - |

### 5.2 SprinterESP Wi-Fi card

| Item | Fact | Source |
|---|---|---|
| Card | ISA-8, fixed decode at `#3E8-#3EF` ("COM3", 74HC30 + 74HC27), rev 1.0.5 (schematic 2023-01-27) | ESPCARD |
| UART | TL16C550C, **14.7456 MHz** crystal, divisor 8 = 115 200 baud | ESPCARD, ESPKIT `esplib.asm` (`XIN_FREQ`) |
| Module | ESP-12F (ESP8266) behind a TXB0104 level shifter, **ESP-AT v2.2.1** firmware (shipped in `Docs/`; modules from AliExpress often come without it) | ESPCARD README, `Docs/ESP-module-flashing.pdf` |
| Wiring | UART RTS / CTS to the ESP (swapped on boards before v1.0.3; `Docs/rts-cts-fix.pdf`); **OUT1** (MCR bit 2) = ESP reset, **OUT2** (MCR bit 3) = ESP GPIO0 (flash mode); AFE (MCR bit 5) for hardware flow control; INTR to ISA IRQ3 (inference from the net names) | ESPCARD schematic, ESPKIT |
| Driver sequence (ESPT) | detect by IIR & `#3F` = 1; FCR `#81`, IER 0, DLAB + DLL 8, LCR 3, FCR `#83`; ESP reset: MCR 6, 200 ms, MCR 2, wait for a byte, 700 ms, MCR `#22` | ESPT disassembly |
| Card detection (ESPKIT) | IER high nibble 0 plus the scratch register `#55` / `#AA` test, both slots | ESPKIT `isa.asm`, `esplib.asm` |
| IRQ | never enabled (IER = 0); polling | all ESP software |
| Who makes | open hardware (Gerbers, BOM); built by community members; no shop, no build count found | ESPCARD |
| AT commands used by the 2026 kit | `CIPMUX`, `UART_CUR`, `CWDHCP`, `CIPCLOSE`, `CWJAP`, `CIPSTA`, `SLEEP`, `CWMODE`, `CIPSTART`, `GMR`, `CWLAPOPT`, `CWLAP`, `SYSSTORE`, `CIPSEND`, `PING`, `CIPSTATUS`, `CIPSNTPCFG`, `CIPSERVER`, `CIPRECVMODE`, `CIPMODE`, `CIPDOMAIN`, `SYSLOG`, `CIPTCPOPT`, `CIPSTO`, `CIPSNTPTIME`, `CIPSERVERMAXCONN`, `CIPRECVDATA`, `CIPDNS`, `CIPDINFO`, `CIFSR` | WIFIKIT, ESPKIT sources (string scan) |

### 5.2a The Sprinter ESP Network Kit (`sprinter_wifi`) in detail

The kit drives the SprinterESP card of §5.2 and nothing else: no Z84C15 SIO, no own registers. Source:
[witchcraft2001/sprinter_wifi](https://github.com/witchcraft2001/sprinter_wifi) at `909330f` (2026-09-20; package
0.3.0, DLL 0.3.3 on `main`; tag [0.2.1](https://github.com/witchcraft2001/sprinter_wifi/releases/tag/0.2.1) from
2026-07-29), BSD-3, 121 commits since 2026-04-28. The driver library (`src/lib/esplib.asm`, `isa.asm`, `esp_tcp.asm`,
`esp_tcp_multi.asm`, `esp_udp.asm`) is Roman Boykov's ESPKit code, extended.

| Item | Fact | Where |
|---|---|---|
| Bus and address | ISA I/O `#3E8` ("COM3") in slot 1 or 2 through window 3: registers at CPU `#C3E8-#C3EF` (`PORT_UART_A = ISA_BASE_A + #3E8`); slot found by the ESPKit probe | `esplib.asm:15-32` |
| UART | TL16C550, 14.7456 MHz, 8N1; divisor 8 = 115 200 baud by default; `BAUD` in `NET.CFG` may select 230 400, 115 200, 57 600, 38 400, 19 200 or 9 600 after `NETUP` set the ESP with `AT+UART_CUR` | `esplib.asm:69-71`, README |
| FIFO | FIFO on, receive trigger level **4 bytes** (`FCR_TR4`) for firmware 2.2.2 | `esplib.asm:50-58` |
| Flow control | **manual RTS by default**: MCR = `RTS` (`#02`) keeps the ESP sending; `UART_RX_PAUSE` writes MCR = 0 (RTS off) around slow consumer paths, `UART_RX_RESUME` restores it. **AFE** (MCR `#22`: automatic RTS at the trigger level and CTS-gated transmit) is switched on only after `NETUP` set the ESP to `flow=3` with `AT+UART_CUR` **and** an AT round trip still worked ("some ESP-AT builds accept flow=3 while the pins are not muxed, and AFE then deadlocks TX"). The result is published as `NET_ESP_FLOW=3` / `0` and reused by every client | `esplib.asm:255-345`, README |
| ESP reset | MCR = `RST` (`#04`, OUT1 -> the ESP's reset pin low), then MCR = `RTS`: reset released in manual RTS mode | `esplib.asm:689-693` |
| Firmware | ESP8266 ESP-AT **V2.2.2.0**, a custom build shipped in the repository (`firmware/SprinterESP-AT-v2.2.2.0-runtime-flow-fix-full-2MB.bin`, flashing guide `firmware/FLASHING.md`); a compatibility path for **V2.2.1** (no `AT+CIPRECVMODE` / `AT+CIPRECVDATA`, so active `+IPD` receive only) | README "ESP-AT Firmware Baseline" |
| Firmware detection | `NETUP` sends `AT+SYSSTORE?` once: `ERROR` = 2.2.1 profile (`_CUR` commands), `OK` = 2.2.2 (then `AT+SYSSTORE=0`, settings session-only); published as `NET_ESP_FW=2.2.1` / `2.2.2` | README |
| Connections | `AT+CIPMUX=1`: two channels at once (passive FTP: control + data), link-id `AT+CIPSTART` / `AT+CIPSEND`, `+IPD,<link>,<len>:` frames; a 2 KB defer buffer keeps `+IPD` data that arrives inside a `CIPSEND` exchange | `unetesp.asm:40-52`, `esp_tcp_multi.asm` |
| Receive | 2.2.2: passive receive (`AT+CIPRECVMODE=1`, `AT+CIPRECVDATA=<link>,<len>`) in the DLL; active `+IPD` path for 2.2.1 and the stand-alone programs | `unetesp.asm`, README |
| AT commands the kit sends | `AT`, `ATE0`, `GMR`, `RST`, `SYSSTORE`, `UART_CUR`, `CWMODE`, `CWJAP`, `CWLAP`, `CWLAPOPT`, `CWDHCP`, `CIPSTA`, `CIPDNS`, `CIFSR`, `SLEEP`, `SYSLOG`, `CIPMUX`, `CIPSTART`, `CIPSEND`, `CIPCLOSE` (incl. `=5`, all), `CIPSTATUS`, `CIPSERVER`, `CIPSERVERMAXCONN`, `CIPSTO`, `CIPTCPOPT`, `CIPRECVMODE`, `CIPRECVDATA`, `CIPMODE`, `CIPDINFO`, `CIPDOMAIN`, `PING`, `CIPSNTPCFG`, `CIPSNTPTIME` | string scan of `src/` |
| Interrupts | none: polling (IER = 0) | `esplib.asm` |
| DLL interface | `UNETESP.DLL` (libman 1.3, L1 format, 24-entry jump table, ABI `#0100`): `GETCAPS`, `NETINIT`, `NETDONE`, `CONNECT`, `SEND`, `RECV`, `CLOSE`, `STATUS`, `UDPOPEN`, `RESOLVE`, `PING`, `RXPAUSE`, `RXRESUME`, `GETINFO`, `LASTERR`, `SETOPT`, `LISTEN`, `UNLISTEN`; capabilities TCP, UDP, RESOLVE, PING, RXFLOW, MULTICHAN, ASYNCSEND, LISTEN; the DLL refuses to start unless `NETUP` found firmware 2.2.2 | `src/include/unet.inc`, `docs/UNETAPI.md` |
| Programs | `NETPROBE`, `NETRESET`, `NETCFG`, `NETUP`, `TCPTEST`, `UDPTEST`, `TFTP`, `FTP`, `PING`, `WGET`, `NTP`, `WTERM`, `TELNET`, `UNETTEST` (DLL test), `DLSPEED`, `RACETEST`; examples `WGETCERN.BAT`, `FTPLIST.BAT`, `TFTPGET.BAT`, `UDPECHO.BAT`; C (SDCC) and Turbo Pascal bindings | `src/apps/`, `examples/`, `bindings/` |
| DLL consumers | the [Gopher browser](https://github.com/witchcraft2001/sprinter_gopher_browser) (ESP default), the [weather forecast](https://github.com/witchcraft2001/sprinter-weather-forecast), `UNETTEST`; network games are being designed on it (`docs/UNETRTL-GAMES-FR.md`: listen, asynchronous send) | as linked |

### 5.3 3Com 3C509B EtherLink III

| Item | Fact | Source |
|---|---|---|
| Boards verified | 3C509B-TPO (two assemblies), 3C509B-TP; 10BASE-T only | EL3KIT README |
| Discovery | the ISA **ID port `#110`**: an ID sequence, then EEPROM reads through the ID port; checks product ID (`#9550` / `#9050`), manufacturer `#6D50`, a unicast MAC and both EEPROM checksums; then the card is activated at its I/O base | EL3KIT README, EL3 |
| Model | 16-byte register block in 8 windows, transmit and receive FIFOs (no packet ring), status / command register at `+#0E` | EL3 |
| IRQ | none; "no IRQ setting anywhere in the kit" | EL3KIT README |
| Software | the same set as the RTL kit (`EL3INFO`, `NETCFG`, `IFUP`, `PING`, `NSLOOKUP`, `NTP`, `TFTP`, `WGET`, `FTP`, `TELNET`) and `UNET509B.DLL` | EL3KIT |

### 5.4 ISA Hayes modems

| Item | Fact | Source |
|---|---|---|
| Why ISA exists on the Sprinter | "the Sprinter needs a Hayes modem, which has an ISA bus" (Ivan Mak, Sprinter FAQ 2000-07-31); "to allow using ISA modems" | DISK, SC05 |
| Peters Plus offer | no modem card in the option list (`INFO_014.TXT`: optional 5.25" FDD, HDD, DALLAS clock, ISA -> Spectrum-bus adapter, PAL encoder); users fitted PC modems | DISK, PP-WEB |
| Chip | a 16450 / 16550 UART at 1.8432 MHz behind the modem | BC-Term divisor table |
| BC-Term probe | slot `#D4`, then `#D6`; in each the bases `#3F8`, `#3E8`, `#2E8`, `#2F8` (CPU `#C3F8` ...); test: MCR bits 7-5 = 0, then LCR written and read back for 0..255 | BC-Term 1.11 disassembly |
| IRQ | yes: IM 2, receive interrupts through PIO port B; RTS dropped when the 16 KB ring is nearly full; transmit polled (CTS, THRE) | BC-Term 1.11 disassembly |
| Today | no telephone line to dial; the modern equivalent is a "Wi-Fi modem" or an emulator modem that dials `host:port` (telnet BBSes) | - |

### 5.5 SprinterSerial

| Item | Fact | Source |
|---|---|---|
| Card | ISA-8, PC16552D (two 16550s) at 1.8432 MHz; COM1 `#3F8` through a CH340 USB bridge, COM2 `#2F8` through a MAX232 to a DB-9 | SERCARD |
| IRQ | INTR1 / INTR2 jumpered (J5 / J6) to IRQ2, 3 or 4 | SERCARD schematic |
| Software | none written for it; BC-Term's probe finds a UART at `#3F8` / `#2F8` (inference: a null-modem link to a PC works with BC-Term) | - |

### 5.6 SprinterNet (Shaos)

| Item | Fact | Source |
|---|---|---|
| Hardware | ISA-8 card with a WIZnet W5100 module, GPIO, 2 KB RAM, 8 KB ROM, a 25C320 SPI EEPROM for MAC and settings | SNET forum |
| Status | prototype 3 assembled 2021-09-05, two wire fixes 2021-11; 2024-08-27 "no time for Sprinternet now"; no card released; the repository's `hardware/` and `firmware/` hold only README and licence | SNET |
| Software interface | a DSS call `RST #10` with `C = #D0`, `B` = function (netinit, getconf, socket, bind, listen, accept, connect, send, recv, sendto, recvfrom, close, httpget, resolv, time, hash, auth, persistent variables); "close to the Spectranet API" | SprintEm `network.cpp`, `bios.cpp:755` |
| Programs | INET1-4 test programs (2021), the "goferash" Gopher browser (2021-12); they run only in SprintEm (high-level emulation of the call through host sockets) | SNET |

### 5.7 Not network adapters on the Sprinter

| Candidate | Finding |
|---|---|
| Z84C15 SIO A / B | keyboard and mouse; the mouse connector is a 9-pin header with a PC COM bracket, but the port is used by the mouse driver and is "not standard RS-232" (PP-WEB FAQ). No program uses it for anything else |
| ZX-bus adapter + ZXNETUSB / Spectranet | no Sprinter program; NedoOS does not run on the Sprinter. The adapter passes I/O cycles only ([ISA open questions](../2026-10-02-sprinter-isa/open-questions.md) Q7) |
| PLD port code `#32` "XTR-modem and GS port redirected to ISA" | an Sp97 leftover that reaches no slot on the Sp2000 ([ISA research](../2026-10-02-sprinter-isa/research.md) §4.4) |
| "Sprinter RTL8019 (Sprinter only)" in the [network catalog](../2026-09-30-nedoos-integration/network-adapters-catalog.md) | the RTL8019 is the generic NE2000 clone of §5.1, not a Sprinter-specific card |

## 6. Software

| Program | Card | Purpose | Author, date | Where |
|---|---|---|---|---|
| `NETCFG`, `IFUP`, `NICINFO`, `ISAPROBE`, `NICEEP` | RTL8019AS / NE2000 | configuration (static or DHCP), card probe, EEPROM dump | Mikhalchenkov 2026 | RTLKIT |
| `PING`, `NSLOOKUP`, `NTP`, `TFTP`, `WGET`, `FTP`, `TELNET` | RTL8019AS / NE2000 | ping, DNS lookup, set the DSS clock, TFTP get / put, HTTP/1.0 download with redirects and resume, passive FTP, VT100 telnet with Zmodem / Ymodem | Mikhalchenkov 2026 | RTLKIT |
| `UNETRTL.DLL` | RTL8019AS / NE2000 | UNET library: TCP (connect, listen), UDP, DNS, ping, two channels | Mikhalchenkov 2026 | RTLKIT, UNET |
| bring-up tests `HELLO`, `NICRAM`, `NICLB`, `NICTX`, `NICRX`, `NICMODE`, `ARP`, `UDPTEST`, `UNETTEST` | RTL8019AS | packet RAM round trip, loopback, transmit, receive | Mikhalchenkov 2026 | RTLKIT (floppy image, not in the release zip) |
| `EL3INFO` + the same client set + `UNET509B.DLL` | 3C509B | as above | Mikhalchenkov 2026 | EL3KIT |
| `NETPROBE`, `NETRESET`, `NETCFG`, `NETUP`, `TCPTEST`, `UDPTEST`, `TFTP`, `FTP`, `PING`, `WGET`, `NTP`, `WTERM`, `TELNET` + `UNETESP.DLL` | SprinterESP | Wi-Fi join, the same clients over ESP AT | Mikhalchenkov on Boykov's code, 2026 | WIFIKIT |
| Gopher browser 0.2.3 | ESP by default, RTL as a build option (UNET) | Gopher browsing | Mikhalchenkov 2026 | GOPHER |
| Weather forecast | UNET | weather over Gopher | Mikhalchenkov 2026 | GOPHER |
| `ESPT.exe` 0.1 | SprinterESP | AT terminal | Sayman (Sprinter Team), about 2023 | DISK (MAME-pack system disk `UTILS/`) |
| `wterm.exe` 1.0 beta1 | SprinterESP | AT terminal, Wi-Fi setup | romych, 2024-07-13 | DISK, ESPKIT |
| `espset`, `wset`, `wtime`, `wtftp` | SprinterESP | Wi-Fi join, SNTP clock, TFTP (unfinished) | romych 2024-2025 | ESPKIT (sources) |
| Black Cat Terminal (BC-Term) 1.10 / 1.11 | ISA modem (16450 / 16550) | ANSI / AVATAR terminal, X / Y / Zmodem, EMSI | Aleksey Gavrilenko, 1998 / 2002 | DISK (`MODEM/BCT111.TRD`, Peters CD, DSS repack `bcterm.exe`) |
| INET1-4, goferash | SprinterNet (emulator only) | tests, Gopher | Shaos 2021 | SNET |

No DSS or BIOS network or serial driver API exists: every program drives its card directly (or through UNET
since 2026). The app.sprinter.ru catalog has no network category.

## 7. Development activity

| Date | Event |
|---|---|
| 1998-03 / 2002-04-19 | BC-Term 1.10 / 1.11 (ISA modem) |
| 2021-01 .. 2021-12 | SprinterNet thread, API, SprintEm support, Gopher (emulator only) |
| 2022-03 .. 2022-05 | SprinterSerial card |
| 2022-04 .. 2024-07 | SprinterESP card (rev 1.0.5), wterm 2024-07-13 |
| 2023-03 .. 2025-05 | ESPKit library |
| 2024-08-27 | SprinterNet paused |
| 2026-04-28 | Wi-Fi kit and a SprinterESP model in the MAME fork start |
| 2026-05-03 | RTL8019AS kit starts |
| 2026-06-07 | Gopher browser starts |
| 2026-08-26 .. 09-14 | UNET libraries |
| 2026-08-29 | 3C509B kit starts |
| 2026-09-12 / 09-13 | RTL kit releases 0.2.56 / 0.3.8 |
| 2026-09-20 | MAME PR #16206 (RTL8019AS ISA-8 card) opened |
| 2026-09-21 .. 09-29 | latest commits (RTL kit, 3C509B kit, MAME fork `enable-isa8-net`) |

**Stability.** The 2026 kits are young (0.1-0.3) but tested on real boards with documented acceptance runs (each
repository carries `specs.md` with an acceptance log and a host-side test harness that runs the real `.EXE` files
under a Z80 + RTL8019AS / 3C509B model). BC-Term and the ESP card hardware are finished and unchanged.

## 8. Other emulators

| Emulator | Network on the Sprinter |
|---|---|
| MAME upstream | two ISA-8 slots accept any PC card: `ne1000` (DP8390 at `#300`), `3c503`, `com` (8250 / 16450); ISA IRQs are not wired to the PIO; RTL8019AS card pending in PR #16206 |
| MAME fork (Mikhalchenkov) | SprinterESP card, RTL8019AS, 3C509B, an ISA COM card; pcap as the host provider on macOS (`slirp` not available on that build: RTLKIT `docs/MAME_NETWORK.md`); its RTL8019AS returns loopback frames into the receive ring, unlike the real chip (the kit handles both) |
| The kits' host harness | a JavaScript Z80 + DSS + ISA + RTL8019AS / 3C509B model with quirk switches (`hangOnResetPort`, `loopbackToRing`, PROM layouts): RTLKIT `tools/exe-harness/rtl8019-model.js` - a useful behavioral reference |
| SprintEm | SprinterNet call only (high level) |
| ZXMAK2, Unreal, others | nothing |

## 9. What unreal-ng already has

| Piece | State on master | Where |
|---|---|---|
| ISA bus | designed, not built: window 3 on `#D0-#D6` reads `#FF`; `#9FBD` keeps A19-A14 only | [ISA tdd](../2026-10-02-sprinter-isa/tdd.md) phases I1 (bus) and I4 (UART card + PIO IRQ lines) |
| `Uart16550` | `Chip16550` flavor with `uartClockHz`, FIFOs, AFE (MCR bit 5), no access wait; `InterruptActive()` exists but nothing on any machine consumes it yet | `core/src/emulator/io/serial/uart16550.{h,cpp}` |
| `ComPort` + peers | `ComPortSpec`: `NONE`, `LOOPBACK`, `TCP:host:port`, `SERIAL:device[,baud]`, `ESPNET[,baud]`, `AT[,baud]`; peers `LoopbackPeer`, `StreamPeer`, `AtModule`, `EspnetModule`; `ISerialPeer::OnModemLines(rts, dtr)` only (no OUT1 / OUT2) | `core/src/emulator/io/serial/` |
| ESP AT module | dialect by `[NETWORK] EspChip=`: ESP8266 = NonOS AT 1.7.4, ESP32 = AT 2.2.0; 5 links + server; missing for the Sprinter kits: `CWLAPOPT`, `CIPTCPOPT`, `SYSLOG`, `CIPDNS`, `CIPSERVERMAXCONN`, ESP8266 AT **2.2.1** identity; reset only by `AT+RST` | `core/src/emulator/io/serial/esp/atmodule.cpp` |
| Card wrapping a UART on a non-native bus | `Atm2IoEsp`: owns a `ComPort` with `Chip16550` params and a `registerOf` map, reached through the ATM INTERNAL bus; TTD through `TTDSerialPort` under its own id | `core/src/emulator/io/network/atm2ioesp.{h,cpp}` |
| Virtual network | socket-level (TCP, UDP, DNS, ICMP echo, DHCP by MAC, forwards, hosts table); host answers journaled as `NetEvent` at the frame boundary; `NetLinkReset` when leaving the recorded past; guests numbered: 1 = ZXNETUSB chip, 2 / 3 / 4 = COM / machine serial / ATM2IOESP (fixed `SerialGuests` fields) | `core/src/emulator/io/network/virtualnetwork.{h,cpp}`, `vnet/dhcpserver.*`, `core/src/common/network/` |
| Frame-level networking | **none**: the W5300 serves TCP / UDP / IPRAW sockets; MACRAW is not served. No ARP, IPv4 or TCP segment code exists | `w5300.cpp:379-384` |
| Network config and plan | `[NETWORK] Card=` (`ZXNETUSB`, `ZXWIFI`, `ATM2IOESP`), `ComPort=`, `ZxWifi=`, `EspChip=`, `Atm2IoEsp=`, `Atm2IoEspAddress=`; `NetworkManager::MakePlan()` reads `PortDecoder::DescribeNetwork()`; refused cards go to `not_fitted` | `networkmanager.cpp:32-117`, `portdecoder.h:795-833` |
| Sprinter network today | no `DescribeNetwork()` override: it inherits `zxBus = true`, so a ZXNETUSB could be "fitted" although nothing reaches it; legacy unused `[MISC] Modem=NONE` / `ZiFi=NONE` lines in `data/configs/sprinter/unreal.ini` | ISA open questions Q7 |
| Automation | `GET .../state/network`, `POST .../network/config` (+ OpenAPI `openapi_network.inc`), MCP `inspect_state` aspect `network`, CLI `network`, Lua / Python `network_state()` / `network_configure{}`; one report `DeviceState::Network` | `core/automation/`, `core/src/emulator/state/devicestate.cpp` |
| Qt | Network window with hand-built group boxes per card; a Qt-free `NetworkPanelModel` | `unreal-qt/src/network/` |
| TTD | `PeripheralId` 0-38 used (38 = ZX-Evo mouse, landed on master the same day), **39** next free; `SprinterIsa = 33` reserved; `TTDSerialPort` reusable for any 16550 + peer | `core/src/debugger/ttd/ttdserializable.h:44-89` |
| Recipes | `.recipe/peripherals/network.md` | - |

## 10. Corrections to earlier documents

- [network-adapters-catalog.md](../2026-09-30-nedoos-integration/network-adapters-catalog.md) lists "the Sprinter
  RTL8019 (Sprinter only)" as excluded: the RTL8019AS is a generic NE2000 clone and is now the main Sprinter
  network option (§5.1).
- [ISA research](../2026-10-02-sprinter-isa/research.md) §7.1: ESPT is by Sayman (Sprinter Team, about 2023),
  wterm by romych (2024-07-13); BC-Term probes four bases in both slots (`#3F8`, `#3E8`, `#2E8`, `#2F8`) and is
  interrupt-driven through PIO port B; SprinterESP's UART runs at 14.7456 MHz (divisor 8), not 1.8432 MHz; the
  2026 network kits (RTL8019AS, 3C509B, Wi-Fi) are missing from its table.
- [ISA research](../2026-10-02-sprinter-isa/research.md) §7.2 rates the network cards priority 3 after ISA RAM:
  with the 2026 kits and the owner's decision the Ethernet card moves up (this folder's [tdd.md](tdd.md) §8).
