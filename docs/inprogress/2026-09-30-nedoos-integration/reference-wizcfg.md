# wizcfg.com: how it talks to the ZXNETUSB card (W5300)

**Status:** reference, 2026-09-30 (disassembly analysis). Used by [tdd-network.md](tdd-network.md).

Sources: `testdata/machines/zxevo/nedoos/sdcard-full/bin/wizcfg.com` (8913 bytes, identical to
NedoOS `release/bin/wizcfg.com`; no C source is in the NedoOS repo), disassembled with
`z80dasm -a -l -t -g 0x100` (regenerate the listing with that command; addresses below are load addresses). The DHCP traffic
goes through NedoOS kernel socket calls, so the kernel driver `src/kernel/w5300.asm` (NedoOS repo)
was read too. W5300 datasheet: `svn/zxusbnet/trunk/pdfs/W5300_DS.pdf`; the model summary is [reference-w5300-model.md](reference-w5300-model.md).

Tags: **[V addr]** = verified in the wizcfg binary at that address; **[V kernel]** = verified in
`src/kernel/w5300.asm` source (the shipped kernel binary is assumed to match it: inferred);
**[V DS pN]** = W5300 datasheet page; **[I]** = inferred.

## Summary

- wizcfg is a C program (printf/sscanf/strtok runtime). It is **not** a W5300 prober: it does not
  read IDR, MR or any socket register itself, and writes no MR/IMR/TMSR/RMSR/RTR/RCR.
- Its own W5300 traffic is small: a CPLD presence check, a reset pulse, writing SHAR (MAC) and
  GAR/SUBR/SIPR, and one write/read-back of SUBR0 (`#AA`) that decides between "real chip" and
  "emulator" behavior.
- **The shipped `net.ini` has `DHCP 1`, so on a chip that stores the `#AA` write, wizcfg runs a
  real DHCP client** (DISCOVER/REQUEST broadcast to 255.255.255.255:67 from port 68) through the
  kernel's UDP socket calls. The kernel driver has **untimed polling loops** on Sn_CR and Sn_SSR.
- On a chip that does *not* read back `#AA` (the Unreal reference model ignores common-register
  writes), DHCP is skipped entirely and wizcfg prints whatever GAR/SUBR/SIPR the chip returns.

## 1. Command line and net.ini

Argument parsing: `sub_0c73` splits the command line at `#0080` on spaces; main (`#0A73`) scans
argv[1..] for words starting with `-`, taking the second character case-insensitively (`res 5`):

| Option | Effect |
|---|---|
| `-V` | [V #0AC2] "view": reads W5300 `#008..#01B` (SHAR, GAR, SUBR, SIPR) into RAM (`sub_0774`) and prints gateway/mask/ip/dns, then exits. **No presence check, no reset, no net.ini.** DNS always prints `0.0.0.0` (never fetched). |
| `-S` | [V #0ACF, #0AD5] sets a flag whose only effect is to skip a second `GETSTDINOUT` BDOS call (`sub_1086`). In this build `-S` and "no arguments" behave identically (the flag byte is later overwritten by the presence-check value at #0AEE). |
| anything else | ignored |

net.ini handling (`sub_08cc`, #08CC..#0A72) [V]:
- Opened as relative `net.ini` (`OPENHANDLE`) after a `SETSYSDRV` call; up to 1024 bytes read into
  `#8068`. Open error or 0 bytes read -> skip parsing and go straight to "apply" with defaults
  (DHCP mode, MAC 02:02:6A:6A:3B:3B, name `ZXEvo`) [V #0B13, #0900].
- Split into lines with `strtok(buf,"\r\n")`; a line whose first char is `#` is skipped.
- Each line parsed with `sscanf(line,"%[^= ]%[= ]%s",key,sep,val)` and must yield 3 fields.
- The key is located with `strstr("DHCP    MAC     GW  MASKIPNAMEDNS", key)` (string at #22F2);
  the offset selects the field. Key matching is therefore substring-based and case-sensitive.

| Key | Offset | Parsed as | Stored at | Meaning |
|---|---|---|---|---|
| `DHCP` | 0 | first char `'1'` | sets `mask[0]=0` and **stops parsing** (later lines ignored) [V #097F] | DHCP mode. `DHCP 0` does nothing. |
| `MAC` | 8 | `%x:%x:%x:%x:%x:%x` (6 fields) | `#8000` | SHAR |
| `GW` | #10 | `%u.%u.%u.%u` | `#8008` | GAR |
| `MASK` | #14 | `%u.%u.%u.%u` | `#800C` | SUBR |
| `IP` | #18 | `%u.%u.%u.%u` | `#8010` | SIPR |
| `NAME` | #1A | string, max 64 chars | `#801A` | DHCP option 12 host name (DISCOVER only) |
| `DNS` | #1E | `%u.%u.%u.%u` | `#805D` | handed to kernel SETDNS (DHCP path only, see 2) |

The RAM block `#8000..#8013` mirrors W5300 `#008..#01B` exactly (SHAR, 2 pad bytes, GAR, SUBR,
SIPR). **Mode rule** [V #0B02, #080F]: before parsing, `mask[0]` is set to 0. After parsing,
`mask[0]==0` means DHCP, nonzero means static. So a static config needs a `MASK=` line with a
nonzero first octet, placed before any `DHCP 1`.

Shipped `bin/net.ini` (579 bytes, CRLF, CP866 Russian comments, no newline after the last line):
```
MAC=02:02:6A:6A:3B:3B      NAME =Speccy      IP = 192.168.1.177
MASK= 255.255.255.0        GW=192.168.1.1    DHCP 1
```
Result: MAC 02:02:6A:6A:3B:3B, name "Speccy", IP/MASK/GW parsed, then `DHCP 1` forces DHCP mode
(`mask[0]=0`, so mask becomes 0.255.255.0 if DHCP fails). There is no DNS line.

## 2. Port accesses in execution order (`wizcfg.com -S`)

Common prologue of every wizcfg W5300 helper (`sub_0BEA` block write, `sub_0C0F` block read,
`sub_0C34` byte read, `sub_0C4B` byte write) [V]: `DI; IN #82AB; OUT #82AB,(v AND #40) OR #10;
OUT #81AB,#00` — W5300 in ports, A0 not inverted, ROM window/memory map off, M/S bit kept;
address window 0 (common registers `#000..#03F`). The data access is then on port `(reg<<8)|#AB`.
`sub_0C34`/`sub_0C4B` return **without EI** [V #0C4A, #0C60]; the block helpers end with EI.

| # | Addr | Port | Dir | Value | Meaning |
|---|---|---|---|---|---|
| 1 | #0ADE | #81AB | OUT | #0A | presence check (DI first) |
| 2 | #0AE9 | #81AB | IN | low nibble must be #A | else print "ZXNetUsb not found", exit 0 [V #0AF4-#0AFF] |
| - | | | | | BDOS: SETSYSDRV, OPEN/READ/CLOSE net.ini (no ports) |
| 3 | #035A | #83AB | IN | d | reset pulse start (`sub_0355`, called at #07CE) |
| 4 | #0361 | #83AB | OUT | d AND NOT #10 | W5300 /RESET low |
| - | #0363 | | | | BDOS YIELD (one scheduler tick, about 20 ms [I]) |
| 5 | #0369 | #83AB | IN | d | |
| 6 | #0370 | #83AB | OUT | d OR #10 | W5300 /RESET released; then one more YIELD |
| 7 | #0BEA | #08AB..#0DAB | OUT x6 | 02 02 6A 6A 3B 3B | SHAR (MAC) via OUTI [V #07D7] |
| 8 | #0BEA | #10AB..#1BAB | OUT x12 | all #00 | clear GAR, SUBR, SIPR (source: 12 zero bytes at #223F) [V #07E3] |
| 9 | #0C4B | #14AB | OUT | #AA | SUBR0 test write [V #07EA] |
| 10 | #0C34 | #14AB | IN | expect #AA | **emulator detection** [V #07EF-#07F4] |
| 11a | #0C0F | #10AB..#1BAB | IN x12 | anything | read != #AA: copy chip GAR/SUBR/SIPR into RAM, **skip DHCP**, go to 13 [V #07F6] |
| 11b | #0C4B | #14AB | OUT | #00 | read == #AA: restore SUBR0 = 0 [V #0809] |
| 12 | | kernel | | | static mode: go to 13. DHCP mode: print "dhcp resolving...", run section 3 |
| 13 | #0BEA | #10AB..#1BAB | OUT x12 | GW, MASK, IP | final GAR/SUBR/SIPR write [V #08B9] |

Then printed (printf, #0B22..#0BE3): `"\r\ngateway "`, `%d.%d.%d.%d\r\n` of GAR, then `mask `,
`ip `, `dns ` the same way. MAC is not printed. `dns` is what kernel GETDNS returns (called right
before step 13 in every path except `-V`).

wizcfg itself has **no polling loop on any port**. All untimed loops are in the kernel driver
(section 3). Things wizcfg never touches: MR, IR, IMR, RTR, RCR, TMSR, RMSR, MTYPER, IDR, and all
socket registers (those only through the kernel).

## 3. DHCP (DHCP mode only; the shipped net.ini selects it)

Yes, it does DHCP, but not by driving socket registers directly. It uses NedoOS BDOS network
calls (`CMD_WIZNETOPEN #DB` with L=subfunction, `CMD_WIZNETWRITE #DE`, `CMD_WIZNETREAD #DD`),
which the kernel (`w5300.asm`) turns into W5300 socket accesses. There is no "DHCP" string except
the key name; ports appear as `#4400`/`#4300` stored little-endian (= 68/67 big-endian) [V #039F, #03B0].

### Client state machine (`sub_07C0`, #081D..#089D) [V]
- xid: 32-bit value at `#8016`, initial bytes `AB BA E7 51`; each DISCOVER round increments it
  first (so on the wire xid bytes are `AC BA E7 51`, then `AD ...`). The loop **ends when the
  low word reaches #BAB1**: at most 6 DISCOVERs (AC..B1), but only 5 receive windows, because the
  loop check right after sending the 6th exits [V #081D-#0830].
- State 0 (round): close the previous socket if any (`L=2`), `socket(AF_INET=2, SOCK_DGRAM=3)`
  (`L=1`; negative handle -> exit code 1), `bind` port 68 (`L=5`, sockaddr at `#906A`), set
  destination 255.255.255.255:67, build DISCOVER, `sendto`; state 1.
- State 1: receive window (`sub_050E`): up to 32 polls of `recvfrom(len #800 -> #8068)`; each empty
  poll (result <= 0) is followed by 5 YIELDs (about 100 ms [I]), so one window is about 3.2 s [I].
  Returns 0 = timeout (-> state 0, new xid), 1 = packet with wrong xid (bytes 4..7) or wrong chaddr
  (bytes 28..33 vs MAC) (-> state 1, fresh window), 2 = match.
- State 2: parse the matched packet (the OFFER): yiaddr (bytes 16..19) -> IP; options from byte
  240 on (cookie at 236 is **not** checked; op and DHCP message type are **not** checked):
  opt 1 -> mask, 3 -> gateway, 6 -> DNS (first 4 bytes), 54 -> server id (`#8061`), stop at `#FF`.
  Every option is treated as code+length, so a Pad (`#00`) option breaks the walk [V #06CC-#0773].
  Then build REQUEST in place over the DISCOVER options, `sendto`, **one** receive window, and exit
  the loop **whatever the result** (the ACK is never inspected) [V #0877-#089D].
- After the loop: close the socket, `SETDNS(#805D)` (`L=7`), `GETDNS(#805D)` (`L=8`), step 13.
- Worst case with no DHCP server: about 5 x 3.2 s = 16 s [I], then IP/GW from net.ini with
  mask 0.255.255.0, and DNS 0.0.0.0 (SETDNS of an empty value, then read back).
- Static and emulator-detect paths never call SETDNS; `dns` prints the kernel value
  (kernel default `8.8.4.4`, `w53_setdns.dns` [V kernel]).

### Packets sent (UDP, from port 68 to 255.255.255.255:67) [V #03C5, #05A6]
DISCOVER, 267 bytes: BOOTP header 240 bytes zeroed then op=1, htype=1, hlen=6, xid, flags=`80 00`
(broadcast), chaddr=MAC, cookie `63 82 53 63`; options
`35 01 01` | `37 03 01 06 03` | `3D 07 01 <MAC>` | `0C <n> <NAME>` | `00` (stray NUL of the name) | `FF`.
REQUEST, 272 bytes: same header (same xid); options
`35 01 03` | `36 04 <server id>` | `32 04 <offered IP>` | `3D 07 01 <MAC>` | `0C 05 "ZXEvo"` (hard-coded,
not NAME) | `FF`.

### What an emulator must return
A UDP datagram on the socket (any source; the kernel reads the 8-byte W5300 PACKET-INFO: peer IP,
peer port, length [V DS p104-105]) whose payload has the same xid bytes at 4..7, the MAC at 28..33,
yiaddr at 16..19, and options at 240.. using only TLV options ending in `FF`. One such OFFER is
enough; the ACK is optional.

### Kernel W5300 access sequences used for DHCP [V kernel]
Socket 0 is used (first free kernel slot, W5300 socket 0 -> `#81AB = #08`). Every call starts
with `IN #82AB; OUT #82AB,(v AND #40) OR #10; OUT #81AB,#08; IN #01AB` (Sn_MR1, masked `#0F`;
**must read back the written mode, or the call fails with ERR_NOTSOCK**). Register ports below are
`(offset<<8)|#AB` within the socket window.
- socket: `OUT #01 <- #02` (UDP), `OUT #0A,#0B <- local port` (#C001, #C002, ...).
- bind: `OUT #0A <- #00`, `OUT #0B <- #44` (port 68).
- sendto: `IN #09` (Sn_SSR1). `>= #22` -> go on; `0` -> OPEN: `OUT #03 <- #01`, **loop `IN #03`
  until 0 (no timeout)**, **loop `IN #09` until nonzero (no timeout)**; 1..#21 -> ERR_NOTCONN.
  `OUT #12,#13 <- 00 43`, `OUT #14..#17 <- FF FF FF FF`; `IN #27`, `IN #26` (Sn_TX_FSR low 16 bits,
  must be >= length, else EMSGSIZE and nothing is sent); data as pairs `OUT #2E`, `OUT #2F`
  (length rounded up to even); `OUT #22,#23 <- length` (Sn_TX_WRSR bits 15..0; #20/#21 never
  written); `OUT #03 <- #20` (SEND), **loop `IN #03` until 0 (no timeout)**.
- recvfrom: `IN #09`, if 0 -> OPEN as above; `IN #2B`, then `IN #2A` (Sn_RX_RSR bits 15..0);
  both 0 -> EAGAIN (HL=-1). Else `IN #09`, then from the RX FIFO: `#30,#31,#30,#31` = peer IP,
  `#30,#31` = peer port, `#30,#31` = size (big-endian); data as `#30/#31` pairs, odd last byte
  followed by one dummy `IN #31`; excess over 2048 bytes is read and dropped; then
  `OUT #03 <- #40` (RECV), **loop `IN #03` until 0**.
- close (e=0): `IN #09`, `OUT #03 <- #10` (CLOSE), loop `IN #03` until 0, **loop `IN #09` until 0
  (no timeout)**, `OUT #01 <- #00`.

## 4. Read-backs, checks and messages

| Check | Where | Condition | Outcome |
|---|---|---|---|
| CPLD register | #0AE9 | `IN #81AB AND #0F == #0A` | else `ZXNetUsb not found` + CRLF, exit 0 |
| SUBR0 write test | #07F4 | `IN #14AB == #AA` after writing #AA | else "emulator" path: read #10..#1B back, no DHCP |
| socket() result | #0395 | handle negative | exit with code 1, no message |
| DHCP reply | #0569-#059C | xid and chaddr match | else keep waiting / resend |

No IDR (`#5300`) check, no MR check, no read-back verification of SHAR/GAR/SIPR [V: the only W5300
reads in wizcfg are #14 at #0C47, the 12-byte block at #0C2A and the -V block at #07AB].
Strings in the binary (all at #22E0..#2373): `dhcp resolving...` (printed before the DHCP loop),
`ZXNetUsb not found`, `net.ini`, `\r\ngateway `, `mask `, `ip `, `dns `, `%d.%d.%d.%d\r\n`. There are no
other error messages (bind/sendto/recvfrom errors are silently ignored).

Normal output, DHCP success (values from the OFFER):
```
dhcp resolving...

gateway 192.168.1.1
mask 255.255.255.0
ip 192.168.1.50
dns 192.168.1.1
```

## 5. Minimal W5300/CPLD behavior for `wizcfg -S` to succeed

CPLD (all [V] from the access pattern):
1. `#81AB` stores a write and reads back at least bits 3:0 (presence check).
2. `#82AB` read returns bit 6 (M/S) and bit 7 (VBUS); write bit 4 enables W5300 in ports
   `#00AB..#3FAB`; bit 3 (A0 invert) is always written 0 here.
3. `#83AB` read returns the last written bit 4 (read-modify-write reset pulse); a 1->0->1 on bit 4
   should reset the W5300 model (datasheet asks 2 us low, 10 ms before use [V DS p15]; wizcfg gives
   about 1 frame each [I]).

W5300, two ways to make it work:

**A. "Emulator" path (what Unreal does; no network needed).** Common-register reads come from a
table; writes to `#014` must not read back as `#AA`. wizcfg then prints the table's GAR/SUBR/SIPR,
never opens a socket, and finishes at once. Unreal stores GAR/SUBR/SIPR (#10..#1B) from the host
adapter, MR=#38, IDR=#5300, TMSR/RMSR=8 [V zxusbnet.cpp `Wiz5300_Init`, `Wiz5300_RegWrite`].
Minimum: return the desired 12 bytes on `#10..#1B` and anything but `#AA` on `#14` after an `#AA`
write. Downside: MAC/IP writes are lost; the machine keeps the table's values.

**B. Real-chip path (writes stored).**
1. Common registers `#008..#01B` store writes and read back (SUBR0 test).
2. Socket 0 registers: Sn_MR1 (`#01`) R/W; Sn_CR1 (`#03`) reads 0 once the command is taken (the
   datasheet auto-clear [V DS p70]); **Sn_SSR1 (`#09`) = #22 after OPEN in UDP mode, 0 after
   CLOSE** [V DS p77]; PORTR/DPORTR/DIPR store writes; TX_FSR bits 15..0 (`#26/#27`) >= 268 (reset
   default 8 KB per socket is fine); TX FIFO (`#2E/#2F` pairs, first byte at the even address
   [V DS p87]) collects bytes and SEND transmits `TX_WRSR` bytes to DIPR:DPORTR; RX_RSR
   (`#2A/#2B`) = bytes waiting (8-byte PACKET-INFO + data, odd data padded); RX FIFO (`#30/#31`,
   even address = first byte [V DS p88]); RECV drops the consumed packet.
3. A DHCP responder: either forward the broadcast to the host network (binding port 68 on the host
   needs privileges [I]) or answer inside the emulator with one OFFER as described in section 3.
   Without it wizcfg still completes, after about 16 s, printing net.ini IP/GW, mask 0.255.255.0,
   dns 0.0.0.0.
4. **Hang risks (no timeouts in the kernel):** Sn_CR never returning 0, Sn_SSR staying 0 after
   OPEN, or Sn_SSR staying nonzero after CLOSE each freeze the boot in `autoexec.bat`.

**Card absent (all ports read #FF):** `#81AB` reads #FF, low nibble #F != #A -> prints
`ZXNetUsb not found` and exits 0 immediately; no other port is touched and autoexec continues
[V #0AE9-#0B01]. With `-V` there is no presence check: it would print 255.255.255.255 for gateway,
mask and ip, and dns 0.0.0.0 [V #0774-#07BF].

## 6. BDOS calls made (C=function, `CALL #0005`) [V]

| Call | Where | Use |
|---|---|---|
| `#D3` GETSTDINOUT | #0C9D | stdin/stdout handles (once with -S, twice without) |
| `#49` WRITEHANDLE, `#FF` YIELDKEEP | #0E01 | all text output to stdout (retry on partial write) |
| `#EA` SETSYSDRV | #10FD | before opening net.ini |
| `#43` OPENHANDLE, `#48` READHANDLE, `#45` CLOSEHANDLE | #10E0, #110A, #10E7 | read net.ini (1024 bytes max) |
| `#F2` YIELD | #0C61 | delays: reset pulse, receive polling |
| `#DB` WIZNETOPEN, L=1/2/5/7/8 | #1141, #113D, #1131, #1129, #1125 | socket, close, bind, SETDNS, GETDNS |
| `#DE` WIZNETWRITE (UDP sendto, IX=buf, DE=sockaddr) | #1157 | DISCOVER, REQUEST |
| `#DD` WIZNETREAD (UDP recvfrom) | #115C | OFFER / ACK |

Exit is `JP #0000` (#0142), with DE=1 after a socket() failure and DE=0 after "not found".
