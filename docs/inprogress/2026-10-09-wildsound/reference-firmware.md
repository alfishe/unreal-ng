# Wild Sound (AYX-32 / "Wild Sound II") firmware - emulation spec

Source tree: `~/Downloads/zx-spectrum/sprinter/peripherals/wild-sound/Wild-Sound`
(GitHub RobusAsmCoder/Wild-Sound, single commit `edd3b11`).

Path abbreviations used in citations:

| Tag | Path |
|-----|------|
| `FW/` | `STM_AYX32_TSL/STM_AYX32_TSL_WILDSOUND/SRC/` (the Wild Sound firmware) |
| `LIB/` | `STM_AYX32_TSL/SRC/` (shared Rob F. library: fifo, flash, math) |
| `XM/` | `STM_AYX32_TSL/TMT/MasonXM/` (PC converter + ZX loader) |
| `CFG/` | `STM_AYX32_TSL/TMT/ay-config/` (TSL's AYX-32 config tool, for the E0..EF protocol) |

Which build: `STM_AYX32_TSL_WILDSOUND/OBJ/STM_AYX32_TSL_WILDSOUND.hex` contains the build
string `16:59:31 Apr 23 2021` (offset 0x0800B50C), the same date as the newest firmware image
`TMT/ay-config/ayx32_fw_WS_16593123042021.trd`. The `.map` image limit `0x0800B528` matches the
hex length (46376 bytes). So the sources describe the April 2021 firmware.

There is only ONE firmware variant. `STM_AYX32_TSL/SRC` is a library directory, not a second
firmware; `STM_AYX32_TSL_WILDFIRMWARE/SRC/main.c` is a self-flashing packer that embeds the
WILDSOUND image (`STM_AYX32_TSL_WILDFIRMWARE/SRC/main.c:12-27`).

---------------------------------------------------------------------------------------------

## A. Bus protocol

### A.1 Physical model

The STM32 sees only the AY bus: BDIR (PB0), BC1 (PB1), data in on PB8..PB15, data out either
on PB8..PB15 (TSL board) or via SPI3 to an external latch ("Robus config", selected by the BOOT1
pin PB2) (`FW/HARDWARE/hardware.h:84-108`, `FW/HARDWARE/hardware.c:287-338`,
`FW/ChipAY_Interrupt.c:354`). Bus cycle decoding (`FW/ChipAY_Interrupt.h:16-29`):

| BDIR | BC1 | Meaning |
|------|-----|---------|
| 0 | 0 | idle |
| 0 | 1 | read (Z80 IN) |
| 1 | 0 | write data |
| 1 | 1 | write address (register select) |

On a ZX the tools use `OUT (#FFFD)` = address, `OUT (#BFFD)` = data, `IN (#FFFD)` = read
(`FW/ChipAY_Interrupt.c:32-36`, `XM/BOOT/XMPLOAD/XMPLOAD.AZ8:37-69`). On the Sprinter ISA card
"port 0 / port 1" are mapped onto these three cycle types by board glue that is NOT in this
repository (see G).

### A.2 The read pipeline (important for every side-effect register)

Every bus event (address write, data write, read) is handled by `EXT_DoAyBusReg()`
(`FW/ChipAY_Interrupt.c:846-924`), entered from the TIM3 capture interrupt. For a read cycle the
interrupt first drives the bus while the read is in progress (`FW/ChipAY_Interrupt.c:976-983`), then
runs the handler AFTER the read ends. At the end of every event the handler computes the byte for
the NEXT read and drives it onto the output port (`FW/ChipAY_Interrupt.c:901-913`):

```
adr <  0x10        : next = ay[ayn].REG.R[adr]
0x10 <= adr < 0x20 : next = ay[ayn].REG.R[adr]        (extension regs, see E.3)
adr >= 0x20        : next = ws.R[adr]                  (shared 256-byte "Wild Sound" area)
```

So: **a Z80 read returns the value prepared by the previous bus event; the read itself then
triggers the side effect (advance a string/frame pointer) and prepares the next byte.**
An emulator can model this as: `Read() { v = latch; OnReadSideEffect(); latch = Lookup(adr); return v; }`
and `WriteAddr/WriteData` also end with `latch = Lookup(adr)`.

Every data write first stores the byte in `ws.R[adr]` (`FW/ChipAY_Interrupt.c:875`), so a
register without a write handler reads back the last written byte. Registers 0x20..0x7F and
every 0x80..0xFF register not listed below are plain echo storage.

Per-chip state: each AY has its own address latch `ay[n].adr` (`FW/ChipAY_WildSound.h:95-99`);
`ws.R[256]` and all WORK_* fields are shared (`FW/ChipAY_WildSound.h:109-128`).
Reset values: `ayn=0`, both `adr=0`, `dta=0xFF`, `WORK_LOCK_BIT=0xFF`, `WORK_MODE=0xFF`,
`WORK_CMD=0xFF`, `WORK_STATUS=0`, `WORK_POS=0`, `WORK_ERROR=0`, `WORK_FW_SIZE=0`,
`WORK_TRANS_TYPE=ZXIOA`, `WORK_EMUL_HARDMD=0` (AY mode) (`FW/ChipAY_WildSound.c:22-37`);
`chMain()` then sets `WORK_STATUS |= 0x80` (`FW/ChipAY_Main.c:117`). `ws.R[]` and `WORK_DATA[]`
start at zero.

### A.3 Address write (register select)

`FW/ChipAY_Interrupt.c:868-873`: the written byte becomes the address; if bit 7 is set the
special handler `EXT_DoWsBusWriteAddr()` runs first; then `ay[ayn].adr = byte` (for the chip that
is selected AFTER the handler, which matters for #FE/#FF).

### A.4 Register map

#### Normal AY registers 0x00..0x0F (per chip)

Writes store the raw byte in `REG.R[n]` and call a per-register generator update
(`FW/ChipAY_Interrupt.c:881-895`, table `FW/ChipAY_Interrupt.c:178-246`). Reads return the raw
byte - **no masking** (R1 written as 0xFF reads 0xFF, unlike a real AY). R14 (port A) may be
replaced by IOA pin input when the ZX-IOA / ZX-Uno / Buryak UART bridges are active
(`FW/ChipAY_Interrupt.c:137-173`, `FW/ChipAY_WS_Process.c:85-90`); for an emulator just return
the written value. Register semantics: see E.

#### Extension registers 0x10..0x1F (per chip)

Documented in `FW/ChipAY_Interrupt.c:259-291`:

| Reg | Meaning | Implemented? |
|-----|---------|--------------|
| 0x10-0x15 | "TONE A/B/C PHASE" (12-bit) | stored only; table entry is `prcAYprcNULL` (`FW/ChipAY_Interrupt.c:196-201`), no audible effect |
| 0x16-0x17 | none | - |
| 0x18-0x1A | PWM duty of tone A/B/C ("PHASE FIX") | yes: `CCR = ARR*v>>8` (`FW/ChipAY_Hardware.h:28`, `FW/ChipAY_Interrupt.c:121-123`). Default 0x80 = 50 % (`FW/ChipAY_WildSound.c:193-195`). Value 0 means "always high" (`FW/ChipAY_Hardware.h:26`) |
| 0x1B-0x1F | none | - |

#### Plain storage 0x20..0x7F

Read/write echo of `ws.R[adr]` (no handler; `FW/ChipAY_Interrupt.c:881-912`).

#### Special registers 0x80..0xFF (shared, not per chip)

Handlers: address write `FW/ChipAY_Interrupt.c:594-687`, data write `:690-839`,
read `:497-590`.

| Reg | Addr write | Data write | Read |
|-----|-----------|-----------|------|
| 0xE0 DEV_SIG | `POS=0`; latch = 0x55 (`:598-601`) | echo | returns latched byte, then latch next byte of 0xAA55 LE -> 55 AA 55 AA ... (`:502-504`) |
| 0xE1 CMD/STATUS | latch = WORK_STATUS (`:602-604`) | `WORK_CMD=b`, run `EXT_DoAyxCmd`, latch = STATUS (`:696-700`) | latch STATUS again (`:505-507`). STATUS normally 0x80 (S_BOOT, `FW/ChipAY_Main.c:117`, also `FW/ChipAY_Hardware.c:333-347`) |
| 0xE2 ERROR | latch = WORK_ERROR (`:605-607`) | echo | echo of latch (no refresh on read) |
| 0xE3 PARAM | `POS=0` (`:608-610`) | `WORK_DATA[POS++ & 31] = b` (`:701-703`) | echo |
| 0xE4 DATA | `POS=0; ERROR=E_BUSY(3)` (`:611-614`) | firmware upload byte into CCM RAM 0x10000000 while `POS < WORK_FW_SIZE`; `ERROR=E_DONE` at end, `E_DATAERR` on overflow (`:704-715`) | echo |
| 0xEB UPTIME | snapshot ms counter u32 into WORK_DATA, latch byte 0 (`:615-619`) | echo | next WORK_DATA byte (`:508-510`) |
| 0xEC VER | WORK_DATA = 3 x u16 `WILDfwVer[0..2]`, latch byte 0 (`:620-626`) | echo | next byte (`:511-513`) |
| 0xED CPR_STR | latch first char of "Wild Sound II By Rob F." (`:627-630`, string `:357`) | echo | next char, stops on 0 (`:514-516`) |
| 0xEE BLD_STR | latch first char of build string "HH:MM:SS DD.MM.20YY" (`:631-634`) | echo | next char (`:517-519`) |
| 0xEF CORE_FRQ | WORK_DATA = SYSCLK u32 (`:635-639`) | echo | next byte (`:520-522`) |
| 0xF0 UNLOCK/VERSION | lock + reset (`:640-646`) | unlock state machine (`:716-743`) | if unlocked: next char of version string (`:523-525`) |
| 0xF1 STREAM CMD | `POS=0`, status snapshot, latch frame byte 0 (`:647-651`) | command bytes (`:744-756`) | status frame, switches adr to 0xF3 (`:526-531`) |
| 0xF2 STREAM DATA | `POS=0` (`:652-654`) | packet bytes (`:757-779`) | echo (no read path) |
| 0xF3 STREAM STATUS | `POS=0`, snapshot, latch frame byte 0 (`:655-659`) | echo | next frame byte (`:529-531`) |
| 0xF4 | latch 3 (`:660-662`) | send byte to UART (`:780-782`) | UART RX byte with 0x03 "empty" protocol (`:532-578`) |
| 0xF5 | `WORK_MODE=0xFF` (`:663-665`) | UART/bridge mode select (`:783-830`) | echo of WORK_MODE |
| 0xF6 | latch = UART RX count (`:666-668`) | `WORK_MODE=0xFF` (`:831-833`) | next RX byte while count>0 (`:579-585`) |
| 0xF7 | latch = TX free (`:669-671`) | send byte, latch TX free (`:834-837`) | TX free (`:586-588`) |
| 0xFC, 0xFD | nothing (`:672-674`) | echo | echo |
| 0xFE | select chip 1 (`ayn=1`) (`:675-678`) | echo | echo |
| 0xFF | select chip 0 (`ayn=0`) (`:679-682`) | echo | echo |

Everything else in 0x80..0xFF (including TSL's 0xD0 PSG_SEL etc., `CFG/src/defs.h:105-112`) is
echo storage only.

#### TurboSound-style chip select

Only 2 AYs exist (`tbl_AYREGPER_proc[64]` covers chips 0/1, `FW/ChipAY_Interrupt.h:86-88`; init
of ay[0], ay[1] only in `FW/main.c:103-104`), although `ay[4]` is allocated
(`FW/ChipAY_WildSound.h:113`). Writing ADDRESS 0xFE selects chip 1, 0xFF selects chip 0. Because
`ay[ayn].adr = byte` runs after the switch (`FW/ChipAY_Interrupt.c:870-871`), the newly selected
chip's own register latch becomes 0xFE/0xFF; software must then select a real register. The other
chip's latch is preserved.

### A.5 Unlock state machine (#F0)

String `LOCKSTRING = "Wild Sound Enable "` (18 chars, trailing space) (`FW/ChipAY_Interrupt.c:356`).

1. Address write 0xF0: `WORK_MODE=0xFF, WORK_CMD=0, LOCK_BIT=0xFF (locked), WORK_POS=0`
   (`FW/ChipAY_Interrupt.c:640-646`).
2. Data writes while `LOCKSTRING[POS] != 0`: `LOCK_BIT=0xFF`; byte matches -> `POS++`, otherwise
   `POS=0` (no re-sync on a mismatching 'W') (`:717-726`).
3. String complete, `LOCK_BIT==0xFF`: next byte -> `WORK_CMD` (the "mode" byte; nothing else
   uses it in this firmware), `LOCK_BIT=0xFE` (`:728-731`).
4. Next byte: `0x00` -> unlocked (`LOCK_BIT=0`), `ws.R[F0]=0xDF`, `POS=0`; non-zero ->
   `LOCK_BIT=0xFF` (back to step 3, POS stays at end of string) (`:733-740`).
5. Reads of 0xF0: locked -> echo of the last byte written to F0 (or 0xDF from an earlier unlock);
   unlocked -> first read returns 0xDF, each read then latches the next char of `WILDwsVER`;
   at the terminating 0 the pointer stops, so reads return 0x00 forever (`:427-435`, `:523-525`).

The unlock gates ONLY the version-string read. Streams (#F1..#F3), TSL regs and AY regs work
without it. The ZX loader `XMPLOAD.AZ8` never unlocks (`XM/BOOT/XMPLOAD/XMPLOAD.AZ8:116-499`);
the detector `XM/BOOT/DETECT.AZ8:70-118` does: select F0, send string, cmd byte, 0x00, read,
compare with 0xDF, then read up to 32 bytes until 0.

### A.6 Stream command register (#F1) - write format

`FW/ChipAY_Interrupt.c:744-756`. After address write 0xF1 (`POS=0`), data bytes go to
`WORK_DATA[POS++ & 31]`:

* byte 0,1: command u16 LE. When the 2nd byte arrives: `cayStrmClear()` (both FIFOs emptied,
  `CMD=NOP`, `flag=0`, i.e. busy and error cleared) then `CMD=cmd, STP=0`
  (`FW/ChipAY_StreamData.c:41-45, 60-66`).
* bytes 2..5: SIZE u32 LE. When the 6th byte arrives: `SIZE=value, POS(stream)=0`.
* Sending only 2 bytes is legal (XMPLOAD does it for NOP/CLEAR/ERASE/DIGPLAY,
  `XMPLOAD.AZ8:117-127`); SIZE/POS then keep stale values.

Writing the command does NOT start it. The command's state machine runs only while the
`DODONE` ("busy") flag is set, and that flag is set by a #F2 packet (A.7). To run a command with
no data, send an empty packet (count 0).

### A.7 Stream data (#F2) - packets and flow control

`FW/ChipAY_Interrupt.c:757-779`. After address write 0xF2 (`POS=0`):

* If busy (`DODONE` set) every write is ignored (`:758`).
* First byte = packet length N (`WORK_FW_SIZE=N`, shared with the TSL upload counter).
* Next N bytes are appended to the 128-byte OUT FIFO; a byte is silently DROPPED if the FIFO is
  full (`:770`). FIFO capacity is 127 (`rb_fifo_free = size_mask - size`, `LIB/rb_fifo.c:266-274`,
  size 128 `FW/ChipAY_StreamData.h:12`).
* When the N-th byte arrives (or immediately if N=0): `DODONE` set (busy), `POS=0` -> the next
  byte written is a new length byte (`:773-777`).

Flow control used by the reference loader (`XMPLOAD.AZ8:286-320`): wait until not busy, read
`fiOUTfree` (frame byte 10), send `min(fiOUTfree, remaining)` bytes as one packet, wait not busy,
repeat. If fiOUTfree were 0 the loader would send a 0-length packet, which some commands treat
as "end of data" (B).

Reading 0xF2 has no handler (echo). `fiINsize` (FIFO IN) is never written by anything
(`grep FifoINwrB` - only its own definition `FW/ChipAY_StreamData.c:80-83`), so it is always 0:
there is no device-to-host data channel in this firmware.

Errors: there is no error reporting on the bus. `cays_FLAG_ERROR` exists but is never set or
reported (`FW/ChipAY_StreamData.h:17-18`, `FW/ChipAY_StreamData.c:288`). Upload errors only
print "TR Error"/"INH Error"/"INS Error" on the debug UART (`FW/ChipAY_StreamData.c:357-362`).
The Z80 can detect them only by comparing frame POS with SIZE.

### A.8 Status frame (#F3, also readable via #F1)

Builder `EXT_DoWsStreamStatus()` (`FW/ChipAY_Interrupt.c:462-475`) writes into the 32-byte
`WORK_DATA` buffer (`FW/ChipAY_WildSound.h:124`):

| Offset | Size | Content |
|--------|------|---------|
| 0 | 2 | `CMD & 0x7FFF`, bit 15 = busy (`DODONE`) |
| 2 | 4 | SIZE (u32 LE) |
| 6 | 4 | POS (u32 LE) - bytes consumed by the current command |
| 10 | 1 | fiOUTfree = free bytes in OUT FIFO (0..127, 127 when idle) |
| 11 | 1 | fiINsize = 0 always |
| 12..28 | 17 | NOT written by the builder: whatever `WORK_DATA` held (0 after reset; #F1 writes touch only 0..5, #E3/#EB/#EC/#EF can leave data there) |
| 29 | 2 | marker 0x3DFA LE -> bytes FA 3D (`:473`, `ws-3` with ws=32) |
| 31 | 1 | crcX of bytes 0..30 |

The source comment "cmd x2 + size x4 + pos x4 + fiOUTfree + fiINsize + 0x3DFA + crcX + 0x00"
(`:526`) describes a 16-byte layout and is wrong for the code; the Z80 loader checks bytes 29/30/31
of a 32-byte read (`XMPLOAD.AZ8:186-214`), matching the code.

Read sequencing (`EXT_DoWsStreamGetStatusByte`, `FW/ChipAY_Interrupt.c:477-494`): `WORK_POS` low
byte = index p, high byte = running CRC r.

* p != 31: return `WORK_DATA[p]`, `r = crcstep(b, r, p)`, `p++`.
* p == 31: `WORK_DATA[31] = r`, take a NEW snapshot (builder), `WORK_POS=0`, return r.

Combined with the read pipeline:

* Address write 0xF3: `POS=0`, snapshot, latch = byte 0, POS=1 (`:655-659`). Reads then return
  byte 0, 1, ..., 31 (CRC), then the next snapshot's byte 0, ... (frames repeat every 32 reads).
* Address write 0xF1: `POS=0`, snapshot, latch = byte 0 (POS stays 0) (`:647-651`). The first
  read returns byte 0; the read handler switches `adr` to 0xF3, consumes byte 0 and latches byte 1
  (`:526-531`, deliberate fall-through). Net effect: same frame as #F3.
* Reading right after writing command bytes to #F1 (POS=6, no new snapshot) returns the
  last written byte, then stale WORK_DATA bytes - always re-select #F3 before reading status.

crcX step (`FW/ChipAY_Interrupt.c:437-447`), all u8 arithmetic, start r=0, p=byte index:

```
p&3 == 0: r ^= (b + p)
p&3 == 1: r ^= (b - p)
p&3 == 2: r -= (b + p)
p&3 == 3: r += (b - p)
```

(Z80 twin: `XMPLOAD.AZ8:155-179`.)

### A.9 TSL AYX-32 commands (via #E3 + #E1)

`EXT_DoAyxCmd()` (`FW/ChipAY_Interrupt.c:373-396`) takes the u32 param from `WORK_DATA[0..3]`:

| Cmd | Name | Action |
|-----|------|--------|
| 0xE8 | UP_FW | `STATUS |= DRQ(2)`, `WORK_FW_SIZE = param` (note: u16 field) |
| 0xE9 | FLASH_FW | if param == 0x7841AA55: `STATUS |= BUSY`, `ERROR = 0xAA` -> main loop reflashes from CCM RAM (`FW/ChipAY_WS_Process.c:17-24`, `FW/ChipAY_BootFlasher.c:56-98`) |
| 0xEB | RESET | if param == 0xDEADBEEF: MCU reset |
| 0xE4 LOCK, 0xEA SAVE_CFG, 0x00 BREAK | defined (`FW/ChipAY_Interrupt.h:31-38`) but not implemented |

Magic constants: `FW/ChipAY_Interrupt.c:364-368` (same as TSL's `CFG/src/defs.h:160-168`).
An emulator can ignore firmware upload; implementing E0/E1/EC/ED/EE is enough for TSL-style
detection.

### A.10 Hardware reset

AY RESET line (PA2) falling edge -> wait for release -> `NVIC_SystemReset()`
(`FW/ChipAY_Interrupt.c:1080-1087`, `:1332-1338`). Everything is reinitialised, including the
allocator descriptor table: uploaded XM data stays in flash physically but is forgotten (B.1).

---------------------------------------------------------------------------------------------

## B. Stream commands

Constants: `FW/ChipAY_StreamData.h:16-27`. State machine: `cayStrmPool()`
(`FW/ChipAY_StreamData.c:290-533`), run from the main loop (`FW/process.c:140-145`,
`FW/ChipAY_WS_Process.c:96-99`) only while busy. Each pool call executes one step. `mStepStop()`
clears busy (`:285-288`).

### B.1 Storage: the memory allocator

* `memalInit(mema_FLAG_ROMRAM, 64)` (`FW/main.c:78`): **64 descriptors** total
  (`FW/MemoryAllocator.c:407-439`).
* All XM data is allocated with `mema_FLAG_ROM`, i.e. in the STM32's internal **FLASH** after the
  firmware image, rounded up to the next flash sector (`FW/MemoryAllocator.c:411-423`). The image
  ends at 0x0800B528 (`STM_AYX32_TSL_WILDSOUND.map` "Load$$LR$$LR_IROM$$Limit"), so storage is
  0x0800C000..0x08100000 = **999,424 bytes** (1 MB STM32F405, scatter file
  `STM_AYX32_TSL_WILDSOUND.sct`).
* Allocation is a bump allocator (`FW/MemoryAllocator.c:251-291`). Flash writes are AND-writes
  (half-word programming with 0xFF padding, `LIB/BootFlasherF4.c:168-203`); allocated flash is NOT
  cleared (`FW/MemoryAllocator.c:280-283`). Only ERASE resets the allocator and erases non-blank
  16 KB blocks (whole sectors) (`FW/MemoryAllocator.c:386-403`, `LIB/BootFlasherF4.c:124-149`).
* Descriptor budget: 1 (track) + 1 (instrument table) + 1 (blank instrument 0) + 1 per XM
  instrument -> **at most 61 instruments**. On exhaustion `memalMemDescAllocate` returns an error
  code that the instrument code does not check (`FW/ChipAY_DigitalSample.c:19-21`) -> garbage
  pointers. (DARKNESS.XM with 128 instruments would break.)
* Per-instrument allocation = `16 (tInstrumentInfo) + 8 + Length + 24` bytes
  (`FW/ChipAY_DigitalSample.c:16`).

### B.2 Commands

| Cmd | Name | Data expected | Behaviour |
|-----|------|---------------|-----------|
| 0x0000 | NOP | none (0-length packet) | logs "NOP", done (`:296-301`) |
| 0x0001 | CLEAR | none | AY mode (stops XM playback), `cayStrmClear()` -> CMD reads back 0 afterwards (`:302-310`) |
| 0x0002 | ERASE | none | AY mode, free all descriptors, erase flash storage, done after 4 steps (`:311-332`). On hardware this blocks for the duration of the flash erase (sector erase is ~1-2 s per 128 KB sector) and busy stays set meanwhile |
| 0x0010 | XMAST | track pack (C.1), SIZE = total pack size | step 0 (first packet): allocate SIZE bytes, set parser TRACK (`:336-347`); step 1: if FIFO has data go to step 2, else flush cache and finish ("TR Done" if POS==SIZE) (`:348-366`); step 2: move all FIFO bytes through the cache into flash, back to step 1, clear busy (`:367-377`). So: N data packets, then a 0-length packet finalises |
| 0x0011 | XMADO | - | empty case: immediately "default" -> STP=0, done (no-op) (`:379-383`) |
| 0x0020 | XMBST | instrument table (C.3), SIZE = table bytes | allocate SIZE+21, parser INSH (`:383-425`); same packet/finalise protocol as XMAST. The parser allocates each instrument's sample block in flash as records arrive |
| 0x0021 | XMBDO | sample bytes (C.4), SIZE = bytes in this command | no allocation; each byte goes to `cayStrmCacheDescMemParsingINSDATA` (`:247-269`) which routes it by the GLOBAL sample pointer `SmpsPTR`; POS++ per byte (`:429-473`). SmpsPTR survives across XMBDO commands (reset only by XMBST, `:214`), so samples may be sent as many XMBDO commands (the loader uses one per 32 KB chunk) |
| 0x0030 | DIGPLAY | none | `cayDigPlayerInit(&digPlayer, AyFreqSND)`, log header, one player pool, switch DAC source to XM (`WORK_EMUL_HARDMD = DIGITAL`), done (`:478-526`). Playback then runs forever in the main loop (`FW/ChipAY_WS_Process.c:101-110`; `cayDigPlayerPool` always returns 0, `FW/ChipAY_DigitalPlayer.c:663-701`). Stop = CLEAR or ERASE |
| other | - | - | done immediately (`:528-530`) |

Required order (as in `XMPLOAD.AZ8:350-362, 391-499`): NOP, CLEAR, ERASE, XMAST (+packets, final
0-length packet), XMBST (+packets, 0-length), XMBDO per chunk (+packets, 0-length), DIGPLAY
(0-length). There is no validation: DIGPLAY without a track dereferences a null `pPAT`; a new XMAST
while playing repoints `pPAT` at not-yet-written flash.

### B.3 What the TRACK parser does to the bytes (`FW/ChipAY_StreamData.c:157-201`)

Called before each byte is cached; `p` = stream position of the byte about to be stored.

* p==0: `pPAT = descriptor address`; p==2: flush (so bytes 0..1 are written separately);
  p==4: sets bit 15 of the u16 formed by bytes 2..3 of the `Size` field (i.e. `Size |= 0x80000000`)
  - a marker, the player never reads `Size`.
* p < 292: `PatNum = 255`.
* p >= 292 (pattern records, 10 bytes each): at record offset 4 (OFS complete) the OFS u32 is
  replaced by `OFS + base address` (relative -> absolute pointer). The FIRST record's OFS also
  defines `PatNum = (OFS0 - 292) / 10`. So PatREC[0].OFS MUST equal the header size
  (292 + 10*PatNum).

### B.4 What the INSH parser does (`FW/ChipAY_StreamData.c:203-245`)

* p==0: `InsNum = (SIZE+21)/21` (= records + 1), `pINS` = descriptor address, `SmpsPTR=0`; a
  blank 16-byte instrument (all samples 0x80 -> stored as 0x01) is created and written as record 0
  (`FW/ChipAY_DigitalSample.c:64-79`). So **firmware instrument k = uploaded record k-1 = XM
  instrument k** (XM numbering is 1-based; 0 = "no instrument").
* When byte 20 (last) of a record is about to be stored, the cached first 20 bytes are fixed up:
  `OFS -= (InsNum-1)*21` (= minus the table size, C.3), then `cayDigSmpAllocateInstrument()`
  allocates the sample block and the OFS field is replaced by the block address
  (`tInstrumentInfo*`). The original OFS is kept in `InsInfo->OFS`.

---------------------------------------------------------------------------------------------

## C. Uploaded data formats (and how MasonXM produces them)

All little-endian, packed. Firmware structs: `FW/ChipAY_DigitalPlayer.h:15-53`,
`FW/ChipAY_DigitalSample.h:24-46`; converter structs `XM/RobXM.pas:162-199`.

### C.1 Track pack (`.xmPatPack`) - `tPatternsPacked`

| Offset | Type | Field | Source in converter (`XM/RobXM.pas:1293-1302`) |
|--------|------|-------|------|
| 0 | u32 | Size = bytes of pattern data only (excl. header) | `PatternsPacked.SIZE` |
| 4 | char[20] | TrackName (XM song name, or 2nd CLI arg) | `XM.Info.Name` / `XM/MasonXM.pas:403-410` |
| 24 | u16 | Channels (firmware max 32, `FW/ChipAY_DigitalPlayer.h:11`) | XM header |
| 26 | u16 | TMP = initial speed (ticks/row) | XM "default tempo" |
| 28 | u16 | BPM | XM "default BPM" |
| 30 | u16 | FreqTable = raw XM flags (bit0: 1 = linear) | XM header |
| 32 | u16 | PatLen = song length (order count) | XM header |
| 34 | u16 | PatRes = restart position | XM header |
| 36 | u8[256] | PatOrder | XM header |
| 292 | tPatternRec[PatNum] | {u32 OFS, u32 SIZE, u16 Rows} = 10 bytes | `XM/RobXM.pas:1329-1331` |
| 292+10*PatNum | bytes | pattern data | raw XM packed pattern bytes, copied verbatim (`XM/RobXM.pas:1333`) |

* PatNum = (highest pattern index with SIZE != 0) + 1 (`XM/RobXM.pas:1154-1165`).
* Only patterns referenced in the order list are stored (`XM/RobXM.pas:1311-1319`); others get
  SIZE=0, Rows=0, OFS=0.
* OFS is relative to the start of the pack (header included): `PatternsPackChangeOFS(+header)`
  (`XM/RobXM.pas:1265-1269`). Records with SIZE=0 are not relocated.
* Pattern data is the XM packing: per channel, byte b; if bit 7 clear, b is the note and 4 bytes
  follow (ins, vol, eff, efv); else bits 0..4 say which of note/ins/vol/eff/efv follow
  (`FW/ChipAY_DigitalPlayer.c:70-90`, `XM/RobXM.pas:1337-1354`).
* Edge cases to avoid in a new converter: an XM pattern with PackSize=0 (empty) that is used in
  the order list gets SIZE=0 and an unrelocated OFS -> firmware reads garbage; emit an explicit
  packed empty pattern (Channels x 0x80 per row) instead. Make sure PatREC[0].OFS = header size
  even if pattern 0 is unused (B.3).

Chunking for the ZX loader (`SaveAuto*`, `XM/RobXM.pas:1167-1276`): files `<name>.xmPatPack00`,
`01`, ... of max 32768 bytes; chunk 00 starts with a 4-byte u32 = total pack size, then the pack
bytes. Verified: `Prosiak.xmPatPack` = 19107 bytes, `Prosiak.xmPatPack00` = 19111 bytes starting
`A3 4A 00 00` (= 19107). The loader reads that prefix as the XMAST SIZE and streams the rest
(`XMPLOAD.AZ8:391-446`). Header check on Prosiak: Size 0x4849, "Prosiak", Channels 4, TMP 2,
BPM 125, FreqTable 0, PatLen 44, PatRes 0, PatREC[0].OFS = 0x25A = 602 = 292 + 31*10.

### C.2 'XM A' vs 'XM B'

The command names XMAST/XMADO/XMBST/XMBDO mean: "A" = track/pattern stream, "B" = instrument
stream; "ST" = start (header/table), "DO" = data. XMADO is unused (no-op). XMBST carries the
instrument table, XMBDO the sample bytes (`FW/ChipAY_StreamData.c:333-473`, log strings "XMST",
"XINH", "XINS").

### C.3 Instrument table (`.xmInsPack00`) - `tInstrumentRec`, 21 bytes

| Offset | Type | Field |
|--------|------|-------|
| 0 | u32 | OFS = 21*NumInstruments + offset of this sample inside the sample stream |
| 4 | u32 | Length (bytes, 8-bit samples) |
| 8 | u32 | Loop (start) |
| 12 | u32 | LoopEnd (absolute end, NOT XM loop length) |
| 16 | u8 | Volume 0..64 |
| 17 | s8 | FineTune |
| 18 | u8 | SType (raw XM sample type byte) |
| 19 | u8 | Panning 0..255 |
| 20 | s8 | RelativeNote |

`XM/RobXM.pas:1088-1151`: one record per XM instrument (N = `NumInstruments`), using only
**sample 0** of each instrument (`M:=0`, `:1102-1112`); instruments with no samples get an
all-zero record. Table size = 21*N; the `.xmInsPack` file = table + sample stream; `.xmInsPack00`
= table only; `.xmInsPack01..` = sample stream in 32 KB chunks. Verified on Prosiak:
`xmInsPack00` = 441 = 21*21; record 0 OFS 0x1B9 = 441, Length 3310, Loop 0x198, LoopEnd 3310,
Vol 64, FT 0, SType 1, Pan 0x80; record 1 OFS = 441+3310.

Firmware use: firmware fixes `SType` 0/other -> `Loop = LoopEnd` (no loop), 1 forward, 2
ping-pong (`FW/ChipAY_DigitalSample.c:25-33`). Any value other than 1 or 2 (e.g. 0x11 for a
16-bit looped sample, because the converter passes the raw type byte) means "no loop".

### C.4 Sample stream (`.xmInsPack01..`) - 8-bit unsigned

Per XM instrument with samples, `Length` bytes, concatenated in instrument order. Conversion in
`LoadSampleData` (`XM/RobXM.pas:674-1051`, called with SCONV=3 at `:1517-1522`):

1. delta-decode bytewise (`:704-715`) - also for 16-bit samples, which is a converter bug (no
   carry between bytes);
2. 16-bit: keep the high byte, halve Size/Loop/LoopLen (`:720-734`);
3. XOR 0x80 -> unsigned (0x80 = silence) (`:737-746`);
4. replace 0x00 by 0x01 (`:748-760`) (0 is the firmware's loop marker);
5. loop: type 0/3 -> Loop = Length, LoopLen = 0; type 1 unchanged; type 2 (ping-pong) appends a
   reversed copy of the loop minus its last byte, drops the post-loop tail, `Loop += 1`, Length =
   new size (`:762-811`);
6. clamp `Loop+LoopLen <= Length`, `LoopEnd = Loop + LoopLen` (`:1038-1041`).

Firmware storage per instrument (`FW/ChipAY_DigitalSample.c:13-62`):
`[tInstrumentInfo 16][8 x 0][sample 0..Loop-1][8 x 0][Loop..LoopEnd-1][8 x 0][LoopEnd..Length-1][8 x 0]`,
`SMPDATA` points after the first 8 zeros. Each byte is stored as `b ^ 0x80` (back to signed), and
a stored 0 becomes 1. The zero runs are end/loop markers for the mixer (D.4).

Routing (`FW/ChipAY_StreamData.c:247-269`): for global byte index p, the instrument is the
highest index i whose `InsInfo->OFS <= p` (OFS after the "-table size" fix-up); bytes past that
instrument's Length are dropped (`FW/ChipAY_DigitalSample.c:52`).

### C.5 Writing a Python converter (equivalent output)

```
pack = header(292) + PatREC[PatNum]*10 + concatenated XM packed pattern data (order-used patterns)
  PatREC[i].OFS = 292 + 10*PatNum + offset_in_data   (also for unused/empty ones: point anywhere valid)
  header.Size = len(pattern data); FreqTable = XM flags; TrackName = 20 bytes, zero padded
XMAST SIZE = len(pack)

instab = for each XM instrument (1..N): 21-byte record of sample 0 (zeros if no sample)
  OFS = 21*N + offset_of_this_sample_in_stream
smp stream = per instrument sample 0: delta-decode (8-bit) or proper 16-bit decode -> 8-bit
  signed, XOR 0x80, 0x00 -> 0x01; Loop/LoopEnd absolute; SType = type & 3 (MasonXM passes the
  raw byte, see C.3); no-loop: Loop = LoopEnd = Length
XMBST SIZE = 21*N ; XMBDO SIZE = len(chunk), any chunking
Limits: Channels <= 32, instruments <= 61, total (pack + 21*(N+1) + sum(Length+48)) <= ~999 KB
```

---------------------------------------------------------------------------------------------

## D. The XM player (`FW/ChipAY_DigitalPlayer.c`)

### D.1 Timing

* Output rate `DevFreq = AyFreqSND = 1774400/32 = 55450` Hz (`FW/ChipAY_WildSound.h:11-12`,
  `FW/ChipAY_StreamData.c:480`). The DAC timer TIM4 actually runs at
  `TIMclk/floor(TIMclk/55450)`; with SYSCLK 220 MHz (`FW/HARDWARE/hardware.c:342`, `:72-73`,
  APB1 = HCLK/2 so TIM clock 220 MHz, `:108-112`) that is 220e6/3968 = ~55443.5 Hz
  (`FW/ChipAY_Hardware.c:96-124, 426`). Assumes a 16 MHz HSE.
* Tick length: `DevTacts = DevFreq*60 / (BPM*24)` samples, integer, recomputed every tick
  (`FW/ChipAY_DigitalPlayer.c:643`) -> 1108 samples at BPM 125.
* Speed: `TIK = TMP` per row, decremented each tick, new row when `TIK < 1`
  (`:304-372`). `TIKI` = tick index inside the row (0 on the row tick).
* Order handling: at end of pattern `POS++`, wrap to `PatRes` (`:309-325`). The player never
  stops (`SongEND` is computed but ignored; `cayDigPlayerPool` returns 0, `:663-701`).
* Row fetch: `cayDigPlayerExtractLine` decodes one row for all channels (`:58-98`).

### D.2 Effects implemented (`XM_Player_STEP`, `:287-626`)

"Row tick" below = the tick where `RelativePRES` reaches 0 (tick 0, or the ED-delayed tick);
"other ticks" = all ticks where it is non-zero.

| XM | Implemented as | Notes / deviations from FT2 |
|----|----------------|------------------------------|
| note + instrument | row tick: `Vol = ins.Volume`; if note and EFF != 3: FineTune, RelativeTone, Ins, retrigger (`:386-404`) | instrument without note only resets volume; no instrument envelopes, fadeout, auto-vibrato, sample map (only sample 0) |
| note without instrument | retrigger with last instrument (`:405-411`) | |
| note 97 (key-off) | **not special-cased**: treated as note 97 -> retriggers a high-pitched note | bug |
| 0xy arpeggio | `TIKI % 3` -> +0/+x/+y semitones (`:440-447`) | applied whenever EFF=0 |
| 1xx porta up | other ticks: `PeriodPortamento += xx` (`:450-452`) | period changes by 2*xx per tick (FT2 Amiga: 4*xx); no effect memory |
| 2xx porta down | other ticks: `-= xx` (`:454-456`) | same |
| 3xx tone porta | other ticks, custom algorithm comparing frequencies (`:459-488`); note with 3xx sets the target (`:417-422`) | no memory (300 does nothing) |
| 9xx sample offset | if note present: start at xx*256 (`:491-496`, `:653`) | no memory |
| Axy volume slide | other ticks: up x if x != 0 else down y (`:503-513`) | no memory |
| Bxx position jump | row tick: `POS = xx`, then the row fetch does `POS++` (`:516-526`, `:312`) | **jumps to order xx+1** (bug) |
| Cxx set volume | `:498-500`, clamp 0..64 (`:580-581`) | |
| Dxy pattern break | row tick: next order, row x*10+y (`:529-540`) | |
| E1x / E2x fine porta | row tick (`:543-549`) | |
| E6x pattern loop | row tick (`:551-568`) | one global loop counter |
| EAx / EBx fine volume slide | row tick (`:571-577`) | |
| EDx note delay | `RelativePRES = x+1` (`:361-366`) | ED0 never triggers the note (FT2 plays it) |
| Fxx speed/BPM | at row fetch: <0x20 speed else BPM (`:337-344`) | F00 sets speed 0 = new row every tick |
| effect 0x17 | odd remap: `x=1` -> EFF 0x18 (nothing); `x>1` -> EFF 0 with EFV &= 0x0F -> acts as arpeggio (`:345-358`) | quirk |
| volume column 0x10..0x50 | set volume (`:429-432`) | all other volume-column commands ignored |

Not implemented: 4 vibrato, 5, 6, 7 tremolo, 8 set panning, E3, E4, E5, E7, E8, E9 retrigger,
EC note cut, EE pattern delay, Gxx/Hxy global volume (GlobalVol fixed 64, `:733`), Kxx key-off,
Lxx, Pxy, Rxy, Txy, X1x/X2x, all instrument envelopes (EnvelopeVol fixed 64, `:724`), fadeout,
vibrato, volume-column slides/vibrato/panning/porta.

### D.3 Pitch

`PerFrCalc()` (`:220-273`). The linear branch is taken only if `FreqTable == 10`, which never
happens (XM flags are 0/1, the converter stores them raw), so **every module uses the Amiga
branch**:

```
Note = PatternNote + RelativeTone - 12
tofs = (Note % 12)*8 + FineTune/16      (clamped to 0..95)
Period = PeriodTab[tofs] * 16 / 2^(Note/12) - PeriodPortamento*2   (clamped 1..65535)
Freq = 8363*1712 / Period
step = Freq/DevFreq in 32.32 fixed point (cayDigSmpCalcFreq, FW/ChipAY_DigitalSample.c:83-89)
```

C-4 (note 49) with RelativeTone 0, FineTune 0 -> 8363 Hz. Quirks: the "interpolation" uses the
same table entry twice (no fine-tune interpolation, `:256-265`); `PeriodTab[63]` is typed as
`59075` instead of 575 (`:227`) - G with FineTune 112..127 is badly detuned. Pascal reference
(`XM/RobXM.pas:1697-1745`) has the correct table and a real linear branch for `FreqTable=1`.

### D.4 Mixer (`cayDigSmpGenerateBUF`, `FW/ChipAY_DigitalSample.c:100-168`; pool `FW/ChipAY_DigitalPlayer.c:663-701`)

* Channels = header Channels (<= 32), each mixed every sample, **no interpolation** (nearest,
  position advanced before the read, `:123-125`).
* Per channel volume: `FinalVol = Vol*64*255/4096` (0..255) (`FW/ChipAY_DigitalPlayer.c:589-597`);
  `MulFactor = 256 / Channels` (integer); `VoL = VoR = FinalVol*MulFactor >> 8`.
* Panning: instrument `Panning` only (no 8xx): pan < 0x80 -> `VoR *= 2*pan/256`, else
  `VoL *= 2*(255-pan)/256` (`:110-115`). Additionally, for non-linear (Amiga) modules channels
  0..3 get fixed stereo: ch0 L1.0/R0.4, ch1 L1.0/R0.9, ch2 L0.9/R1.0, ch3 L0.4/R1.0
  (`FW/ChipAY_DigitalPlayer.c:599-621`); channels >= 4 are centre.
* Sample value: signed 8-bit (stored form) * Vo -> s16, summed into a stereo s16 pair:
  `buf[0] += s*VoR`, `buf[1] += s*VoL` (`:161-164`).
* Loops: hitting a stored 0 byte triggers wrap logic (`:126-160`): end of a forward loop -> loop
  start + overshoot; ping-pong flips the step sign; no-loop samples park on the trailing zeros
  (silence). Entering the loop region for the first time from a non-zero loop start jumps to
  near the loop END for one sample, then wraps (artefact of the `dir` logic; ~1 sample glitch).
  `SysABS` lacks parentheses, so `SysABS(of) % RepLen` is `of<0 ? -of : of % RepLen`
  (`LIB/SysMath.h:8`).
* Ring buffer of 2048 stereo samples, refilled 256 at a time (`FW/ChipAY_DigitalPlayer.h:11-13`).

### D.5 DAC and AY/XM mixing

`ChipAY_DacStep()` runs in the TIM4 interrupt at the output rate (`FW/ChipAY_Hardware.c:277-326,
349-356`) and selects ONE source by `WORK_EMUL_HARDMD`:

* mode 0 (AY, default): `ChipAY_Mixer()` output;
* mode 3 (DIGITAL): next XM ring-buffer sample (`:314-318`);
* modes 1/2 (software AY) exist but nothing on the bus selects them.

**XM and AY are never mixed**: while an XM plays, AY register writes are accepted but inaudible;
CLEAR/ERASE switch back to AY. Output: `value ^ 0x8000` written to `DAC->DHR12L1/2` by DMA, i.e.
the top 12 bits of an offset-binary 16-bit value, two channels (PA4 = DAC1 gets the low half-word,
PA5 = DAC2 the high one) (`FW/ChipAY_Hardware.c:321-322, 361-412`). Note: in AY mode the low
half-word is the "L" sum (channel A heavy), in XM mode the low half-word is the `VoR` sum, so a
left-panned XM sample comes out on the DAC that carries AY channel C. Which physical side is
which depends on the board.

---------------------------------------------------------------------------------------------

## E. AY emulation in the firmware

* **2 chips** (A.4), clock **1774400 Hz** fixed (`FW/ChipAY_WildSound.h:11`).
* Default "hardware timer" mode: each tone/noise/envelope generator is an STM32 timer
  (`FW/ChipAY_Hardware.h:113-129`): tone timers clocked so that f = 1774400/(16*TP), PSC =
  TP-1 with TP=0 treated as 1 (`FW/ChipAY_Interrupt.c:85-95`); noise period 0 -> 1 (`:108-112`);
  envelope period 0 -> treated like 1 (`:98-104`); R13 write restarts the envelope counter
  (`:120`). The mixer samples the timers at 55450 Hz (`FW/ChipAY_WildSound.c:215-325`) - no
  band-limiting, so it aliases.
* Noise: a new random bit from the hardware RNG every time the noise timer's PWM state changes
  (`FW/ChipAY_WildSound.c:262-263`) - not the AY 17-bit LFSR.
* Envelope: 32 steps per timer period, looked up as `EnvRndTAB[256 + step*16 + shape]`
  (`FW/ChipAY_WildSound.c:267-269, 131-148`), i.e. only the "repeat" segments 1 and 2 of the table.
  Consequence: one-shot shapes 0-7, 9, 15 are ALWAYS 0 in this mode (their attack segment 0 is never
  used); 8/A/C/E repeat correctly; B/D hold 15. This looks like a firmware bug; the software mode
  (`FW/ChipAY.c:93-150`) does use segment 0 but is unreachable.
* Mixer rule: standard `(tone | Tdis) & (noise | Ndis)`, volume bit 4 -> envelope
  (`FW/ChipAY_WildSound.c:200-211`).
* Volume table (16 levels, log, `volType=1`, max 8192): 0, .0078, .0110, .0156, .0220, .0312,
  .0441, .0624, .0883, .1250, .1515, .2500, .3030, .5000, .7070, 1.0 (`FW/ChipAY_WildSound.c:43-74`).
* Stereo: fixed ABC with balance A = L100/R20, B = L68/R68, C = L20/R100 percent
  (`FW/ChipAY_WildSound.c:46-51, 75-105`). No ACB/mono option in this firmware.
* Two-chip sum: `out = chip1 - chip0` per side (chip 0 phase-inverted) (`FW/ChipAY_WildSound.c:303-313`).
* Extension: PWM duty regs 0x18-0x1A per chip (A.4); "phase" regs 0x10-0x15 inert.
* Modes named "AY", "Phase"/PWM, "Synthesia", "Digital" in other documentation: this source
  has only `work_hrdEmul_mode_AY_HARDWARE_TIMER / AY_SOFT / AY_SOFTODD / AY_DIGITAL`
  (`FW/ChipAY_WildSound.h:102-107`); "Synthesia" and a Mason AY player do not exist here
  (`grep -i mason|synthes` finds only the converter name and a comment). "MasonXM" is the PC
  converter/ZX boot-disk builder, not a firmware player.
* Other features: UART bridge #F4-#F7 (host UART, baud select 4800..115200 via #F5 0x80..0x86,
  0x90), bit-banged UARTs on AY port A for ZX-Uno (#F5=0xAA), Buryak (0xAB), raw IOA (0xAC)
  (`FW/ChipAY_Interrupt.c:780-837`, `FW/ChipAY_WS_Process.c:26-94`); TSL-protocol firmware update
  E8/E4/E9 (A.9); debug log on UART at 115200 (`FW/main.c:74-76`). All are reachable from the bus
  but not needed for a Sprinter emulation (return 0/"empty" for #F4/#F6, 0 for #F7 free).

---------------------------------------------------------------------------------------------

## F. Version / identification

* After the #F0 unlock: 0xDF, then `WILDwsVER` = `"HHMMSS.DDMM20YY"` + 0x00, built from
  `__TIME__`/`__DATE__` (`FW/ChipAY_Interrupt.c:358-360, 1145-1173, 1422-1428`). For the shipped
  build: **"165931.23042021"** (matches `ayx32_fw_WS_16593123042021.trd`).
* #EE build string: `"16:59:31 23.04.2021"` (same function with separators).
* #ED: `"Wild Sound II By Rob F."` + 0.
* #EC: 6 bytes = 3 x u16 LE: `10000+YY*100+MM`, `10000+DD*100+hh`, `10000+mm*100+ss`
  (`FW/ChipAY_Interrupt.c:1422-1425`) -> 12104, 12316, 15931 (0x2F48, 0x301C, 0x3E3B) for the
  shipped build. The source labels them HARD VER / FRMW VER / CONF VER (`:622-624`).
* #E0: 55 AA 55 AA ... (signature 0xAA55, TSL compatible).
* #EF: SYSCLK in Hz (u32 LE), #EB: uptime ms (u32 LE).

---------------------------------------------------------------------------------------------

## G. ISA / Sprinter variant

No occurrence of "Sprinter", "ISA" (as a word), "DSS" or a Sprinter EXE in any source, Pascal,
.AZ8, .CMD or text file of the repository (only a Pascal variable named `ISA` in
`TMT/BININC/PROC/BINTOHEX.PAS:10`). `LOAEXE.AZ8` is a ZX loader stage, not a DSS EXE. The only
board-level variation is BOOT1 (PB2): TSL/AYX-32 parallel output vs "Robus config" with SPI3
data latch, inverted BDIR/BC1 sampling and falling-edge capture (`FW/ChipAY_Interrupt.c:1053-1070,
1384, 1404-1407`; `FW/HARDWARE/hardware.h:84-102`). The Sprinter card is presumably one of these
with ISA glue logic in front; its port decoding, read port, and DAC L/R wiring are not documented
here. Schematics present: TSL AYX-32, Buryak, Karabas - no ISA board (`DOC/SCHEMATIC/`).
The Sprinter player `prosiak.exe` is not in the repo; its described sequence matches
`XMPLOAD.AZ8` + `DETECT.AZ8` exactly.

---------------------------------------------------------------------------------------------

## H. S3M / MOD / AY formats

* Firmware: XM only (the uploaded pack). No S3M/MOD/AY/PT3/VTX parser in `FW/`.
* Tools: `RobXM.pas` reads XM only (`XM/RobXM.pas:1391-1546`). `XM/DOC/Micromod.pas` and
  `DOC/XM/MicroMod/micromod-master.zip` are third-party ProTracker MOD replayer references, not
  wired into anything. `XM/DOC/XM.TXT`, `XI.TXT`, FT2 effect PDFs are documentation.
* No built-in AY music player that takes a file stream: AY music must be played by the Z80
  writing registers.

---------------------------------------------------------------------------------------------

## I. Test music in the repo (`XM/Music/`, plus `XM/MMM/PROSIAK.XM` identical to `Music/PROSIAK.XM`)

All are FastTracker 2 XMs, 8-bit samples only. "Converted" = `.xmPatPack/.xmInsPack` and their
numbered chunks present; complete ZX TR-DOS disks with loader for the converted ones are in
`XM/BOOT/LINKER/{BALLADAL,checkno,CONSUL,DISCHARZ,FASTER-2,prosiak}.trd`.

| File | Bytes | Ch | Pat | Ins | Freq | Speed/BPM | Converted | Notes |
|------|------:|---:|----:|----:|------|-----------|-----------|-------|
| BALLADAL.XM | 55,548 | 4 | 21 | 13 | Amiga | 5/115 | yes (Pat 11,166; Ins 41,233 = 273 + 32768 + 8192) | uses 4, 6, E9 (unsupported) |
| CHECKNO1.XM | 144,765 | 4 | 27 | 26 | Amiga | 7/125 | yes, as `checkno1.*` (Pat 19,436; Ins 117,084) | heavy ED (901), 4/5/6, EE |
| CHECKNO2.XM | 144,490 | 4 | 27 | 26 | Amiga | 6/125 | no | |
| CONSUL.XM | 181,771 | 4 | 23 | 31 | Amiga | 3/125 | yes, as `Consul.*` (Pat 13,049; Ins 160,769) | only 1, 2, A, C, F - best-supported test |
| DARKNESS.XM | 498,936 | 22 | 38 | 128 | linear | 3/163 | no | 128 instruments > 61 limit, key-offs, envelopes |
| DISCHARZ.XM | 359,957 | 10 | 34 | 15 | Amiga | 3/125 | yes (Pat 50,206 in 2 chunks; Ins 305,511 = 315 + 9x32768 + 10284) | 3xx heavy, key-offs |
| EFF00.XM | 126,097 | 4 | 3 | 26 | linear | 2/32 | no | effect test (3xx) |
| FASTER-2.XM | 132,457 | 8 | 14 | 12 | Amiga | 4/125 | yes (Pat 18,103; Ins 118,810) | 1 ping-pong sample |
| PROSIAK.XM | 177,815 | 4 | 31 | 21 | Amiga | 2/125 | yes, as `Prosiak.*` (Pat 19,107; Ins 154,123 = 441 + 4x32768 + 22610) | the prosiak.exe tune; uses 8xx, B, E6, EE |
| PROSIAK0.XM | 57,245 | 4 | 31 | 21 | Amiga | 4/125 | no | short variant |
| PROSIAK1.XM | 177,421 | 4 | 31 | 21 | Amiga | 3/125 | no | variant |
| PROSIAK.XM.AutoSave.20120124.054206.xm | 179,680 | 4 | 31 | 21 | Amiga | 2/125 | no | OpenMPT autosave |
| Sleep.xm | 243,112 | 4 | 14 | 60 | Amiga | 8/124 | no | 60 instruments (just under the limit) |
| STINK-B_.XM | 115,443 | 4 | 41 | 23 | Amiga | 2/125 | no | |
| TEXS-3.XM | 98,981 | 4 | 35 | 16 | Amiga | 3/125 | no | |
| TREXZ.XM | 263,644 | 10 | 51 | 34 | linear | 5/211 | no | 1 multi-sample instrument, ping-pong |

(Header and effect statistics extracted with a throw-away parser over the XM files.)

---------------------------------------------------------------------------------------------

## Open questions

1. Sprinter ISA glue: which Z80 port and cycle produce BDIR/BC1 "read", and whether the data
   port read returns the latch (A.2). Not in this repo.
2. Which firmware build the Sprinter cards carry (this source = 2021-04-23).
3. DAC1/DAC2 to left/right wiring on the ISA card; the XM mixer and the AY mixer disagree (D.5).
4. Busy timing: real ERASE (seconds) and flash-write time per packet; an emulator can complete
   commands instantly (the reference loader polls busy), or model delays.
5. Whether to replicate firmware bugs (one-shot envelopes silent, Bxx off-by-one, key-off as a note,
   59075 period, ED0, no effect memory) or emulate correct XM/AY behaviour. Bit-exact emulation of
   the card means replicating them.
6. The flash keeps old data across resets but the allocator forgets it; a real card needs ERASE
   before every upload. Whether an emulator should persist uploads across sessions is a design
   choice.
