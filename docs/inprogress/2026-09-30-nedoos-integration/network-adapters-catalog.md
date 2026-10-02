# Network adapters for ZX-Evo, ATM Turbo 2+ and related machines

**Status:** research result, 2026-09-30. Design: [tdd-network.md](tdd-network.md).
Folder index: [README.md](README.md).

Every network adapter we found for the machines unreal-ng emulates, what
software uses each one, and in which order we implement them. Facts come from
local sources: NedoOS, the pentevo SVN (BaseConf FPGA and AVR, the NedoPC
Unreal branches), the ZXNETUSB SVN (card CPLD, W5300 datasheet), the TS-Labs
tree, and other emulators.

## 1. Which adapter a program talks to

A ZX program reaches the network through one of three paths:

| Path | What the program does | Hardware behind it |
|---|---|---|
| **Kernel sockets** | NedoOS program calls `OS_NETSOCKET` / `OS_WIZNETREAD` ... (`currentNetwork=0`) | the kernel's network driver: Wiznet W5300 (`INETDRV=1`, kernel `sd_boot.$C`) or ESPNET (`INETDRV=2`, kernel `sd_bootesp.$C`) |
| **ESP AT commands** | the program writes `AT+CIPSTART=...` to a UART itself (`currentNetwork=1`, TR-DOS software such as Moon Rabbit) | an ESP8266 / ESP32 with stock AT firmware on a UART |
| **ESPNET frames** | the program speaks the ESPNET binary protocol to a UART itself (`currentNetwork=2`) | an ESP with NedoOS's ESPNET firmware on a UART |

The UART is the ZX-Evo COM port (a 16550 register set at `#F8EF..#FFEF`
served by the AVR), an external 16550 card (ZX-WiFi), or the ATM Turbo 2+
keyboard-controller COM port. NedoOS picks the UART type in `ini/espcom.ini`
(`comType` 0..3).

The full SD card in `testdata/machines/zxevo/nedoos/sdcard-full` boots the
**W5300 kernel** with `currentNetwork=0`: every network program there needs
the ZXNETUSB card.

## 2. Catalog

| Adapter | Machine / bus | Z80 ports | Chip | Software | Emulated elsewhere | Plan |
|---|---|---|---|---|---|---|
| **ZXNETUSB** (NedoPC, rev C) | any ZX-Bus machine: ZX-Evo, ATM, Pentagon | `#xxAB`: `#83AB` control / reset / IRQ, `#82AB` mode, `#81AB` W5300 address bits 9..6, `#00AB..#3FAB` W5300 register window, `#80AB` / `#xxAB` SL811 USB host | WIZnet W5300 + Cypress SL811HS + CPLD | NedoOS W5300 kernel: browser, telnet, ping, myip, zxdb, zxart radio, updater, time2, getpic, wget, IRC, FTP, scrnet; `wizcfg.com`; Moon Rabbit `mrf.com` | NedoPC Unreal branches (`zxusbnet.cpp`, WinSock, port glue missing) | **N1** |
| **ZX-Evo COM port** (Kondratyev 16550 map) | ZX-Evo BaseConf | `#F8EF..#FFEF` (register = A10..A8), served by the AVR with Z80 WAIT | AVR USART, 16-byte FIFOs, no IRQ, software RTS, no auto flow control | NedoOS ESPNET kernel (comType 0), ESPCOM AT apps, `cuart`, Moon Rabbit `mrfue.com` / `EVO-64.C`, zmodem | Unreal / zx-evo-unreal, ZXMAK2: host COM pass-through only | **N2** + ESP |
| **ZX-WiFi v1.6** | any ZX-Bus machine | same `#F8EF..#FFEF` | real 16550 at 1.8432 MHz with hardware RTS/CTS + ESP-12F | NedoOS comType 2, ESPNET firmware, Moon Rabbit `ZW-64.C` | as the COM port (register compatible) | N2 variant (auto flow control on) |
| **ESP AT personality** | behind any UART | - | ESP8266 / ESP32 stock AT firmware | ESPCOM apps (zxdb, time2, girc, gopher, zifi.com ...), Moon Rabbit, zx-net-tools | jnext (full AT engine, GPLv3: reference only) | **N3** |
| **ESPNET personality** | behind any UART | - | ESP running NedoOS ESPNET firmware 1.27 | NedoOS `sd_bootesp.$C` kernel, `currentNetwork=2` apps, `enet`, `espcfg` | none | **N3** |
| **ATM Turbo 2+ COM port** | ATM Turbo 2+ (ATM710) | `IN #55FE` enters command mode, then commands in the high byte (`#02FE` read, `#03FE` write, `#42FE` status, `#C2FE` RX count, `#C3FE` baud ...) | 8051-family keyboard controller | NedoOS ATM ESP kernels (comType 1), Moon Rabbit `mrfua.com` | none | **N4** |
| **ZiFi** (TS-Labs) | TS-Conf only | `#00EF..#BFEF` data, `#C0EF..#C9EF` FIFO state / control | ESP-01 AT on the TS AVR, FIFOs 512 / 256 | ZiFi shell `zifi.spg`, FT812 SDK | TS Unreal (COM pass-through), MAME tsconf, pico-spec | N5 (with TS-Conf) |
| **ATM2IOESP** | ATM | 16550 index via `OUT #FB`, data via `#FA` | 16550 + ESP | NedoOS comType 3 | none | N6, only if a board or user shows up |
| **AY-UART** | any 128K | bit-banged on AY port A (register 14) | ESP-01 / ESP-12 AT | Moon Rabbit `AY-64.C`, zx-net-tools (uGophy, wget, IRC) | none known | N6 |
| **ZX-Uno UART** | ZX-Uno, Karabas-Pro | `#FC3B` / `#FD3B` | ESP8266 AT | Moon Rabbit `UN-64.C`, uzifi | pico-spec, ZEsarUX | out of scope (machines not emulated) |
| **ZX Next UART** | ZX Next | `#133B..#163B` | ESP-01 AT | Next tools | jnext, MAME, CSpect | out of scope |
| **Spectranet** | 48K / 128K edge connector | paged memory `#0000-#3FFF`, `#003B..#033B`, traps | W5100 | Spectranet ROM, TNFS | Fuse | later, separate design (paging + ROM) |
| **SpeccyBoot**, ZXM-LANCard, legacy 8251 modems | various | various | ENC28J60, W5100, 8251 | TFTP boot / none | Fuse (SpeccyBoot) | not planned |

Not network adapters, checked and excluded: NeoGS (no network logic), CH376
(USB storage), the 2014 `zxinet_protocol.txt` (never built), the Sprinter
RTL8019 (a generic NE2000 clone, not Sprinter-specific; the Sprinter's network cards are designed in
[2026-10-02-sprinter-network](../2026-10-02-sprinter-network/tdd.md)).

## 3. Order of work

| Step | What | Why first |
|---|---|---|
| **N0** | network core: virtual LAN, host socket bridge, TTD journal, config, "no adapter" behavior | every adapter below uses it |
| **N1** | ZXNETUSB (W5300) | the user's NedoOS card needs it; fixes the zxdb hang; the widest NedoOS software set |
| **N2** | ZX-Evo COM port + ZX-WiFi (16550) | carries both ESP personalities; also a plain host serial bridge |
| **N3** | ESP personalities: ESPNET, then AT | ESPNET kernel and apps; AT for TR-DOS software and zx-net-tools |
| **N4** | ATM Turbo 2+ COM port | the same ESP personalities on ATM710 |
| **N5, N6** | ZiFi (with TS-Conf), AY-UART, ATM2IOESP | smaller audiences |

## 4. Sources

| Topic | Where |
|---|---|
| NedoOS kernel driver, W5300 | `NedoOS/src/kernel/w5300.asm`, `bdospg2.asm`, `sysbdos.asm` |
| NedoOS kernel driver, ESPNET | `NedoOS/src/kernel/espnet.asm`, `src/_sdk/espnet.asm` |
| ESPNET protocol and firmware | `NedoOS/src/kapps/common/espnet/` (`PROTOCOL.md`, `protocol.h` 1.27, `sockets.cpp`, `wifi_cmd.cpp`) |
| AT usage in NedoOS apps | `NedoOS/src/kapps/common/esp-com.c`, `zxdb/main.c` |
| NedoOS network API | `NedoOS/src/_sdk/api_net.txt` |
| wizcfg.com (no source) | disassembly and analysis: [reference-wizcfg.md](reference-wizcfg.md) |
| ZXNETUSB CPLD | `svn/zxusbnet/trunk/cpld/rtl/{zbus,ports,wizmap,top}.v`, PRM `doc/revC/zxnetusb_prm_revC` |
| W5300 | `svn/zxusbnet/trunk/pdfs/W5300_DS.pdf`, `W5300_errata.pdf`, `drivers/W5300_Drv_V1.2.2` |
| Prior W5300 model | `svn/pentevo/tools/unreal_fix/0.39.0/Unreal_NS/SRC/zxusbnet.cpp` (also `nedopc/`) |
| ZX-Evo COM port | `svn/pentevo/avr/baseconf/trunk/src/rs232.c`, `fpga/baseconf/trunk/z80/zports.v` |
| ZiFi | `github/zx-evo/pentevo/docs/ZiFi/zifi.md`, `avr/current/rs232.{c,h}` |
| Port table | `github/zx-evo/pentevo/docs/ZX/zx-ports-full-table.txt` |
