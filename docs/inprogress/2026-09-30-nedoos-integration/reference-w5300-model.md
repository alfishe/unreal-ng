# WIZnet W5300 on ZXNETUSB: emulator model spec

**Status:** reference, 2026-09-30. Used by [tdd-network.md](tdd-network.md).

Summary for writing a C++ model of the W5300 as the ZXNETUSB card uses it (8-bit bus,
direct address mode), so the datasheet does not need to be reopened.

Source tags:
- `[DS p.N]`: W5300 datasheet v1.3.1, `zxusbnet/trunk/pdfs/W5300_DS.pdf`. The printed page number equals the PDF page number.
- `[ERR n]`: `W5300_errata.pdf`, erratum n.
- `[DRV file:line]`: WIZnet driver `drivers/W5300_Drv_V1.2.2`, the 8-bit byte-access build.
- `[RTL file:line]`: CPLD `cpld/rtl/*.v`.
- `[PRM p.N]`: `doc/revC/zxnetusb_prm_revC.pdf`.
- `[NEDOOS line]`: `NedoOS/src/kernel/w5300.asm`. This is the driver the build includes (`bdospg2.asm:175`). `w5300ini.asm` is an older, unused variant that relies on A0 inversion.
- `[UNS line]`: `Unreal_NS/SRC/zxusbnet.cpp`.
- `[inferred]`: my reading. No source states it.

---

## 1. Bus interface (8-bit direct mode)

### 1.1 Chip side
- `BIT16EN` is tied low on the card, so the chip runs in 8-bit mode. This is latched into MR bit 15 (DBW) at reset and cannot change afterwards [DS p.15, p.48]. In 8-bit mode ADDR[9:0] are all used [DS p.118].
- Direct mode (MR.IND=0, the reset state) exposes the chip as 0x400 bytes [DS p.24-25]:

| Range | Contents |
|---|---|
| 0x000-0x0FF | MR plus the common registers |
| 0x100-0x1FF | Reserved |
| 0x200-0x3FF | 8 sockets × 0x40 bytes, `Sn_base = 0x200 + 0x40*n` |

- **Byte order is big-endian.** The even (lower) address holds the MSB. For example MR0 at 0x000 is bits 15:8 and MR1 at 0x001 is bits 7:0 [DS p.26].
- Registers that are only 8 bits wide sit at the odd address: Sn_CR, Sn_IMR, Sn_IR, Sn_SSR, Sn_PROTOR, Sn_TOSR, Sn_TTLR, Sn_FRAGR, RCR, PTIMER, PMAGICR and Pn_BRDYR. The even byte next to each is reserved [DS p.28-32].
- **Non-FIFO registers are plain bytes.** Each byte is read and written on its own, with no pairing and no commit on either half:
  - The WIZnet 8-bit driver writes 16- and 32-bit registers byte by byte in MSB-first order [DRV w5300.c:48-58, 466-481].
  - NedoOS writes only the low byte of Sn_MR, Sn_CR, Sn_TX_WRSR and so on [NEDOOS 218-230, 489-491, 786-789].
  - Multi-byte counters are not snapshot-latched. The driver re-reads Sn_SSR, Sn_TX_FSR and Sn_RX_RSR until two reads agree [DRV w5300.c:376-388, 483-514]. The model can return live values.
- **MR.DBS** (bit 2, byte swap of every register) is only valid when DBW=1. It is ignored in 8-bit mode [DS p.50].
- **MR.FS** (bit 8) swaps MSB and LSB of Sn_TX_FIFOR/Sn_RX_FIFOR only [DS p.49]. The 8-bit driver honors FS when it parses PACKET-INFO [DRV socket.c:233-240, 378-392]. So in 8-bit mode FS=1 means that FIFOR0 (even) carries the second stream byte and FIFOR1 (odd) the first [inferred]. NedoOS never sets FS.

### 1.2 FIFO access rule in 8-bit mode
- Sn_TX_FIFOR0 and Sn_TX_FIFOR1 must be accessed as a pair: first FIFOR0 (even address), then FIFOR1 (odd address).
  - After FIFOR0, no other W5300 register may be accessed until FIFOR1 [DS p.87].
  - To write one byte, write the data to FIFOR0 and a dummy byte to FIFOR1 [DS p.87].
- Sn_RX_FIFOR follows the same pairing rule [DS p.88].
  - Do not read RX_FIFOR right after accessing TX_FIFOR. Read any other register, for example Sn_MR, in between [DS p.88].
  - With MR.MT=0, TX_FIFOR is write-only and RX_FIFOR is read-only [DS p.87-88].
- Stream order (FS=0):
  - TX: the byte written to FIFOR0 is stored at the lower TX memory address and is sent first [DS p.87].
  - RX: FIFOR0 returns the earlier stream byte [DS p.88-89].
- **The datasheet does not say which half actually pushes or pops.** Model recommendation [inferred]:
  - FIFOR0 read returns `mem[rd]`. FIFOR1 read returns `mem[rd+1]`, then `rd += 2` and `RSR -= 2`.
  - FIFOR0 write latches the byte. FIFOR1 write stores both bytes, then `wr += 2`.
  - Do **not** reset this pairing state when other registers are accessed in between. NedoOS reads FIFOR0, returns to the user, and on the next syscall reads Sn_MR (0x201) and writes the card ports before it reads FIFOR1 [NEDOOS 604-691, 347-359]. It works on real hardware, so the chip evidently tolerates this. Unreal_NS uses the same peek/pop scheme [UNS 363-373, 544-548].
  - A lone FIFOR1 access without FIFOR0 is undefined [DS p.87]. The model should still advance.

### 1.3 How the card maps the chip ([RTL], [PRM p.3-9])

**Card port decoding**
- A card I/O port is selected when A[7:0] = 0xAB. IORQGE depends on the address only [RTL zbus.v:201-205].
- Control ports need A15=1, and A[9:8] selects the register [RTL zbus.v:210-214, ports.v]:

| A[9:8] | Port | Function |
|---|---|---|
| 11 | 0x83AB | Interrupt and reset control |
| 10 | 0x82AB | Card configuration |
| 01 | 0x81AB | W5300 address high bits |
| 00 | 0x80AB | SL811 address register |

  - Mirrors exist, because A[14:10] are not decoded.

**Control ports**
- **0x83AB** [RTL ports.v:55-62, 104]:
  - Bit 4 = W5300 /RESET. 0 holds the chip in reset.
  - Bit 2 = route the W5300 /INT into the card's internal interrupt.
  - Bit 6 = drive the internal interrupt onto Z80 /INT.
  - Bit 0 (read-only) = W5300 /INT is asserted.
  - Bit 7 (read-only) = internal interrupt state.
  - **All bits reset to 0, so the W5300 stays in reset until software sets bit 4.** The PRM reset sequence is: bit4=0, HALT, bit4=1, HALT [PRM p.7]. The datasheet asks for at least 2 µs low and at least 10 ms settle time [DS p.15].
- **0x82AB** [RTL ports.v:76-84, 107]:
  - Bit 4 = W5300 in I/O space.
  - Bit 3 = invert W5300 ADDR[0].
  - Bit 2 = W5300 replaces ROM in the memory window.
  - Bits 1:0 = which 16 KB window.
  - Bit 6 = SL811 M/S.
  - The RTL enforces that bits 2 and 4 are mutually exclusive (`w5300_ports = b4 & ~b2`, `rommap_ena = b2 & ~b4`). If both are set, both are off. The PRM just calls that combination "undefined".
- **0x81AB**: bits 3:0 become W5300 ADDR[9:6], so each value selects a 64-byte block [RTL ports.v:93-96].

**I/O-mapped W5300 access** (0x82AB bit 4 = 1)
- Any port with A15=0 and A[7:0]=0xAB hits the W5300 [RTL zbus.v:231-232].
- `ADDR = {p81[3:0], A13..A9, A8 ^ inv}` [RTL wizmap.v:46].
  - A14 is ignored, so 0x00AB-0x3FAB mirror 0x40AB-0x7FAB. The PRM documents only 0x40AB-0x7FAB.
  - NedoOS uses B = register offset in the range 0x00-0x3F and p81 = 8+n, which selects socket n's block 0x200+0x40n [NEDOOS 2-3, 348-355].

**Memory-mapped W5300 access** (0x82AB bit 2 = 1, Z80 A[15:14] = p82[1:0])
- Every Z80 write into the window also writes the W5300 [RTL zbus.v:228].
- Reads come from the W5300 only while ROM is paged into that window (/CSROM active) [RTL zbus.v:229]. The card blocks the ROM (ZBLKROM) [RTL zbus.v:235].
- Address translation inside the window, with offset `o` = A[13:0] [RTL wizmap.v:24-42], [PRM p.6]:

| Offset | W5300 address |
|---|---|
| 0x0000-0x1FFF | `o[9:0]`, the whole chip mirrored 8 times |
| 0x2000-0x2FFF | Socket `o[11:9]` TX_FIFOR (0x2E/0x2F) |
| 0x3000-0x3FFF | Socket `o[11:9]` RX_FIFOR (0x30/0x31) |

  - In the FIFO windows, bit 0 = `o[0]^inv`, so LDIR and POP can stream data.

**"Invert A0"** (0x82AB bit 3) is purely a card-side XOR on the chip's ADDR[0] [RTL wizmap.v:24, 46]. The chip sees nothing different.
- Effect: a Z80 little-endian `LD (reg),HL` lands with H at the even (MSB) byte, so 16-bit registers can be accessed with one instruction [PRM p.8].
- For the FIFOs the software must then access reg+1 first, because reg+1 is physically FIFOR0 [PRM p.8-9].
- Implement it in the card layer, not in the chip model. NedoOS does not use it: it writes 0x82AB with bits 6 and 4 only [NEDOOS 348-352].

---

## 2. Common registers (0x000-0x0FF) [DS p.27-30, 48-67]

Reset values are for an 8-bit bus. "Emu" marks how much an emulator needs each one: **must**, *nice*, or - (can be backed by plain storage).

| Addr | Name | W | Reset | R/W | Meaning | Emu |
|---|---|---|---|---|---|---|
| 000-001 | MR | 16 | 0x3800 | R/W | b15 DBW (R, 0 = 8-bit), b14 MPF (R, pause frame), b13:11 WDF, b10 RDH, b8 FS, b7 RST, b5 MT, b4 PB (ping block), b3 PPPoE, b2 DBS, b0 IND [DS p.48-50] | **must** |
| 002-003 | IR | 16 | 0 | R/W | b15 IPCF, b14 DPUR, b13 PPPT, b12 FMTU (write 1 to clear); b7..0 Sn_INT (auto-clear when Sn_IR becomes 0) [DS p.52-54] | *nice* |
| 004-005 | IMR | 16 | 0 | R/W | Mask for /INT, same bit layout as IR [DS p.54-55] | *nice* |
| 006-007 | - | | | | Reserved | |
| 008-00D | SHAR | 48 | 0 | R/W | MAC address [DS p.56] | **must** (store) |
| 00E-00F | - | | | | Reserved | |
| 010-013 | GAR | 32 | 0 | R/W | Gateway IP | **must** (store) |
| 014-017 | SUBR | 32 | 0 | R/W | Subnet mask (see ERR 4/5) | **must** (store) |
| 018-01B | SIPR | 32 | 0 | R/W | Source IP | **must** (store) |
| 01C-01D | RTR | 16 | 0x07D0 | R/W | Retry period in 100 µs units (0x07D0 = 200 ms) [DS p.57] | *nice* |
| 01E | - | | | | Reserved | |
| 01F | RCR | 8 | 0x08 | R/W | Retry count [DS p.57] | *nice* |
| 020-027 | TMSR0..7 | 8 each | 0x08 | R/W | TX memory per socket in KB (0-64) [DS p.59] | **must** (sets FSR) |
| 028-02F | RMSR0..7 | 8 each | 0x08 | R/W | RX memory per socket in KB (0-64) [DS p.60] | *nice* |
| 030-031 | MTYPER | 16 | 0x00FF | R/W | 8 KB block type, 1 = TX, filled from bit 0 upward [DS p.61-62] | - |
| 032-033 | PATR | 16 | 0 | R | PPPoE auth type (0xC023 PAP, 0xC223 CHAP) | - |
| 037 | PTIMER | 8 | 0x28 | R/W | LCP echo timer in 25 ms units | - |
| 039 | PMAGICR | 8 | 0 | R/W | LCP magic byte | - |
| 03C-03D | PSIDR | 16 | 0 | R | PPPoE session ID | - |
| 040-045 | PDHAR | 48 | 0 | R | PPPoE server MAC | - |
| 048-04B | UIPR | 32 | 0 | R | IP from ICMP unreachable or fragment-MTU message [DS p.64] | *nice* |
| 04C-04D | UPORTR | 16 | 0 | R | Unreachable port [DS p.64] | *nice* |
| 04E-04F | FMTUR | 16 | 0 | R | MTU from an ICMP fragment-needed message [DS p.64] | - |
| 060+4k, 061+4k | Pn_BRDYR (k=0..3) | 8 | 0x00 | R/W | b7 PEN, b6 PMT (1 = TX), b5 PPL, b2:0 socket number [DS p.64-66] | - (pins not wired on the card [inferred]) |
| 062+4k-063+4k | Pn_BDPTHR | 16 | undefined | R/W | Buffer-depth threshold for the BRDY pin | - |
| 0FE-0FF | IDR | 16 | 0x5300 | R | ID: **0x0FE = 0x53, 0x0FF = 0x00** [DS p.67] | **must** |

All other addresses in 0x000-0x0FF are reserved. Reads return 0 [inferred].

**MR.RST** [DS p.49]:
- Writing 1 to MR bit 7 (the byte at 0x001) performs a software reset. The bit clears itself when the reset completes.
- The driver writes `MR = 0x80` and then waits 5 ms [DRV w5300.c:577-580].
- Model: reset every register to its reset value and close every socket. MR returns to 0x3800 (DBW stays 0). Reads of bit 7 return 0.

**TMSR/RMSR/MTYPER validity** [DS p.59-62], [DRV w5300.c:632-705]:
- Each TMSRn and RMSRn must be 64 or less.
- TMSSUM = ΣTMSR must be a multiple of 8.
- ΣTMSR + ΣRMSR must equal 128.
- MTYPER must have its low TMSSUM/8 bits set.
- Chip behavior with invalid values is undefined. The model should accept them and size each socket's buffers as TMSRn×1024 and RMSRn×1024 [inferred].
- At reset every socket has 8 KB each way, so Sn_TX_FSR = 0x2000.

**Network identity:**
- SHAR, GAR, SUBR and SIPR are ordinary storage. The chip only uses them for ARP, IP headers and subnet routing.
- A host-socket emulator can ignore them for routing but must read them back as written. NedoOS `wizcfg.com` (binary only, no source in the repo) programs them from `net.ini` (MAC, IP, MASK, GW, DHCP).

---

## 3. Socket registers (`Sn_base = 0x200 + 0x40*n`) [DS p.31-32, 68-89]

| Off | Name | W | Reset | R/W | Meaning | Emu |
|---|---|---|---|---|---|---|
| 00-01 | Sn_MR | 16 | 0x0000 | R/W | b8 ALIGN (TCP only: no PACKET-INFO), b7 MULTI (UDP), b6 MF (MACRAW), b5 ND/IGMPv, b3:0 protocol: 0 closed, 1 TCP, 2 UDP, 3 IPRAW, 4 MACRAW (socket 0 only), 5 PPPoE (socket 0 only) [DS p.68-70] | **must** |
| 03 | Sn_CR | 8 | 0 | R/W | Command register. Reads return 0 once the chip has accepted the command [DS p.70] | **must** |
| 05 | Sn_IMR | 8 | **0xFF** | R/W | Gates the **setting** of Sn_IR bits [DS p.74] | **must** |
| 07 | Sn_IR | 8 | 0 | R/W | b4 SENDOK, b3 TIMEOUT, b2 RECV, b1 DISCON, b0 CON, b7..5 PPP. Write 1 to clear [DS p.74-75] | **must** |
| 09 | Sn_SSR | 8 | 0 | R | Socket state (section 4) | **must** |
| 0A-0B | Sn_PORTR | 16 | 0 | R/W | Source port. Set before OPEN (TCP/UDP) [DS p.79-80] | **must** |
| 0C-11 | Sn_DHAR | 48 | FF:FF:FF:FF:FF:FF | R/W | Destination MAC. Filled by ARP; used by SEND_MAC [DS p.80] | - |
| 12-13 | Sn_DPORTR | 16 | 0 | **WO** | Destination port [DS p.81]. On readback after a TCP connect the high byte appears twice [ERR 3] | **must** |
| 14-17 | Sn_DIPR | 32 | 0 | R/W | Destination IP. A TCP server fills in the peer IP [DS p.81] | **must** |
| 18-19 | Sn_MSSR | 16 | 0 | R/W | MSS/MTU. 0 means the default: TCP 1460, UDP 1472, IPRAW 1480, MACRAW 1514 [DS p.82] | - |
| 1A | Sn_KPALVTR | 8 | 0 | R/W | Keep-alive period in 5 s units (TCP) [DS p.82-83] | - |
| 1B | Sn_PROTOR | 8 | 0 | R/W | IP protocol number for IPRAW. Set before OPEN; 6 and 17 are not allowed [DS p.83] | **must** (ICMP) |
| 1D | Sn_TOSR | 8 | 0 | R/W | IP TOS | - |
| 1F | Sn_TTLR | 8 | 0x80 | R/W | IP TTL [DS p.84] | - |
| 20-23 | Sn_TX_WRSR | 17 | 0 | R/W | Byte count for SEND. Bit 16 = bit 0 of 0x21; bits 15:0 = 0x22:0x23 [DS p.84] | **must** |
| 24-27 | Sn_TX_FSR | 17 | 0x2000 | R | TX free bytes. Bit 16 = bit 0 of 0x25 [DS p.85] | **must** |
| 28-2B | Sn_RX_RSR | 17 | 0 | R | RX bytes stored. Bit 16 = bit 0 of 0x29 [DS p.85-86] | **must** |
| 2D | Sn_FRAGR | 8 | 0x40 | R/W | IP flags byte (DF). Fragmentation is not supported [DS p.86] | - |
| 2E-2F | Sn_TX_FIFOR | 16 | undefined | W (R/W if MT=1) | TX memory window [DS p.86-87] | **must** |
| 30-31 | Sn_RX_FIFOR | 16 | undefined | R (R/W if MT=1) | RX memory window [DS p.88-89] | **must** |
| 32-3F | - | | | | Reserved | |

Datasheet inconsistencies:
- The Sn_MR bit table shows a '1' in bit 5 of the reset row, but the register header says 0x0000 [DS p.68]. Use 0.
- The Pn_BRDYR reset row similarly shows PPL=1.

---

## 4. Commands (Sn_CR) and states (Sn_SSR)

### 4.1 When Sn_CR clears
"When W5300 detects any command, Sn_CR is automatically cleared to 0x00. The command may still be executing; check Sn_IR or Sn_SSR for completion" [DS p.70].
- Every driver writes CR and then spins until it reads 0 [DRV w5300.c:339-343], [NEDOOS 489-496].
- **The model must clear CR for every command, including invalid ones and commands issued in the wrong state.** Otherwise the guest hangs. The simplest approach is to perform the command's immediate part on write and return 0 on the next read [inferred].

### 4.2 Commands [DS p.70-73, p.76-79]

| Cmd | Name | Valid in | Effect / resulting SSR | Sn_IR bits |
|---|---|---|---|---|
| 0x01 | OPEN | CLOSED (Sn_MR protocol ≠ 0) | Resets the socket's buffers and pointers. SSR becomes INIT (TCP), UDP, IPRAW, MACRAW or PPPoE. If the protocol is 0, nothing changes [DS p.70] | - |
| 0x02 | LISTEN | INIT, TCP | SSR = LISTEN. Incoming SYN → SYNRECV (0x16) → ESTABLISHED. SYN/ACK failure → TCPTO → CLOSED. A SYN to a port with no LISTEN socket is answered with RST and no SSR changes [DS p.71] | CON on establish; TIMEOUT on failure |
| 0x04 | CONNECT | INIT, TCP | ARP (0x01) if needed → SYNSENT (0x15) → ESTABLISHED. On ARPTO, TCPTO or a received RST, SSR = CLOSED [DS p.71] | CON on success; TIMEOUT on ARPTO or TCPTO (RST: no documented bit [inferred]) |
| 0x08 | DISCON | ESTABLISHED, CLOSE_WAIT | Sends FIN; passes through FIN_WAIT, TIME_WAIT or LAST_ACK; CLOSED once FIN/ACK arrives, or on TCPTO [DS p.71-72] | DISCON when the peer's FIN or FIN/ACK arrives; TIMEOUT on failure |
| 0x10 | CLOSE | any | SSR = CLOSED immediately, with no packets sent [DS p.72] | - |
| 0x20 | SEND | ESTABLISHED/CLOSE_WAIT (TCP), UDP, IPRAW, MACRAW | Sends Sn_TX_WRSR bytes from TX memory. TCP and UDP split the data at MSS; IPRAW and MACRAW do not. UDP and IPRAW show ARP (0x01) while resolving, and only when DIPR changed; on ARPTO they return to UDP or IPRAW [DS p.72, p.78-79] | SENDOK when the send finishes; TIMEOUT on ARPTO, or on TCP ACK timeout (then SSR = CLOSED) |
| 0x21 | SEND_MAC | UDP, IPRAW | Same as SEND but uses Sn_DHAR and skips ARP [DS p.72-73] | SENDOK |
| 0x22 | SEND_KEEP | ESTABLISHED with KPALVTR=0 | Sends a keep-alive; failure → TCPTO → CLOSED [DS p.73] | TIMEOUT on failure |
| 0x40 | RECV | any open state | Host declares the data it read as consumed; frees RX space [DS p.73] | - (RECV is raised by incoming data, not by this command) |
| 0x23-0x27 | PCON, PDISCON, PCR, PCN, PCJ | Socket 0 in PPPoE mode | PPPoE commands. Not needed | PRECV, PFAIL, PNEXT |

- **Sn_IR RECV** is set whenever a data packet arrives [DS p.75].
- **Sn_IR DISCON** is set when the peer's FIN arrives. For a passive close, SSR becomes CLOSE_WAIT [DS p.75, 77].
- A received RST sets SSR = CLOSED unconditionally [DS p.72].

### 4.3 SSR values [DS p.75-79], [DRV include/w5300.h:549-563]

| Value | Name | Kind |
|---|---|---|
| 0x00 | CLOSED | |
| 0x01 | ARP | Transient |
| 0x13 | INIT | |
| 0x14 | LISTEN | |
| 0x15 | SYNSENT | Transient |
| 0x16 | SYNRECV | Transient |
| 0x17 | ESTABLISHED | |
| 0x18 | FIN_WAIT | Transient |
| 0x1A | CLOSING | Transient; only in the driver header |
| 0x1B | TIME_WAIT | Transient |
| 0x1C | CLOSE_WAIT | |
| 0x1D | LAST_ACK | Transient |
| 0x22 | UDP | |
| 0x32 | IPRAW | |
| 0x42 | MACRAW | |
| 0x5F | PPPoE | |

- 0x10 and 0x11 are undocumented stuck values seen after CLOSE or DISCON during a pending SEND [ERR 1].

### 4.4 State diagrams (text form of Fig 5 [DS p.79])
- **TCP client:**
  - CLOSED -OPEN-> INIT -CONNECT-> [ARP] -> SYNSENT -SYN/ACK-> ESTABLISHED (IR CON).
  - ARPTO or TCPTO (IR TIMEOUT), or RST: -> CLOSED.
- **TCP server:**
  - CLOSED -OPEN-> INIT -LISTEN-> LISTEN -SYN-> SYNRECV -> ESTABLISHED (IR CON).
  - TCPTO: -> CLOSED.
- **Established TCP:**
  - SEND and RECV keep the state.
  - Peer FIN: -> CLOSE_WAIT (IR DISCON). SEND and RECV are still allowed; then DISCON (-> LAST_ACK -> CLOSED) or CLOSE.
  - Local DISCON: -> FIN_WAIT or TIME_WAIT -> CLOSED (IR DISCON when the FIN/ACK arrives).
  - CLOSE from any state: -> CLOSED.
- **UDP:** CLOSED -OPEN-> UDP. SEND and SEND_MAC loop in UDP, with an optional ARP transient; ARPTO returns to UDP with IR TIMEOUT. CLOSE -> CLOSED.
- **IPRAW:** same as UDP with state IPRAW; ICMP needs PROTOR = 1.
- **MACRAW:** socket 0 only. CLOSED -OPEN-> MACRAW. There is no timeout [DS p.117].

---

## 5. TX path [DS p.84-87, 98-99, 105-106]
1. Host checks **Sn_TX_FSR**: free bytes, 17 bits. Its reset value equals TMSRn×1024 (0x2000 by default) [DS p.85]. The host must not write more than FSR bytes.
2. Host writes **ceil(len/2)** word pairs through FIFOR0/FIFOR1. For an odd length the last pair is (data, dummy) [DS p.87]. FSR drops as the FIFO fills: by 2 per word [inferred]. Unreal_NS counts it the same way [UNS 339-347].
3. Host writes the **exact** byte count (not rounded) to **Sn_TX_WRSR**: 17 bits, bytes, at most the TX memory size. For IPRAW and MACRAW it must be at most the MTU [DS p.84].
4. Host writes **SEND** (0x20). CR clears; the data leaves; **Sn_IR.SENDOK** is set when the command completes [DS p.72].
5. FSR recovers:
   - TCP: when the peer ACKs the data.
   - Other modes: at SENDOK [DS p.85].
   - After a UDP ARP timeout the driver notes that "Sn_IR_TIMEOUT causes the decrement of Sn_TX_FSR" [DRV socket.c:340].
6. The next SEND must wait until SENDOK is set; clear it by writing 1. Otherwise "it can cause any error" [DS p.72, 98-99]. The WIZnet `send()` waits for SENDOK before every SEND except the first [DRV socket.c:182-207]. `sendto()` waits for SENDOK or TIMEOUT after each SEND [DRV socket.c:337-349].
- **Odd lengths:** the word written for the last byte carries a pad byte that is never transmitted. Model: queue bytes; on SEND take WRSR bytes, then discard up to the next even boundary [inferred].
- **UDP broadcast** to 255.255.255.255 needs no ARP [DS p.103].

---

## 6. RX path [DS p.85-89, 96-97, 104-105, 111, 113-114]
- **Sn_RX_RSR** counts the bytes stored in RX memory (17 bits), including PACKET-INFO headers and padding, across all queued packets.
  - It **decreases by 2 on every FIFO word read** [DS p.86, and the BRDY timing on p.67].
  - RSR > 0 means at least one packet is waiting.
- **RECV** is issued after one whole packet (header + data + padding, plus the CRC for MACRAW) has been read. It frees that space and lets TCP advance its window [DS p.73, 97]. The host must process data one packet at a time [DS p.86].
- **Padding:** an odd-length packet gets one dummy byte **after** the data, so every packet starts on an even boundary.
  - The datasheet prose says to read the dummy "first", but its own example reads the data byte from FIFOR0 and the dummy from FIFOR1 [DS p.88-89].
  - The WIZnet driver reads `len + (len & 1)` bytes [DRV socket.c:244-249, 433, 483, 513].

PACKET-INFO formats. All fields are big-endian, MSB first on FIFOR0:

| Mode | PACKET-INFO | Then |
|---|---|---|
| TCP, ALIGN=0 | size(2) [DS p.96-97] | data, padded |
| TCP, ALIGN=1 | none. Read RSR bytes; only valid if every segment is even-sized [DS p.68, 95, 97] | data |
| UDP | sender IP(4), sender port(2), size(2) = 8 bytes (Fig 14 [DS p.104-105]) | data, padded |
| IPRAW | sender IP(4), size(2) = 6 bytes (Fig 16 [DS p.111]) | IP payload, padded (no IP header [inferred]) |
| MACRAW | size(2) = frame length without CRC [DS p.113-116] | frame (dst MAC, src MAC, type, payload), padded, then **CRC(4)**, which must be read and ignored [DS p.114] |

- Datasheet figures label the IP and port "destination"; the fields are really the sender's address [DS p.105].
- The PACKET-INFO length is 2 bytes for TCP and MACRAW, 8 for UDP, 6 for IPRAW [DS p.89].
- The MACRAW accounting sample confirms that the size excludes the header and CRC: `rsr -= 2 + size(+1 if odd) + 4` [DS p.115-116].

### 6.1 Check against NedoOS
- **TCP** (`w53_read_new`, `w53_read_min` [NEDOOS 599-727]):
  - Sn_MR high byte is never written, so ALIGN=0 [NEDOOS 218-219].
  - When no packet is in progress, it reads RSR low then high (bits 15:0 only). If both are 0 it checks SSR: ESTABLISHED gives EAGAIN, anything else ENOTCONN [NEDOOS 703-718]. So data left in CLOSE_WAIT is still drained before EOF.
  - Otherwise it reads the size from 0x30/0x31 (MSB first) [NEDOOS 719-727].
  - Data is read in any chunk sizes across calls, alternating 0x30 and 0x31 with INI. The next FIFO half is kept in `(ix+0)`.
  - When the packet is exhausted and a FIFOR1 read is still owed (the odd case), it reads a dummy byte from 0x31, then issues RECV [NEDOOS 690-701].
  - **This matches the datasheet**, apart from the FIFOR0/FIFOR1 split across syscalls noted in 1.2.
- **UDP/IPRAW** (`w53_rd_nontcp` [NEDOOS 498-591]):
  - If SSR is 0, it issues OPEN first.
  - It reads the IP (4), then the port (2, UDP only), then the size (2), all MSB first. This matches Fig 14 and Fig 16.
  - It reads `min(count, size)` bytes, skips the rest, then issues RECV. RECV is issued even for a partial read, which is correct because the packet is discarded.
  - **Mismatch:** when the user count is even, the datagram size is odd and larger than the count, the skip loop under-reads by one word (the last data byte plus the pad). RECV then leaves the FIFO misaligned: the next header is parsed from a data byte [NEDOOS 549-587]. The arithmetic: bytes consumed = `2*ceil(count/2) + 2*floor(skip/2)`, which should equal `2*ceil(size/2)`.
  - A model that discards the whole packet on RECV (like Unreal_NS) hides this bug; a faithful FIFO model reproduces it.
- **TX** (`wiznet_write` [NEDOOS 730-791]):
  - Checks FSR (bits 15:0) against len; if it is too small, returns EMSGSIZE and does not wait.
  - Writes ceil(len/2) pairs (0x2E then 0x2F via OUTI), then WRSR bits 15:0 = len, then SEND. It waits only for CR = 0 and **never checks or clears SENDOK** [NEDOOS 758-791], unlike [DS p.98-99].
  - The model must therefore accept back-to-back SENDs. Completing each SEND synchronously is enough.
  - len = 0 would push about 64 KB of words: IX = 0xFFFF and the loop runs until wrap [NEDOOS 771-784]. The model should cap FIFO writes at the buffer size.
  - For UDP/IPRAW it first copies the port and IP into DPORTR/DIPR with OUTI and `inc b` twice [NEDOOS 412-424], then OPENs the socket if it is closed.
- **close()** with the flag in `e` set [NEDOOS 437-443] treats **FSR bits 15:8 = 0x20** (FSR = 0x20xx) as "TX drained". It only works with the default 8 KB TMSR, and needs FSR to read back exactly 0x2000 when idle.

---

## 7. Timeouts and retransmission [DS p.57-58, 71-72, 78]
- RTR is the retry period in 100 µs units (reset 2000 = 200 ms). RCR is the retry count (reset 8). The final timeout fires after RCR+1 attempts.
- **ARPTO** = RTR × 0.1 ms × (RCR+1). With default values that is 1.8 s.
- **TCPTO**: the period doubles each retry and is capped at 65535 × 0.1 ms. With default values: (2000+4000+8000+16000+32000+4×64000) × 0.1 ms = **31.8 s**.
  - General form: `Σ_{N=0..M} RTR·2^N + (RCR−M)·RTR·2^M`, where M is the first N with RTR·2^(N+1) > 65535.
- **On timeout:**
  - Sn_IR.TIMEOUT = 1 in every mode.
  - In TCP mode SSR becomes CLOSED **at the same moment** [DS p.57-58].
  - In UDP/IPRAW mode SSR returns from ARP to UDP/IPRAW [DS p.78].
- **Failing a connect cleanly:** set SSR → CLOSED plus IR.TIMEOUT for an unreachable host. For a refusal, set SSR → CLOSED with no IR bit, as if an RST arrived.
  - NedoOS `connect` polls SSR until it reads 0x17 or 0x00 [NEDOOS 394-403]. It busy-waits in the kernel, so a host connect timeout well under 31.8 s is kinder to the guest.
  - Transient 0x01 and 0x15 are fine: any non-zero value keeps it polling.

---

## 8. Interrupts [DS p.16, 52-55, 74-75]
- **Sn_IMR** gates whether an event sets **Sn_IR** at all [DS p.74].
- IR bit n = 1 while (Sn_IR & Sn_IMR) ≠ 0. **/INT** is low while (IR & IMR) ≠ 0 [DS p.52-55].
- Clearing:
  - Sn_IR: write 1s. IR bit n clears automatically when Sn_IR becomes 0.
  - IR bits 15..12: write 1s [DS p.52].
- The driver's ISR copies Sn_IR into a software variable, then writes it back to clear it [DRV w5300.c:591-626].
- On the card, W5300 /INT is visible in 0x83AB bit 0. It reaches the Z80 only if 0x83AB bits 2 and 6 are set [RTL ports.v; PRM p.9-10]. The Z80 gets no vector (IM2 reads a random byte).
- **NedoOS does not use interrupts.** It never writes 0x83AB, IMR or Sn_IMR, and never clears Sn_IR [NEDOOS whole file]. Sn_IR therefore accumulates bits. The model still needs Sn_IR for the WIZnet-style drivers that poll SENDOK and TIMEOUT.

---

## 9. Errata that software can see [ERR]
1. **SSR stuck at 0x10/0x11:** CLOSE or DISCON issued while a TCP SEND is still pending (SENDOK not yet set, for example because the peer's window is full) can leave SSR stuck at 0x10/0x11.
   - Workaround: reopen the socket as UDP, send 1 byte to 0.0.0.1, then close. The WIZnet `close()` does this after about 100 ms of waiting for FSR to return to its maximum [DRV socket.c:36-60].
   - The model need not reproduce this.
2. **No window-update ACK:** the chip does not send a window-update ACK by itself, so throughput suffers when the RX buffer fills. The datasheet workaround is to SEND 1 dummy byte after RECV when RSR = 0 [DS p.97-98]. There is no model impact.
3. **Sn_DPORTR readback** after a TCP connect returns the high byte twice (0x1234 reads as 0x1212). Hardware stores the correct value. A model may return the written value, since the register is write-only by spec.
4. **ARP reply bug:** the chip answers an ARP from a 0.0.0.0 host with the gateway IP.
   - Workaround: keep SUBR = 0.0.0.0 and load the real mask only around CONNECT and UDP SEND. The driver's `ApplySubnet`/`ClearSubnet` do this [DRV w5300.c:148-160, socket.c:88, 334].
   - Model impact: guests may leave SUBR at 0 most of the time, so do not rely on SUBR for anything.
5. **ARP with SIPR = 0.0.0.0:** the ARP request targets the gateway instead of the peer (same root cause, same workaround). This matters for DHCP-style bootstrap. A model that routes through host sockets can ignore it.

---

## 10. Minimal faithful model for NedoOS

### 10.1 Required
- **Card:** ports 0x81AB, 0x82AB and 0x83AB with RTL decoding and mirrors, plus I/O mapping `{p81[3:0], A13..A9, A8^inv}`.
  - While 0x83AB bit 4 = 0 the chip is in reset: ignore writes, and reads are undefined (return 0xFF [inferred]). On the rising edge, apply the full chip reset.
  - The memory window and A0 inversion are needed only for software other than NedoOS.
- **Common registers:** a 256-byte store with reset values, plus IDR 0x5300 and MR.RST self-clear with a full reset. SHAR, SIPR, GAR, SUBR and TMSR must be read/write storage because `wizcfg` programs them.
- **For each socket:**
  - Sn_MR, Sn_PORTR, Sn_DPORTR, Sn_DIPR and Sn_PROTOR as storage.
  - Sn_CR that always reads back 0.
  - Sn_SSR as an explicit state machine updated by commands and by host network events. Do not compute it lazily on read.
  - Sn_IR/Sn_IMR with SENDOK, RECV, CON, DISCON and TIMEOUT.
  - TX: a byte queue sized TMSR×1024, FSR = size − queued, WRSR 17 bits, and a synchronous SEND that sets SENDOK.
  - RX: a byte FIFO holding PACKET-INFO + data + padding per packet. RSR = bytes queued, decremented per word read. RECV is a no-op beyond window credit, because the data is already consumed.
- **Commands to cover:** OPEN, CLOSE, CONNECT, LISTEN, DISCON, SEND and RECV, for TCP, UDP and IPRAW (ICMP, `SOCK_ICMP` → PROTOR = 1 [NEDOOS 238-242]).
- **LISTEN/accept semantics:** the W5300 socket in LISTEN becomes ESTABLISHED **itself**.
  - NedoOS then opens a fresh W5300 socket on the same PORTR and LISTENs again, then swaps handles [NEDOOS 262-307].
  - The host side must therefore keep a listening socket per port that outlives the accepted one, or re-listen with SO_REUSEADDR. It also needs a per-W5300-socket pending-accept queue.
- **Timing:**
  - Command completion can be instant.
  - SSR transients (ARP, SYNSENT) are optional.
  - A connect in progress must read non-zero; failure must go to 0x00.
  - Established TCP needs data and EOF → CLOSE_WAIT with the remaining RX data still readable.

### 10.2 Where Unreal_NS deviates from the datasheet (`zxusbnet.cpp`)
1. **SSR is computed lazily** with `select()` on every read [UNS 211-303], and the read has side effects:
   - It calls `accept()` [UNS 222-235] and pulls RX data (`WizRecv`) [UNS 290].
   - A TCP client in progress reads 0x13 (INIT), never 0x01 or 0x15 [UNS 250-258].
   - After a peer FIN it reads 0x00 instead of 0x1C (CLOSE_WAIT) [UNS 172-175, 212-213].
   - FIN_WAIT, TIME_WAIT and LAST_ACK never appear.
2. **CR clearing:** `read_CR` returns 0 only while a host socket exists [UNS 205-209].
   - OPEN with an invalid Sn_MR, or SEND, DISCON or CONNECT after the host socket was closed, leaves CR non-zero forever. Any `while(CR)` loop then hangs, including NedoOS `w53_cmd`.
   - The datasheet says CR always clears [DS p.70].
3. **DISCON = immediate `closesocket`** [UNS 497-503]. There is no FIN handshake visible in SSR, which goes straight to CLOSED.
4. **Common registers are read-only.** A write restores the startup value from `stat_regs` [UNS 576-578]:
   - SHAR, SIPR, GAR and SUBR are filled from the host adapter [UNS 581-623].
   - RTR/RCR read 0 instead of 0x07D0/0x08.
   - MR.RST is not emulated; IR/IMR always read 0.
   - IDR (0x0FE = 0x53) and TMSR/RMSR = 8 are correct [UNS 625-632].
5. **Sn_IR and Sn_IMR always read 0** (`read_ZERO`) [UNS 375-380]. SENDOK, TIMEOUT, CON and DISCON never set, so the WIZnet reference `send()` and `sendto()` would hang. NedoOS does not care.
6. **TX_FSR** is always computed from a fixed 8 KB buffer, not TMSR [UNS 7, 339-347]. It matches NedoOS's `FSR_H == 0x20` check only by coincidence. TX_WRSR is accepted as 17 bits [UNS 453].
7. **RX holds one packet at a time.**
   - RSR equals that one packet's padded length, not the total of queued packets [UNS 180-199].
   - **RECV throws away the whole buffered packet** regardless of how much was read [UNS 489-493]. This hides NedoOS's UDP under-read bug (section 6.1).
   - Reading the FIFO when RSR = 0 returns 0 without advancing [UNS 364].
8. **IPRAW:**
   - `setsockopt(SO_RCVTIMEO)` is called before `socket()` [UNS 412-414].
   - A Windows raw socket delivers the IP header inside the data. The W5300 delivers the payload only [inferred].
   - Raw sockets need administrator rights.
   - Otherwise the PACKET-INFO is laid out correctly [UNS 193-197].
9. **LISTEN/accept:** on the first `accept()` the host listening socket is closed and replaced by the connection [UNS 222-232]. The next NedoOS LISTEN on another W5300 socket re-binds the port, so there is a window where connections are refused. The backlog is 1 [UNS 447].
10. **Writing Sn_MR closes the host socket** [UNS 474-477]. The real chip does not close on an MR write [inferred]. This is harmless for NedoOS, which writes MR only before OPEN and after CLOSE.
11. **Card port decoding** uses exact matches for 0x81AB, 0x82AB and 0x83AB [UNS 668-679]. The RTL decodes only A15, A9:8 and A7:0.
    - On the 0x83AB bit 4 edge it re-initializes Winsock and the tables [UNS 669-671], which is reasonable.
    - W5300 I/O decoding `(port & 0x3F00) >> 8` matches the RTL (A14 ignored). The XOR on A0 matches too [UNS 562, 573, 662-667, 686-690].
12. **Sn_DPORTR** reads back the written value, without the erratum 3 effect. This is fine.
