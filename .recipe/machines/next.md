# Recipe: ZX Spectrum Next

The Next is a Z80N (Z80 + NEXTREG, MUL, ...) at 3.5 / 7 / 14 / 28 MHz with 2 MB RAM, a 256-register control file ("NextREG"),
layered video (ULA, Layer 2, tilemap, 128 sprites, a copper), three AY chips, a DMA, a DivMMC with an SD card, and a Multiface-style
NMI. Its software is NextZXOS, loaded by the firmware (`TBBLUE.FW`) from the SD card; the emulator runs that real chain.

Ground truth: [docs/inprogress/2026-10-07-zx-next/](../../docs/inprogress/2026-10-07-zx-next/README.md) (`design.md`,
`TODO.md` for the status, `design-nextreg-journal.md` for the register journal), the port decoder
[portdecoder_next.h](../../core/src/emulator/ports/models/portdecoder_next.h), the reports
[portdecoder_next_state.cpp](../../core/src/emulator/ports/models/portdecoder_next_state.cpp). Scripts:
[tools/machines/next/](../../tools/machines/next/README.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred; the [WebAPI](#webapi) section has the same steps as curl and
> as the Python scripts under `tools/machines/next/`. Policy: [_common/transports.md](../_common/transports.md).

Outputs below are real, from a build of 2026-10-09 (sections 3b: 2026-10-10), trimmed.

## MCP (preferred)

### 1. Create the machine on a card

The machine boots the real chain only when the model's config names the boot ROM and a card (folder or image):
`configs/next/unreal.ini` - `[ROM] NEXTBOOT=rom/next/nextboot.rom` and `[NEXT] SdCard=<folder>`. A card folder needs `TBBLUE.FW`,
`machines/next/*` (the personality ROMs, `menu.def`, `keymap.bin`), `nextzxos/`, `sys/`, `dot/` - about 3 MB (the repository's
`testdata/machines/zxnext/card` is a starting point; the full NextZXOS distribution works as it is). Without them the machine
starts in the bare personality (128K ROM, no SD).

**RAM size.** `ram_size` 1024 (Issue 1) or 2048 (Issue 2, the default); they are what `GET /emulator/models` lists and the menus offer. 4096 (Issue 3)
is accepted by the API only: the core (3.02.03) addresses 2 MB - an 8-bit MMU page of 8 KB, `#E0` and above the ROM - so the machine is created with
2048 and the reply carries a `note` saying so (requirements.md Q4). On a 1 MB machine an MMU bank beyond the installed RAM is not wired: its slot
reads as `unmapped` (`state/next/mmu`), a write to it goes nowhere. `POST /emulator/start {"model":"NEXT","ram_size":1024}`.

The card is the media manager's slot `sd.next0` (alias `sd`; `sd.next1` is the second socket): `[NEXT] SdCard` only names the card put into an empty slot at the power-on and at every reset, and every
insert, swap, rescan or eject is `media {"action":"insert","slot":"sd","path":"<folder or image>"}` (`rescan` re-reads a changed folder; the machine need not be recreated).

```text
emulator_manage {"action":"create","model":"NEXT","ram_power_on":"zero"}
#   → {"id":"dbd6edec-...","message":"Emulator created and started"}
```

NextZXOS reaches its menu **about 6 seconds** after the reset at normal speed (measured: `reset` to the key-wait loop `#0C8F`
5.6 s). Do not enable `turbo`: it only makes the picture stutter.

```text
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/next"}
#   → machine {"cpu_clock_hz":28000000,"speed_ratio":8,"machine_type":3,"timing":"+3", ...}, divmmc {...}, mmu {...}, spi {...}
```

NextZXOS runs its menus, Browser and editor at **28 MHz** (the guide says so: "the operating system runs at the maximum speed when
possible"); the "3.5MHz >" in its menu is the speed it gives user programs. The status bar shows the real clock.

### 2. Drive the menu and the Browser

The keyboard tools press real keys. SPACE leaves the welcome page, `B` opens the Browser, `H` searches by name, ENTER opens a
directory or runs the file under the cursor, EDIT goes up, BREAK backs out. Wait for the machine to be idle between keys (the
CPU halted at `#0C8F`: `inspect_state registers` shows `halted: true, pc: 3215`).

```text
type_input {"action":"tap","key":"space","frames":6}
type_input {"action":"tap","key":"b","frames":6}
type_input {"action":"tap","key":"h","frames":6}
type_input {"action":"type","text":"tests","delay_frames":5}
type_input {"action":"tap","key":"enter","frames":6}      # accept the search
type_input {"action":"tap","key":"enter","frames":6}      # open the directory
```

`tools/machines/next/browser-drive/drive.py` does this with the waits: `drive.py boot space browser go:tests go:base go:copper enter`.

### 3. Who wrote this register? (the NextREG journal)

The `NEXTREG` instruction makes no port cycle; the port trace and port breakpoints show it as the pair `OUT #243B,reg` /
`OUT #253B,value` at the instruction's pc (`decode_rule` 252: no bus cycle). The copper writes registers with no CPU part at all: only
the NextREG journal sees its writes, and it sees every write with its source. It is off by default.

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/next/reg-journal","body":{"enabled":true,"clear":true}}
#   → {"available":true,"capacity":65536,"enabled":true,"evicted":0,"last_seq":0,"size":0}
... run the program ...
inspect_state {"aspects":["next_reg_journal"],"nr_journal_regs":"02,07","nr_journal_limit":6}
#   → [next_reg_journal] on, 3371 held, 0 evicted
#       #4 f1214 pc 0x3DBD nextreg NR0x07 0x00 -> 0x03  (CPU speed 28 MHz)
#       #422 f1217 pc 0x099C port NR0x07 0x03 -> 0x00  (CPU speed 3.5 MHz)
#       #2308 f1222 pc 0x0AB6 port NR0x02 0x00 -> 0x08  (multiface NMI)
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/next/reg-journal?regs=02,03&sources=nextreg,port&limit=20"}
```

`source` says the door: `nextreg` (the instruction), `port` (`OUT #253B`), `copper`, `internal`. The same report is `state next
journal` on the CLI, `next_reg_journal{...}` in Lua, `emu.next_reg_journal(...)` in Python.

### 3a. What did the program ask the card? (the SD journal)

The port trace shows `#E7` / `#EB` bytes, not the SD protocol. The SD journal turns them into commands: CMD, argument, the R1 the
card answered, and every block read or written, with frame, T and the PC that asked. Off by default.

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/next/sd-journal","body":{"enabled":true,"clear":true}}
... run the program (a directory listing, an NEX load) ...
inspect_state {"aspects":["next_sd"],"nr_sd_kinds":"command","nr_sd_limit":10}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/next/sd?kinds=read,write&card=0&limit=20"}
```

`cards` says what the program saw: `type` (`SDHC` on a Next whatever the size; a game that prints "SDHC OR BETTER REQUIRED" got `SDSC` or
no answer to CMD58), the protocol `state` (`ready` after ACMD41), the last command; `spi` the engine (`too_fast_accesses` counts the
accesses inside the 16-clock byte time). A command line is `#<seq> frame <n> T <t> PC <pc> card <0|1> CMD17 0x00000800 -> R1 0x00`;
`R1 none` = the card ignored it (before CMD0, or while streaming). CLI `state next sd [on|off|clear]`, Lua `next_sd{...}`, Python
`emu.next_sd(...)`. The text above is the one `NextSdJournalMachine_Test.TheCliPrintsTheCardsAndTheJournal` checks.

The bus analyzer shows the same bus as `next.spi` (every select and byte too, and the checks: `early_access` is an
access inside the 16-clock byte time, the accesses `too_fast_accesses` counts): [analysis/bus-analyzer.md](../analysis/bus-analyzer.md).

### 3a2. Why is the DivMMC in, and which esxDOS calls were made? (the DivMMC journal)

`state/next` shows `divmmc.mapped` now; the DivMMC journal shows what put it there. Off by default.

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/next/divmmc-journal","body":{"enabled":true,"clear":true}}
... run the program ...
inspect_state {"aspects":["next_divmmc"],"nr_dm_kinds":"esx_call","nr_dm_limit":20}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/next/divmmc?kinds=map_in,map_out,retn&limit=30"}
```

`entries` is NR `#B8`-`#BB` decoded: which addresses map the DivMMC in, whether only with the 48K ROM paged (`rom3_only`) and whether
the opcode of that fetch already comes from the DivMMC (`instant`). `esx_call` is an entry at `#0008`: the byte after `RST 8`, its
name from the firmware's `esxapi.def`, the return address and the registers the program set; `basic_rom: true` means the 48K ROM was
paged, where `RST 8` is a BASIC error report and the byte an error code, not an esxDOS call. A latch that never goes out (no
`map_out` / `retn` after a `map_in`) is the usual reason a program "hangs in the DivMMC". The same report is `state next divmmc` on the
CLI, `next_divmmc{...}` in Lua, `emu.next_divmmc(...)` in Python. The text is the one
`NextDivMmcJournalMachine_Test.TheCliPrintsTheEntriesAndTheCalls` checks.

### 3a3. Stop at the write of NR x, at a beam place, at a copper instruction (breakpoints)

The journal says who wrote; a breakpoint stops the machine there. NextREG, beam and copper-instruction breakpoints are ordinary
breakpoints (`bp_add`, `POST /breakpoints`, CLI, Lua, Python), set in debug mode only; with none of them the machine pays nothing for them.

```text
control_execution {"action":"bp_add","type":"nextreg_write","address":"0x07"}                              # any door
control_execution {"action":"bp_add","type":"nextreg_write","address":"0x40","address_end":"0x44","doors":"copper"}  # the copper touches the palette
control_execution {"action":"bp_add","type":"nextreg_write","address":"0x15","value":1,"value_mask":1}     # a write that sets bit 0
control_execution {"action":"bp_add","type":"beam","address":99,"hc":255}                                  # where the line interrupt of line 100 fires
control_execution {"action":"bp_add","type":"copper","address":12}                                         # the copper completes list instruction 12
control_execution {"action":"wait"}
```

The CPU's doors (`NEXTREG`, `OUT #253B`, `IN #253B` for reads) stop after the instruction that did it, before the register takes the write.
The copper runs with the machine (after every instruction, and before every CPU NextREG write), whether or not a picture is drawn. A copper
instruction or a copper write under a breakpoint stops the machine BEFORE the MOVE writes (the copper holds it; it writes when the machine runs
on, without a second stop or count); a beam place stops at the first instruction boundary after the beam passed it. The `beam` line and the
`hc` are the copper's counters (NR #64 applied); `next_reg_journal` carries `beam_line` / `beam_hc` for every write, in the order of time.

### 3a4. Which interrupt did the CPU take, why did the M1 button do nothing? (the NMI report)

`state/next/nmi` answers "would the M1 / DRIVE button fire now and what blocks it", the Multiface and its session, the NMI in service, the
reset bits and, with the journal on (off by default), every interrupt the CPU accepted with its source and vector.

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/next/nmi-journal","body":{"enabled":true,"clear":true}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/next/nextreg","body":{"reg":"02","value":"08"}}   # the M1 button (NR #02 bit 3)
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/nmi","body":{"magic":true}}                      # the magic button: the DRIVE button on a Next (a plain POST /nmi is the bare line)
inspect_state {"aspects":["next_nmi"],"nr_nmi_kinds":"int_ack,nmi_request","nr_nmi_limit":20}
#   → [next_nmi] NMI in service: no; sources: m1_button blocked (...) drive_button ready io_trap blocked (the trap is off (NR #D8 bit 0)); multiface off +3, memory out; ...
#       #1 f512 pc 0x3DBD int_ack ula
#       #2 f512 pc 0x0038 nmi_request m1_button REFUSED: the M1 button is off (NR #06 bit 3)
```

`int_ack` has the source (`line`, `ula`, `ctc0`..`ctc7`, `uart0_rx` ...; `spurious` for an acknowledge nothing asked for) and, in hardware IM2
mode, the vector; pulse mode carries `#FF` on the bus and no vector. `nmi_request.accepted` false has `reason`: the button's enable bit in NR
`#06`, CONMEM (port `#E3` bit 7), a DivMMC handler still running, an open Multiface session. `retn.source` says which session the RETN ended
(`multiface`, `divmmc`, `none`). `reset` is `soft` or `hard`, with NR `#02` bits 1:0 after it (`power_on`, `first_reset_after_power_on`,
`later_reset`). The +3 FDC I/O trap (NR `#D8`-`#DA`) is listed as an NMI source but is not modelled. CLI `state next nmi`; Lua
`next_nmi{...}`; Python `emu.next_nmi(...)`.

### 3a5. Why is there no sound, why does the joystick do nothing? (audio and input)

`state/next/audio` shows the chips as the program set them: the model (YM or AY), ABC or ACB, turbosound, the selected chip, each chip's pan
(a chip panned `muted` is silent), its 16 registers with the tone, noise and envelope periods and the volumes (bit 4: the envelope drives
the channel), and the four DAC channels with the left (A + B) and right (C + D) sums (`#80` is silence). `state/next/input` shows NR `#05`
decoded for the left and right joystick (Kempston 1 / 2, MD 1 / 2, Sinclair, cursor, I/O mode) and which port each answers, the NR `#0B`
I/O mode, and the Kempston port `#1F` now. `kempston_1f.selected_by_nr_05` false means `zxnext.vhd` would not drive `#1F` for these modes while
the port idles at `#FF` (`answers_here`), as it does if NR `#82` bit 6 is off; `#37` answers for Kempston 2 / MD 2 (`kempston_37`, NR `#82` bit 7). The host joystick is the left connector. The extended keys (`extended_keys`, NR `#B0` / `#B1`: EXTEND UP CAPS LOCK GRAPH TRUE/INV VIDEO BREAK EDIT ; " , . DELETE and the arrows) come from the host's physical PC keys as the Next's keyboard controller maps them (Left Alt = EXTEND, Right Alt = GRAPH, Backspace = DELETE, Esc = BREAK, ` = EDIT, Tab = TRUE VIDEO, \ = INV VIDEO); the pad's MODE X Y Z (`md_pad_extra`, NR `#B2`) from keypad 7 / 9 / 1 / 3 on the left connector. Both are injectable from every plane through the keyboard API with the PC key names (`keyboard/press {"key":"kp_3"}`, `backspace`, `lalt`; `GET /keyboard/keys` lists them): journaled like a host key. The right connector has no input source. A, C and START are bits 6, 5, 7 of the Kempston byte (MD modes only). CLI
`state next audio` / `state next input`, Lua `next_audio()` / `next_input()`, Python `emu.next_audio()` / `emu.next_input()`, MCP aspects
`next_audio` / `next_input`.

### 3a5b. The UARTs: the ESP and the Pi (D1)

The Next has two UARTs on `#133B` (Tx / status), `#143B` (Rx / prescaler low), `#153B` (select + prescaler high) and `#163B` (frame),
one per line: UART 0 is the ESP module's, UART 1 the Raspberry Pi's. Each has a 64-byte Tx FIFO, a 512-byte Rx FIFO (a byte carries its
error flag), a 17-bit prescaler in 28 MHz clocks per bit (default 243 = 115200 baud), hardware flow control (RTS drops two bytes
before the FIFO is full; Tx waits for CTS) and interrupts (NR `#C6`: Rx on a byte or only at 3/4 full, Tx on an empty FIFO; the
requests are in NR `#CA`). `state/next/uart` shows all of it without clearing the sticky error bits a status read clears. What sits on
each line is a serial peer, as on every machine with a COM port: `[NETWORK] ComPort=` is the ESP's line and `ComPort2=` the Pi's
(`NONE | LOOPBACK | PLUG | TCP:host:port | SERIAL:device | AT | ESPNET | MODEM ...`; at run time `POST /network/config
{"com_port": "...", "com_port2": "..."}`). With nothing on a line the Tx bytes are lost and nothing arrives. `LOOPBACK` on a line
echoes every byte, a handy check of a program's UART code. NR `#A0`'s Pi GPIO routing is a register only (UART 1 talks to its peer
whatever NR `#A0` says).

**The UART on the joystick connector.** NR `#0B` bit 7 (I/O mode on) with bit 5 (UART mode) takes a UART to the connector: bit 0
picks UART 0 (the ESP's) or UART 1 (the Pi's), bit 4 the connector. What is plugged in is `[NETWORK] ComPortJoy=` (same values as
`ComPort=`; `{"com_port_joy": "loopback"}` at run time). The UART's own module is cut off (nothing reaches it, its ready-to-receive pin
reads not ready) until NR `#0B` lets go or a reset; the connector has no RTS pin, so a joystick device is never held off by a full
FIFO. `next_uart` shows `via_joystick_connector` per channel and `routing.joystick_port_uart` / `joystick_port_peer`.

**Break.** A peer holding its line low (`LoopbackPeer` after `SetBreak`, a PLUG that echoes the program's own break, set with frame
bit 6) is seen as the VHDL's receiver sees it: the byte is dropped and status bit 6 (framing, sticky) is set when the stop bit's
sample point passes, status bit 7 (break, held, not sticky) eight bit times after that until the line rises; a pulse that ends
before the stop sample is a byte of zero bits (`$F0` for a pulse of four bit times), a FIFO reset pauses the receiver until the line
is high. Status bit 4 (Tx empty) reads 0 while frame bit 6 (transmitter break) or 7 (reset) is set.

**The ESP module.** A Next ships with its ESP-12 on UART 0, so `ComPort` is `AT` unless the config says otherwise (`ComPort=NONE`
takes the module off): an ESP8266 with Espressif's AT firmware (NonOS AT 1.7.4; `[NETWORK] EspChip=ESP8266-AT222` picks AT 2.2.2),
115200 baud, boot banner and `ready` on the Rx FIFO at start, sockets through the virtual network. The program's baud is
`28 MHz / prescaler`, told to the module as the nearest standard rate (243 is 115200), so `AT+UART_CUR` and a prescaler change work as on the
board. NR `#A8` / `#A9` drive the module's GPIO0 (`#A8` bit 0 enables the output, `#A9` bit 0 is its level, reset 1): with it low the
module's next boot (`AT+RST`) is the ROM's download mode and it stops answering AT (`state/network` and `next_uart` show the peer; the
flasher's protocol itself is not emulated).

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/network/config","body":{"com_port":"loopback"}}
inspect_state {"aspects":["next_uart"]}          # CLI: state next uart, Lua next_uart(), Python emu.next_uart()
```

### 3a6. The program's console and the magic breakpoint (G9)

A program under test can print through the **magic debug port**: `POST /next/magic-port {"enabled":true,"port":255,"mode":"line"}` and the bytes the
program writes with `OUT (C),A` (BC = the port, an exact 16-bit address) come back from `state/next/magic-port` as text (modes hex, dec, ascii,
line). Off by default, one branch per OUT when off; a tool's own write never counts. For the **magic breakpoint** of other emulators add a
breakpoint of type `magic_ed_ff` (jnext, ZEsarUX) or `magic_dd_01` (CSpect): the opcode runs as on the hardware (NOP / `LD BC,nn`) and the
machine stops after it. CLI `state next magicport`, `bmagic`; Lua `next_magic_port()`, `bp_magic()`; Python the same; MCP aspect `next_magic_port`.

### 3b. What did the program configure? (the debugger reports)

Guessing from port traces and one register at a time found the DMA writing into ROM, the tilemap reading bank 7 as 16K and a card
that answers as SDSC. Seven reports show the state at once; **none of them has a side effect on the machine** (reading the DMA does
not step its read sequence, reading a palette does not move the index, a register read does not touch the select latch).

```text
inspect_state {"aspects":["next_dma","next_video","next_palette","next_ports","next_nextreg"],
               "nr_palette":"sprites_1","nr_range":"0-3","nr_port":"6B","nr_access":"w","nr_reg":"07"}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/next/dma"}                  # also /video /palette /ports /nextreg
```

CLI (`state next ...`, `next dma` is the same), Lua `next_dma()`, Python `emu.next_dma()`. The outputs below are real: they are produced by
the core test `CliNextReports_Test.RecipeStory` (`UNREAL_PRINT_NEXT_RECIPE=1 core-tests --gtest_filter=CliNextReports_Test.RecipeStory`) -
a program that copies 256 bytes from `#8000` into the ROM at `#0100` through the DMA, puts the tilemap in bank 7, and a disabled port.

**`next_dma`** - the copy into the ROM is visible as `dst_kind: rom` before the first byte moves:

```text
$ state next dma
mode: zxn                  # the port the last access used: #6B = zxn, #0B = z80
enabled: false             # WR6 ENABLE not given yet
burst: continuous
status: 0x3A               # the byte the next read of the read sequence returns
a:  address 0x8000, type memory, step inc, timing 1
b:  address 0x0100, type memory, step inc, timing 1
direction: a_to_b
block_length: 256
counter: 0
src: 0x8000   (src_kind: ram)
dst: 0x0100   (dst_kind: rom)       # <- the past find: a transfer into ROM
holds_bus: false    dma_delay: false
interrupt_enables: nr_cc 0x00, nr_cd 0x00, nr_ce 0x00
```

**`next_video`** - layer order, ULA / Layer 2 / tilemap / sprites switches, clip windows, raster. The tilemap base `#A0` is bank 7 at 8K
offset `#2000`: the video side of bank 7 is an 8K RAM, so the map is read from `#0000` of the bank (`map_wraps_8k: true`):

```text
$ state next video          (excerpt)
layer_order: value 2, name SUL, raw_nr_15 0x08
ula: enabled true, mode standard, port_ff 0x00, clip 0,255,0,191
layer2: enabled false, resolution 256x192x8, bank 8, shadow_bank 11
tilemap: enabled true, columns 40, control 0xA0, map_base 0xA0, map_bank 7, map_offset 0x2000, map_wraps_8k true,
         tile_base 0x0C, tile_bank 5, tile_offset 0x0C00, transparent_index 15
raster: frame 0, frame_t 3, tstates_per_line 228, vc 0, hc 6, int_line 0, current_line 239
timing: family 128K, hz 50
```

**`next_palette`** - the eight 9-bit palettes (`ula_1 layer2_1 sprites_1 tilemap_1 ula_2 layer2_2 sprites_2 tilemap_2`), the selected one
(NR `#43`), the index, the transparent indexes; `palette=` and `range=` choose:

```text
$ state next palette sprites_1 0-3
Palette ula_1 selected, index 0, auto-increment on
transparent: global 0xE3, sprites 0xE3, tilemap 15, fallback 0xE3

[2] sprites_1
  0	0x000  R0 G0 B0
  1	0x003  R0 G0 B3
  2	0x005  R0 G0 B5
  3	0x007  R0 G0 B7
```

**`next_ports`** - which device answers a port, and whether NR `#82`-`#85` switch it off. `enforced` tells whether the emulator gates the
port by that bit: a write to a port whose bit is off is ignored and a read is the idle bus `#FF`; with the expansion bus on (NR `#80` bit 7)
NR `#86`-`#89` are ANDed in. Every port the Next decodes is gated (DAC, Multiface, `#E3`, `#E7` / `#EB`, `#FF`, `#7FFD` / `#DFFD` / `#1FFD`,
DMA, joystick, I2C, sprites, `#123B`, AY, CTC, the UARTs, the Kempston mouse `#FADF` / `#FBDF` / `#FFDF`, ULA+ `#BF3B` / `#FF3B`); `#EFF7` and the +3 floating bus are not decoded at all yet:

```text
$ state next ports 6B w          (after NR #82 <- #DF)
Port 0x006B write: DMA (zxnDMA)
  decoded by: A7:A0 = #6B
  enabled: off (NR 0x82 bit 5, gated by the emulator: off)
  side effect: the next byte of the DMA's WR0-WR6 sequence; the zxn mode latch is set

Internal port enable word 0xFFFFFFDF (NR #82-#85; restored by soft reset)
  bit 0  NR0x82  on  #FF  (not gated)
  ...
```

**`next_nextreg` and the write** - one register with decoded bits, no side effects; the write goes through the board's single write choke
point, so the journal sees it. `door` is `nextreg` (the `NEXTREG` instruction: the select latch stays), `port` (`OUT #243B`, `OUT #253B`) or `internal`:

```text
$ next nextreg 07 03                       # POST /api/v1/emulator/{id}/next/nextreg {"reg":"07","value":"03","door":"nextreg"}
NR #07 <- #03 (was #00, reads #33) through nextreg (stopped)
$ state next nextreg 07
reg: 0x07   name: CPU Speed   access: RW   value: 0x33   stored: 0x03   reset: 0x00   decoded: CPU speed 28 MHz
$ state next journal regs=07
NextREG journal on: 1 event(s) of 1 held, 0 evicted
#1 frame 0 T 3 PC 0x0000 nextreg NR0x07 0x00 -> 0x03  CPU Speed (CPU speed 28 MHz)
```

The write runs where nothing else drives the machine (paused, or between two frames of a running one) and answers 409 on a machine that is
not a Next. The JSON of every report is the same tree as the text (`GET .../state/next/dma` and so on; the OpenAPI manifest lists the
query parameters).

**`next_copper`** - the copper list as a disassembly. `pc` and the `>` marker show where the copper is; the write address is the NR `#60`-`#63`
pointer and a report never steps it:

```text
$ state next copper count=3
Copper stopped (mode 0), write address 6 (word 3, msb next), pc 0, line offset 0, 3 word(s) in the list
> 0	0x9864  WAIT line 100 hpos 12
  1	0x1505  MOVE NR #15 <- #05  (Sprite and Layers System)
  2	0xFFFF  HALT
```

**`next_sprites`** - the sprite attributes decoded (a relative sprite is resolved against its anchor), the switches, the collision flags (the
report does not clear them, a port `#303B` read does) and the pattern memory in use:

```text
$ state next sprites
Sprites off, over border off, 1 visible; collision off, too many off
#0 basic x 64 y 64 pattern 0 palette +0 scale 1x1  [40 40 00 80 00]
pattern memory: 256 non-zero byte(s) of 16384, 1 8-bit pattern(s) in use
```

MCP: `inspect_state {"aspects":["next_copper","next_sprites"],"nr_from":"0","nr_count":"16"}`; WebAPI `GET .../state/next/copper?from=0&count=16&raw=true`,
`GET .../state/next/sprites?all=true`; Lua `next_copper{...}`, `next_sprites{...}`; Python `emu.next_copper(...)`, `emu.next_sprites(...)`.

### 3c. Which layer draws this pixel? (the layer capture and the pixel inspector)

"Is this tile from the tilemap or from Layer 2?", "why is my sprite hidden - behind which layer?", "what does the ULA look like without the tilemap on
top?" The Next screen records, while it draws each frame, the four layers (`ula` with the border and LoRes, `layer2`, `tilemap`, `sprites`), which
layer won every pixel (`ids`) and the register state of every line segment - so copper bars, split screens and palette tricks are exact; a
re-render from the registers of the moment of the request would not be (design: `docs/inprogress/2026-10-07-zx-next/design-layer-capture.md`).
The record is **on by default** (ini `[NEXT] LayerCapture=on|off`; it costs about 4 % of a frame and 3.1 MB for two frames of buffers) and a paused
machine answers from its last frame.

```text
capture_media {"action":"screenshot","layer":"tilemap"}                     # PNG, alpha 0 where the tilemap paints nothing; 640 x 512 like the screenshot
capture_media {"action":"screenshot","layer":"ids","filename":"scratch/ids.png"}   # every pixel coloured by the layer that won it (the legend is in the answer)
inspect_state {"aspects":["next_pixel"],"nr_x":"50","nr_y":"50"}             # which layer draws pixel (50, 50), and why
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/capture/next-layers"}         # ula, layer2, tilemap, sprites and ids of the SAME frame
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/next/layer-capture","body":{"enabled":false}}   # switch the record (the default is on)
```

WebAPI `GET .../capture/screen?layer=ula|layer2|tilemap|sprites|ids[&format=rgba][&alpha=false]`, `GET .../capture/next-layers`,
`GET .../state/next/pixel?x=50&y=50[&sx=1]`, `POST .../next/layer-capture {"enabled":true}`; CLI `capture screen --layer=tilemap scratch/tiles.png`,
`state next pixel 50 50`, `next layer-capture [on|off]`; Lua `capture_screen_layer("tilemap")`, `next_pixel(50, 50)`,
`next_layer_capture_control{enabled=true}`; Python `emu.capture_screen_layer("tilemap")`, `emu.next_pixel(50, 50)`,
`emu.next_layer_capture_control(True)`. A layer image answers 409 when the machine is not a Next (`not-next`), the record is off
(`layer-capture-off`) or no frame has been completed yet (`no-frame`); 400 for a word that is not a layer, a format or an alpha. The `next_video`
report carries the state as `layer_capture {enabled, frames, last_frame, bytes}`.

The pixel report names the **winner**, the composed colour and, per layer, whether it painted, **why it lost or did not paint** and **where its
pixel came from**: the ULA bitmap and attribute byte, the tile number and its definition byte, the Layer 2 byte, the sprite number and its pattern
byte. The winner, the colours and the opacity are the recorded ones; the addresses are recomputed from the pixel position and the registers of the
line *segment* the pixel was drawn in, and the bytes are read from the memory now (a paused machine: exact; a running one can be a frame newer).
The outputs below are real: they come from the core test `CliNextReports_Test.RecipeWhichLayerDrawsThisPixel`
(`UNREAL_PRINT_NEXT_RECIPE=1 core-tests --gtest_filter=CliNextReports_Test.RecipeWhichLayerDrawsThisPixel`).

**A tile you drew does not show.** The ULA screen is white paper; tile 1 (a solid square) is at column 6, row 6 - pixel (50, 50) is inside it:

```text
$ next layer-capture
Next layer capture on: 0 frame(s) recorded, last emulator frame 0, 1638400 bytes held

$ state next pixel 50 50
Pixel (50,50) sub-pixel 0 of Next frame #3 (emulator frame 3), line segment 0-640
winner: ula  colour 0x16D #B6B6B6  layer order SLU
  ula    : painted 0x16D #B6B6B6  - wins (layer order SLU)
           kind paper, ram_page 5, bitmap_offset 0x0242, bitmap_byte 0x00, bit 5, ink_pixel no, attribute_offset 0x1842, attribute_byte 0x38, palette_index 23, palette 0
  layer2 : not painted  - Layer 2 disabled (port #123B bit 1 / NR #69 bit 7)
           kind disabled
  tilemap: painted 0x00B #00246D  - below the ULA (tile attribute bit 0, or 512 mode)
           kind tile, column 6, row 6, map_bank 5, map_offset 0x21EC, tile 1, attribute 0x01, tile_bank 5, definition_offset 0x3029, pixel_value 5, x_mirror no, y_mirror no, rotate no, below_ula yes, transparent_index 15, palette_index 5, palette 3
  sprites: not painted  - sprites disabled (NR #15 bit 0)
           kind disabled
```

The tilemap painted (`painted 0x00B`) but the ULA won: the tile's attribute byte `0x01` has bit 0 set, "ULA above". Clearing the bit:

```text
$ state next pixel 50 50   # after clearing attribute bit 0 of the tile
Pixel (50,50) sub-pixel 0 of Next frame #6 (emulator frame 6), line segment 0-640
winner: tilemap  colour 0x00B #00246D  layer order SLU
  ula    : painted 0x16D #B6B6B6  - covered by the tilemap pixel (the tile is above the ULA)
           kind paper, ram_page 5, bitmap_offset 0x0242, bitmap_byte 0x00, bit 5, ink_pixel no, attribute_offset 0x1842, attribute_byte 0x38, palette_index 23, palette 0
  layer2 : not painted  - Layer 2 disabled (port #123B bit 1 / NR #69 bit 7)
           kind disabled
  tilemap: painted 0x00B #00246D  - wins (layer order SLU)
           kind tile, column 6, row 6, map_bank 5, map_offset 0x21EC, tile 1, attribute 0x00, tile_bank 5, definition_offset 0x3029, pixel_value 5, x_mirror no, y_mirror no, rotate no, below_ula no, transparent_index 15, palette_index 5, palette 3
  sprites: not painted  - sprites disabled (NR #15 bit 0)
           kind disabled
```

**A sprite that does not show.** Layer 2 is on, the layer order is LSU, sprite 0 is at (100, 100):

```text
$ state next pixel 105 105
Pixel (105,105) sub-pixel 0 of Next frame #9 (emulator frame 9), line segment 0-640
winner: layer2  colour 0x055 #2449B6  layer order LSU
  ula    : painted 0x16D #B6B6B6  - layer order LSU: layer2 above ula
           kind paper, ram_page 5, bitmap_offset 0x0929, bitmap_byte 0x00, bit 6, ink_pixel no, attribute_offset 0x1929, attribute_byte 0x38, palette_index 23, palette 0
  layer2 : painted 0x055 #2449B6  - wins (layer order LSU)
           resolution 256x192x8, bank 8, kind bitmap, ram_page16 9, offset 0x0949, bitmap_offset 0x4949, byte 0x2A, palette_index 42, palette 1
  tilemap: painted 0x000 #000000  - layer order LSU: layer2 above tilemap
           kind tile, column 13, row 13, map_bank 5, map_offset 0x242A, tile 0, attribute 0x00, tile_bank 5, definition_offset 0x3004, pixel_value 0, x_mirror no, y_mirror no, rotate no, below_ula no, transparent_index 15, palette_index 0, palette 3
  sprites: painted 0x023 #00926D  - layer order LSU: layer2 above sprites
           kind sprite, sprite 0, x 100, y 100, pattern 0, four_bit no, pattern_offset 0x0055, pattern_byte 0x11, palette 2, palette_index 17
```

Layer 2 is above the sprites in LSU (`layer order LSU: layer2 above sprites`); the sprite's own pixel exists (`painted 0x023`, sprite 0, pattern byte `0x11`). With the order SLU (NR #15 bits 4:2 = 000):

```text
$ state next pixel 105 105   # after layer order SLU
Pixel (105,105) sub-pixel 0 of Next frame #12 (emulator frame 12), line segment 0-640
winner: sprite  colour 0x023 #00926D  layer order SLU
  ula    : painted 0x16D #B6B6B6  - layer order SLU: sprite above ula
           kind paper, ram_page 5, bitmap_offset 0x0929, bitmap_byte 0x00, bit 6, ink_pixel no, attribute_offset 0x1929, attribute_byte 0x38, palette_index 23, palette 0
  layer2 : painted 0x055 #2449B6  - layer order SLU: sprite above layer2
           resolution 256x192x8, bank 8, kind bitmap, ram_page16 9, offset 0x0949, bitmap_offset 0x4949, byte 0x2A, palette_index 42, palette 1
  tilemap: painted 0x000 #000000  - layer order SLU: sprite above tilemap
           kind tile, column 13, row 13, map_bank 5, map_offset 0x242A, tile 0, attribute 0x00, tile_bank 5, definition_offset 0x3004, pixel_value 0, x_mirror no, y_mirror no, rotate no, below_ula no, transparent_index 15, palette_index 0, palette 3
  sprites: painted 0x023 #00926D  - wins (layer order SLU)
           kind sprite, sprite 0, x 100, y 100, pattern 0, four_bit no, pattern_offset 0x0055, pattern_byte 0x11, palette 2, palette_index 17
```

The images are the same frame in picture form; the WebAPI answers (from a running instance, Layer 2 / tilemap / sprites off):

```text
$ curl -s "http://localhost:8090/api/v1/emulator/$EMU/capture/screen?layer=ula" | jq 'del(.data)'
{"alpha": true, "format": "png", "frame": {"frame_number": 50, "sequence": 51}, "height": 512, "layer": "ula", "size": 220, "status": "success", "width": 640}
$ curl -s "http://localhost:8090/api/v1/emulator/$EMU/capture/screen?layer=layer3"
{"error":"Bad Request","kind":"bad-parameter","message":"Unknown layer 'layer3': use composed, ula, layer2, tilemap, sprites or ids"}
$ curl -s -X POST -d '{"enabled":false}' -H 'content-type: application/json' "http://localhost:8090/api/v1/emulator/$EMU/next/layer-capture"
{"available":true,"bytes":3276800,"enabled":false,"frames":56,"last_frame":55}
$ curl -s "http://localhost:8090/api/v1/emulator/$EMU/capture/screen?layer=ula"
{"error":"Conflict","kind":"layer-capture-off","message":"Layer capture is off: switch it on (POST /api/v1/emulator/{id}/next/layer-capture {\"enabled\":true}, ini [NEXT] LayerCapture=on) and let a frame run"}
```

(The PNG is RGBA, or a palette with a `tRNS` chunk when the image has few colours - the same alpha either way. `format=rgba` is the raw bytes, 640 x 512 x 4.)

### 4. Starting a snapshot (`.snx`, `.sna`, `.z80`) from the Browser

NextZXOS starts a snapshot in a way worth knowing, because every piece must be exact or the screen stays black:

1. the OS loads the file, builds a register image, and writes **NR #02 = 8** - the Multiface NMI (NR #06 bit 3 on); the code after
   the `OUT` is a `NOP` and the Multiface ROM checks that the NMI returned to `#0AB8` / `#0AB9`;
2. NR #C0 bit 3 is on (**stackless NMI**): the return address goes to NR #C2 / #C3, not the stack; the CPU reads it back at `RETN`;
3. the Multiface ROM (system page 5, `enNextMf.rom`) is at `#0000-#1FFF`, its RAM at `#2000-#3FFF`, from the fetch at `#0066` to
   the `RETN`; **the DivMMC's automap is inactive while it is in**;
4. the ROM sets the machine to 48K mode (`#7FFD` = `#30`), restores the registers and **NR #C2/#C3 = `#2313`** - a `RET` of the 48K
   ROM - so the `RETN` lands on `RET`, which pops the snapshot's PC from its stack.

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/feature/debugmode","body":{"enabled":true}}   # breakpoints need it
control_execution {"action":"bp_add","address":2742,"page":"rom2"}      # the OUT of NR #02 (ROM page 2 - an address alone also hits ROM 0)
... run the Browser entry ...
control_execution {"action":"step"}  ×2
#   → pc 0x0AB8, then pc 0x0066 with slot 0 = "multiface rom", NR #C2/#C3 = #B8 #0A
```

### 5. The real-board test programs

`tools/machines/next/browser-drive/suite.py --card <card>` runs every program of `tests/<area>/<test>/` the way a person does (reset,
Browser, run) and photographs the result beside the board photograph; `tools/machines/next/realboard/run-all.py` runs them straight
through the snapshot loader with automatic verdicts for the four programs that paint their own pass / fail.

### 5b. Playing a game: the keys are the game's own

The Next has **no standard set of game keys**. A game reads what its author chose and says so in its own readme or menu: the keyboard ports `#FE`, the Kempston joystick `#1F` / `#37`, Mega Drive pads, often a redefine menu. Read the readme (it is in the game's folder) before pressing keys, and do not take one game's keys for the platform's.

| Game | Keys (from its readme) | Notes |
|:--|:--|:--|
| Aliens Neoplasma II (`games/AliensNeoplasma2/Next/ANP2_DEMO.nex`) | `W A S D` move, `B N M` actions, `P`; a Mega Drive pad works | the title and the settings menu answer SPACE / ENTER and W / S / N; "any key" is `IN A,(#FE)` with a zero high byte, the pad is `IN A,(#1F)`. Hold the grenade key to throw further; jump + down drops through platforms |
| Vradark's Revenge (`games/VradarkRevenge/VradarkRevenge.nex`, needs `bin/` next to it) | not read yet | the mouse cursor shows on its MAP screen |

`type_input {"action":"tap","key":"w","frames":6}` presses a game key as a real one; hold with `"action":"press"` / `"release"` for a movement key.

**If a game "does not react to the keyboard", check `is_paused` first** (`GET /api/v1/emulator`): a click on the unreal-qt window's toolbar pauses the machine
from the GUI (`Origin::Gui`), and a paused machine takes no keys. `POST /api/v1/emulator/{id}/resume` fixes it. Only after that look at the game's own key scheme
and at the ports it polls.

### 6. Sound: is the DAC alive? (the DMA sample demo)

```text
load_software {"path":".../DMA Sample Engine Demo/zxnext_dma_sample.nex"}      # then tap a key
invoke_api POST /feature/porttrace {"enabled":true};  POST /profiler/porttrace/filter {"include":[{"direction":"out"}]}
#   OUT #FFDF should come at ~8000 per second; OUT #183B (CTC 0) is the timer
capture_media {"action":"audio_capture","seconds":2,"wav":true}              # left / right peak, rms, zero_crossing_rate
```

`audio_capture` leaves the emulator **paused** afterwards: resume it (`POST /resume`) before the next step.

## WebAPI

The same steps; ids come from `GET /api/v1/emulator`.

```bash
BASE=http://localhost:8090/api/v1/emulator; ID=$(curl -s $BASE | jq -r '.emulators[0].id')
curl -s -X POST $BASE/$ID/next/reg-journal -H 'Content-Type: application/json' -d '{"enabled":true,"clear":true,"capacity":262144}'
curl -s "$BASE/$ID/state/next/reg-journal?regs=02&limit=5" | jq '.events[] | {seq,frame,pc,source,value,decoded}'
curl -s -X POST $BASE/$ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"enter","frames":6}'
curl -s -X POST $BASE/$ID/step                                  # one instruction (debug mode on)
curl -s "$BASE/$ID/capture/screen" | jq -r .data | base64 -d > screen.png
```

## Pitfalls

- `turbo` (the feature) is not a speed-up here; leave it off.
- Execution breakpoints are silent unless the `debugmode` feature is on; an address alone matches in every ROM page - add `page`
  (`rom2`, `rom5`, `ram5`).
- The Browser remembers the directory it was last in (the card is the user's NextZXOS state): press EDIT to the root first
  (`drive.py root`).
- NextZXOS writes to the card folder (browser preferences, `autoexec.1st` renamed by the welcome page): keep a pristine copy.
- `screenshot` files go where the *server* process runs; with `filename` prefixes the path may be URL-encoded into one file name.
- NEXLOAD (and any esxDOS call) reads the card with the CPU switched to 14 MHz: a file or directory that "does not exist" on a big card while it exists on a small one is an SPI timing
  problem, not a FAT one - look at `SpiTooFastCount` and the `#2540` directory buffer (audit-divmmc-gap D11). The core-tests harness `NexBrowserRun_Test` (`UNREAL_NEXB*`, see
  `docs/emulator/environment-variables.md`) reproduces the Browser path headless with the whole collection on the card (`UNREAL_NEXB_INPLACE`, `UNREAL_NEXB_SEARCH=COL/DEM/TEC/NEXT3D DEMO CU/CUBE`).
- NextZXOS runs a program with the automap on (`NR #0A = #11`) and its own entry points (`NR #B8-#BB = #82 #00 #00 #F2`: `#0008`, `#0038`, `#3Dxx`, `#056A`, `#04D7` only with ROM 3 paged **and ROM in the
  MMU slot**). `NR #06` bit 4 is the DRIVE button's NMI, not the automap (the NEXLOAD source's comment is from older cores); NR #0A bits 7:5 stick unless in config mode.
- A black screen after starting a snapshot is almost always the NMI chain of section 4: check the journal for the `NR #02`
  write, the DivMMC mapping (`state/next` `divmmc.mapped`) and where `NR #C2/#C3` point.


### 3a5c. Kempston mouse, ULA+, Radastan (zxnext.vhd, D2)

- **Mouse**: `#FADF` buttons and wheel (`{wheel[3:0], 1, not middle, not left, not right}`: D1 is the left button, D0 the right one: the
  reverse of the ZX Evo's order), `#FBDF` X, `#FFDF` Y; decoded at A7-A0 = `#DF` and A11-A8 = A / B / F, enabled by NR `#83` bit 5. NR `#0A`
  bit 3 swaps the buttons. The device is the generic `Mouse` (`GET /api/v1/emulator/{id}/mouse`, MCP `mouse_input`): the wheel nibble is
  always on; with no mouse fitted the board's inputs idle (X = Y = 0, `#FADF` = `#0F`). NR `#0A` bits 1:0 scale the motion as the board's PS/2 module does (`ps2_mouse.v`):
  00 x2, 01 x1, 10 half, 11 a quarter.
- **ULA+**: `#BF3B` selects (bits 7:6 mode, 5:0 index in mode 0), `#FF3B` reads / writes the CLUT entry (GGGRRRBB, entries 192-255 of the ULA
  palette, the second one when NR `#43` bit 6 is set) in mode 0 and the enable bit in mode 1; the enable is NR `#68` bit 3's flop. In ULA+
  the pixel index is `11` flash bright half colour (no flashing), the border the paper half; ULANext wins over it; NR `#85` bit 0 gates the
  ports. `state next video` shows `ula.ulaplus`.
- **Radastan** (NR `#6A` bit 5 with LoRes on): 128 x 96 at 4 bits, 64-byte rows from the display file of port `#FF` bit 0 xor NR `#6A` bit 4,
  high nibble first; palette index high nibble NR `#6A` bits 3:0 (`11` and bits 1:0 with ULA+ on).
