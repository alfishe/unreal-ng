# ZX-Evo BaseConf (ATM3) CPU wait states - RTL-derived specification

Scope: the released BaseConf RTL, `pentevo/fpga/base_trdemu/trunk` (identical, apart from CRLF, in
`zxevo.pentevo/fpga/base_trdemu/trunk`; the older `zxevo.pentevo/fpga/baseconf/trunk` differs only in `zmem.v`,
which has one fetch buffer instead of two, commit b5b58673 "add extra fetch buffer ... one for opcode and one for
data", 2021-12). All paths below are relative to `fpga/base_trdemu/trunk` of the pentevo repository (github.com/tslabs/zx-evo, folder `pentevo`)
unless they name another tree.

Method: (1) read the RTL; (2) ran the actual RTL modules `z80/zclock.v`, `z80/zmem.v`, `dram/arbiter.v`,
`video/video_sync_h.v`, `video/video_sync_v.v` in Verilator 5.052 with a behavioral Z80 bus model. The bus
model applies Z80 output changes 30-34 ns after the Z80 clock edge, the same as the project's own testbench
(`sim_top/tb_top.v:14-22`: `ZCLK_DELAY 9.5`, `Z80_DELAY_DOWN 34`, `Z80_DELAY_UP 30`). The bench is saved in
[tools/zxevo-rtl-sim/](tools/zxevo-rtl-sim/) (`build.sh`, `tb.v`, `main.cpp`). Every number marked "sim" came out of that bench.

Confidence marks: **[H]** read in the RTL and confirmed by the RTL simulation; **[M]** read in the RTL but not
simulated, or it depends on a power-up or bus-timing assumption that is stated; **[L]** inferred.

Units: fclk = 28 MHz FPGA clock (35.7 ns). One DRAM cycle = 4 fclk (phases `cbeg`, `post_cbeg`, `pre_cend`,
`cend`, `arbiter.v:181-186`). One CPU T-state is 2 fclk at 14 MHz ("T14"), 4 fclk at 7 MHz and 8 fclk at 3.5 MHz.
The video line counter `hcount` counts DRAM cycles ("slots", 7 MHz), 448 per line (224 T at 3.5 MHz) or 456 in
the 128K raster (`video_sync_h.v:114-115,145-151`).

---

## C. When each rule applies: speed and raster selection

### C.1 CPU speed

`top.v:401`:
```verilog
.turbo     ( {atm_turbo,~(peff7[4])}   ),
```
`zclock.v:81-83`: `2'b00 - 3.5 MHz, 2'b01 - 7.0 MHz, 2'b1x - 14.0 MHz`.

| #xx77 D3 (`atm_turbo`) | #EFF7 D4 | CPU clock |
|:-:|:-:|:--|
| 1 | any | 14 MHz |
| 0 | 0 | 7 MHz (reset state of both ports) |
| 0 | 1 | 3.5 MHz |

- #xx77 is written only in shadow mode (`zports.v:854`: `atm77_wr_fclk = (loa==ATM77) && shadow && port_wr_fclk`;
  `shadow = dos || shadow_en_reg`, `zports.v:320`, `shadow_en_reg` = #xxBF D0). D3 -> `atm_turbo`
  (`zports.v:898-901`), reset 0 (`zports.v:889-890`). **[H]** (RTL read)
- #EFF7 is written only outside shadow mode, with A12=0 and A8=1 (`zports.v:490`, `zports.v:715-719`:
  `else if( !a[12] && portf7_wr && (!shadow) ) peff7_int <= din; // 4 - turbooff ...`). Reset 0. Bit 4 survives
  the 1 MB block (`zports.v:725`). **[H]**
- After a hard reset with neither port written, the RTL runs at **7 MHz** (turbo = `{0, ~0}` = `01`). The
  firmware/ROM normally writes #EFF7. **[H]** (RTL); what the boot ROM writes: not checked.
- **The switch is deferred to the next opcode fetch's refresh**: `zclock.v:142-149`
  ```verilog
  always @(posedge fclk) if(zpos) begin old_rfsh_n <= rfsh_n; if( old_rfsh_n && !rfsh_n ) int_turbo <= turbo; end
  ```
  `int_turbo` (the speed actually used by every rule below) changes at the first rising CPU edge that sees
  /RFSH low, i.e. at T4 of the M1 after the `OUT`. So the instruction after the `OUT` still runs its M1 T1-T3
  at the old speed. The emulator's `PortDecoder_ATM3::updateTurboMode` mapping (xx77 D3 -> 14, else EFF7 D4 ->
  3.5, else 7) matches the RTL; applying it immediately instead of at the next RFSH is a sub-instruction
  difference. **[H]**

### C.2 Raster (the "Sinclair" timing modes)

- `modes_raster[1:0]` comes from the AVR over SPI, config register #50 bits 5:4: `top.v:789`
  `.config0( {not_used0[7:6], modes_raster, beeper_mux, tape_read, set_nmi[0], cfg_vga_on} )`,
  `slave/slavespi.v:205` (`sel_cfg0 = (regnum[7:4]==4'h5)`). It is **not** a Z80 port. **[H]**
- Values (`video_sync_v.v:136-157`, `video_sync_h.v:147,234`): `00` Pentagon (320 lines x 448 slots),
  `01` 60 Hz (262 lines), `10` 48K (312 x 448 = 69888 T), `11` 128K (311 x 456 = 70908 T).
- AVR side (`pentevo/avr/baseconf/trunk/src`): `main.h:154` `#define MODES_RASTER 0x30`; `zx.c:396-405`
  Scroll Lock increments the 3-bit field {raster[5:4], VGA[0]} of `modes_register`, so it cycles
  TV/VGA x Pentagon/60Hz/48K/128K (8 states); `zx.c:650-659` sends it; `rtc.c:203-206` restores it at power-up
  from PCF8583 NVRAM cell #FE (`rtc.h:25`). The Z80 cannot write that cell (gluk emulation only exposes
  cells up to #EF). So the raster is a user/hardware setting, persisted, invisible to software except as a
  read-back through the gluk "RDCFG" ext type (`version.c:40`). **[H]** (the English BaseConf manual only
  describes TV/VGA for Scroll Lock; the firmware source is newer.)
- `mode_contend_ena` and `mode_contend_type` are hard-wired: `top.v:189-190`
  ```verilog
  wire       mode_contend_type = 1'b0; // 48/128/+2 or +2a/+3 TODO: take these signals from somewhere
  wire       mode_contend_ena  = 1'b1; // contention enable
  ```
  So contention is always on in the 48K and 128K rasters (at 3.5 MHz), and the +2A/+3 pattern
  (`video_sync_h.v:275-280`) is dead code. **[H]**

For the emulator: the ATM3 machine needs a config/setting "raster = Pentagon | 48K | 128K | 60Hz"
(the frame geometry and INT position change with it, not only the contention).

---

## A. 14 MHz waits

### A.1 The mechanism

- DRAM arbiter: blocks of 8 DRAM cycles; video takes 1 per block in ZX modes, 2 in the other BaseConf modes
  (`video_modedecode.v:146-149`: `if( (atm_vmode==3'b011) && (pent_vmode!=2'b10) ) mode_bw <= 2'b00; // 1/8
  else mode_bw <= 2'b01; // 1/4`). The CPU gets any cycle it requests unless all remaining cycles of the block
  must go to video (`arbiter.v:263-291`: `if( vid_rem==blk_rem ) cpu_next = 1'b0`). Idle cycles are refresh
  cycles (`arbiter.v:57`); **refresh never takes a cycle away from the CPU**. **[H]**
- CPU side, `zmem.v`: a RAM access starts a DRAM request `dram_beg` only on a **miss in two one-word caches**
  or on any write (`zmem.v:246`):
  ```verilog
  assign dram_beg = ( !cache_hit || memwr ) && zneg && r_mreq_n && (!romnram) && (!mreq_n) && rfsh_n;
  ```
  - Caches (`zmem.v:221-238,333-402`): a *code* word (filled by reads with /M1 low) and a *data* word (filled
    by reads with /M1 high); each holds the 16-bit DRAM word `za[15:1]` (even-aligned byte pair). A read hits
    if its `za[15:1]` matches **either** valid word (`cache_hit = code_hit || data_hit`).
  - Invalidation (`zmem.v:363-380`): any MREQ access (read or write) to a **ROM** window -> both words; any
    **I/O** cycle start (`io = ~iorq_n`, so interrupt acknowledge too) -> both; a **write** -> only the word(s)
    whose address matches; `nmi_buf_clr` -> both.
  - **ROM accesses never wait** at 14 MHz (`!romnram` in `dram_beg`; ROM is read directly). **[H]**
- The stall (`zmem.v:274-305`): `stall14_ini` (the `dram_beg` clock itself), `stall14_cyc` (until the
  arbiter's next cycle is the CPU's, `cpu_next && cend`), `stall14_fin` (during the CPU's DRAM cycle, released at
  `(opfetch&pre_cend) || (memrd&post_cbeg)`), and `cpu_stall = int_turbo[1] ? (ini|cyc|fin) : ...`. It freezes
  the 14 MHz clock generator (`zclock.v:196-203`). `dram_beg` happens at the **T2 falling edge** (MREQ newly
  low at a `zneg`), so the wait is inserted between T2 and T3, like /WAIT states. **[H]** (sim trace)
- Writes: `stall14_ini = dram_beg && ( (!cpu_next) || opfetch || memrd )` and `stall14_cyc = memwr ?
  (!cpu_next) : ...` -> a write waits only if `cpu_next` is 0, which never happens in BaseConf modes (below).

### A.2 Correction of the in-source wait table

`zmem.v:254-272` says M1 waits +3/+4/+5/+6 and a read +2/+3/+4/+5 depending on whether `dram_beg` falls on
`cend`/`pre_cend`/`post_cbeg`/`cbeg`. What the RTL actually does (sim):

1. **Only two phases ever occur.** Every Z80 rising edge falls on an odd fclk of the DRAM cycle (phase
   `post_cbeg` or `cend`), so `dram_beg` (3 fclk after T1) falls only on `cbeg` or `pre_cend`. This held in
   every run, including after switching 3.5 -> 14 and 7 -> 14 at arbitrary moments and in the middle of
   contended 3.5 MHz code. **[H]** (sim)
2. **M1 and data reads wait the same.** The release term `memrd&post_cbeg` is also true during an opcode
   fetch (/RD is low in M1), so the `opfetch&pre_cend` branch never decides. **[H]** (RTL + sim)

Resulting table (sim, 0 waits never on a miss, never a third value, `next0` = arbiter refusal never seen):

| access (RAM window) | T1 on `cend` phase | T1 on `post_cbeg` phase |
|:--|:--|:--|
| M1 opcode fetch, cache miss | +4 fclk = **+2 T14** (6 T) | +6 fclk = **+3 T14** (7 T) |
| memory read, cache miss | +4 fclk = **+2 T14** (5 T) | +6 fclk = **+3 T14** (6 T) |
| M1 or read, cache hit | 0 | 0 |
| memory write (always goes to DRAM) | 0 | 0 |
| any access to a ROM window | 0 | 0 |
| I/O, internal port | 0 (I/O cycle 4 T14) | 0 |
| I/O, external port (A.4) | +6 fclk = **+3 T14** (7 T) | +3 T14 |

### A.3 Emulator form of the phase

At 14 MHz all waits are whole T14, so the phase is simply the parity of the CPU's 14 MHz T-state counter:

> **t = frame-relative 14 MHz T-state at which the access's T1 starts (all earlier waits included).
> DRAM read (M1 or data, RAM window, cache miss): wait = 2 + (t & 1) T14.**

Proof in sim: with `t` measured from the 3.5 MHz-grid frame origin (the same origin that gives the 48K
contention onset of 14335, section B.3), `t` even <=> `cend` phase and `t` odd <=> `post_cbeg` phase in all
three rasters (48K, 128K, Pentagon). The 3.5 MHz T-grid points are `cend`-phase edges, and a 3.5 MHz T is 4 T14,
so any frame origin on the 3.5 MHz T grid gives the same parity. If the emulator's 14 MHz frame origin is
shifted by an odd number of T14 from that grid, swap the parity. **[H]** (sim) for the rule; **[M]** that
unreal-ng's ATM3 frame origin is on that grid (it is if it is the 3.5 MHz origin scaled by 4, which is what
`ApplyHardwareTurboNow`'s rescale implies).

Useful invariant (follows from the table): an M1 miss always ends on an even t, a read miss always on an odd t.

### A.4 I/O at 14 MHz

`zclock.v:158-191`:
```verilog
assign io = (~iorq_n) & m1_n & external_port;
... else if( io && (!io_r) && zpos && int_turbo[1] ) io_wait_cnt[3] <= 1'b1;
... case( io_wait_cnt ) 4'b1000..4'b1100: io_wait <= 1; 4'b1101: 0; 4'b1110: 1; 4'b1111: 0;
```
`external_port` (`zports.v:361-367`) = port low byte #FD with A15=1 (AY #FFFD/#BFFD and any #8xFD..), or the
VG93 ports #1F/#3F/#5F/#7F while shadow is on (not #FF). The counter stalls 6 of the next 8 fclk. Sim: an
external I/O cycle takes 14 fclk = **7 T14 instead of 4** at either phase (**+3 T14**, not a full 7 MHz cycle,
which would be 8 T14); every other port takes 4 T14. Not at 7 or 3.5 MHz (`int_turbo[1]`). Interrupt
acknowledge excluded (`m1_n`). **[H]**

### A.5 What an emulator can ignore, and the simplest faithful rule

- **DRAM refresh**: only fills idle cycles; no effect. **[H]**
- **Video bandwidth**: sim with `go` forced on (1/8 and 1/4 bandwidth) and with the real fetch window gave
  identical cycle counts to `go=0`; the arbiter refusal (`cpu_next=0`) never occurred. Only a bandwidth of 8/8
  (`bw=11`) would stall the CPU, and BaseConf never selects it (`video_modedecode.v:146-149`). **[H]**
- **What must be modeled**: the two one-word caches (hit = no wait, and the invalidation rules above), the
  RAM/ROM window type, and the T-parity.

Simplest faithful rule at 14 MHz (only `int_turbo == 14 MHz`):
1. M1 fetch or data read from a RAM window: if `addr>>1` equals the valid code word or the valid data word,
   0 wait; else wait `2 + (t & 1)` and load the word into the code word (M1) or data word (read).
2. Write to RAM: 0 wait; invalidate whichever word matches `addr>>1`.
3. Any access to a ROM window: 0 wait; invalidate both words (also a ROM write).
4. Any I/O cycle, including interrupt acknowledge: invalidate both words; +3 T14 if the port is external
   (A.4).
5. Everything else (refresh, internal T-states): nothing.

This is exact against the RTL except for the DOS-ROM entry stall (A.6) and the AVR /WAIT ports (A.6).

### A.6 Other clock stalls (all speeds)

- **TR-DOS ROM entry**: `mem/atm_pager.v:246-282`, a fetch at #3Dxx with the 48K ROM paged in stalls the clock
  for 4 fclk (`dos_turn_on` + `stall_count` 101->110->111), at the T2 falling edge. Effect: +2 T14 at 14 MHz;
  by the clock-generator logic +1 T at 7 MHz and +1 T at 3.5 MHz (a blocked edge waits for its next slot).
  **[M]** (derived, not simulated)
- **/WAIT from the AVR** (`z80/zwait.v:57-79`, `zports.v:778-780`): Gluk clock data port #BFF7 (#BEF7 in shadow)
  and the RS-232 ports #F8EF-#FFEF hold /WAIT until the AVR answers over SPI. Duration depends on AVR firmware
  latency (microseconds), not on the FPGA; not modelable from the RTL. **[H]** that it exists; length unknown.

### A.7 Worked examples at 14 MHz (sim, T14)

- **NOP stream in RAM, sequential addresses**: the even-address NOP misses, the odd one hits the code word.
  After the first miss every even NOP starts on an even t (an M1 miss ends even): 4+2 = **6**, then **4**,
  6, 4 ... = 5 T14 per NOP on average (sim: 12 / 8 fclk alternating). A NOP stream at one address (a `HALT`, a
  `JR $`) is **4** per fetch after the first. **[H]**
- **`LD A,(HL)`** (nominal M1 4 + read 3 = 7 T), PC and HL walking so that every data read misses (sim test
  `ldahl_seq`):
  - PC even (code miss), previous read miss ended on odd t: M1 at odd t -> 4+3 = 7; read at odd+7 = even ->
    3+2 = 5; **12 T14**.
  - PC odd (code hit): M1 at odd t -> 4; read at odd+4 = odd -> 3+3 = 6; **10 T14**.
  - Alternating 12 / 10 (sim: 24 / 20 fclk). With a data hit (same word as the previous read) the read adds 0.
- **`LD (HL),A`**: M1 as above + write 3 (no wait). **`OUT (#FE),A`**: I/O adds 0 but invalidates both words,
  so the next M1 always misses. **`OUT (C),A` to #FFFD**: the I/O cycle is 7 T14 (sim: 36 fclk = 18 T14 for
  the whole ED 79 = (4+3) first M1, a miss on an odd t, + 4 second M1, a hit, + 7 I/O).

---

## B. Sinclair contention in the 48K / 128K rasters (3.5 MHz only)

### B.1 The condition

`zclock.v:267-282`:
```verilog
assign iorq_n_a = iorq_n || (a[0]==1'b1);
always @(posedge fclk) if( zpos ) begin r_mreq_n <= mreq_n; r_iorq_n_a <= iorq_n_a; end
assign contend_addr = (modes_raster[0]==1'b0) ? ( a[15:14]==2'b01 ) :                          // 48k mode
                                                ( a[15:14]==2'b01 || (a[15:14]==2'b11 && p7ffd[0]) ) ; // 128k mode
assign contend_mem = contend_addr && r_mreq_n;
assign contend_io  = !iorq_n_a && r_iorq_n_a;
assign contend_wait = contend && (contend_mem || contend_io) && !int_turbo && modes_raster[1] && mode_contend_ena;
```
- Only at 3.5 MHz (`!int_turbo`), only in the 48K/128K rasters (`modes_raster[1]`). **[H]**
- Contended addresses: #4000-#7FFF always (whatever is mapped there, even ROM, even under ATM paging); in the
  128K raster also #C000-#FFFF when **#7FFD bit 0** is 1 (banks 1,3,5,7) - the RTL looks at the #7FFD
  register bit, not at the page actually mapped by the ATM pager or the Pentagon-1024 extension bits. **[H]**
  (sim: `c000` probe contended for p7ffd=1,3,5,7, not for 0,2)
- `contend_mem` needs MREQ *inactive at the previous rising edge*: T1 of every memory cycle and every internal
  T-state whose address is contended; never T2/T3, never the refresh half of M1. `contend_io`: the T-state
  after an I/O cycle's IORQ goes low, when A0=0. This is the Ferranti ULA's condition. **[H]**
- The stall blocks both clock edges and a blocked edge waits for its next slot, so delays are whole 3.5 MHz
  T-states. **[H]**

### B.2 Pattern

`video_sync_h.v:118,255-274`: `contend_ctr` restarts at `hcount==127` (`CONTEND_START`), counts 256 slots
(128 T), and on lines where `vpix` is set:
```verilog
case( contend_ctr[3:1] ) 3'd6, 3'd7: contend <= 1'b0; default: contend <= 1'b1; endcase  // 48k type
```
= contended 6 T out of every 8, for 128 T per line. Sim delay per T1 position within the 8-T group:
**6, 5, 4, 3, 2, 1, 0, 0** (the ULA pattern), for M1, read, write and internal T-states alike. **[H]**

### B.3 Onset relative to INT, per raster

INT: `video_sync_h.v:109-111,232-238` + `video_sync_v.v:109-111,190-197`: INT starts on line 1 at slot 126
(48K) / 130 (128K); `zint.v:57-78` holds it 256 fclk = 32 T. Pixel lines (`vpix`, `video_sync_v.v:88-92,201-207`,
ZX video modes add 4): lines 65-256 (48K), 64-255 (128K), 192 lines.

Measured in sim, with T=0 defined as in Fuse (the first instruction boundary at which the CPU accepts the INT;
the INT is then accepted at boundaries 0..31, exactly the 32 T window):

| raster | line length | frame | first contended T1 (delay 6) | lines |
|:--|:--|:--|:--|:--|
| 48K (`10`) | 224 T | 312 lines, 69888 T | **14335** | 192, +224 per line |
| 128K (`11`) | 228 T | 311 lines, 70908 T | **14361** | 192, +228 per line |

Both equal the real 48K and 128K. **[H]** for the RTL numbers; **[M]** for their absolute placement: they need
the 3.5 MHz clock phase that the sim gets from all registers powering up at 0 (the 3.5 MHz rising edges then
fall on even `hcount` slots). The other phase would move both onsets 1 T later (14336 / 14362). ACEX1K
registers do power up at 0, and nothing resets `hcount` or `precend_cnt` later (`video_top.v:241` ties
`init` to 0), so the modeled phase is the likely one, and it is the one that reproduces the Sinclair numbers
the constants 126 / 130 / 127 were evidently tuned for.

Caveat: in the ATM video modes (not ZX) `vpix` covers 200 lines starting 4 lines earlier, and contention
follows `vpix`, so it would start at 14335 - 4*224. Edge case. **[H]** (RTL read)

### B.4 I/O contention and comparison with a real 48K / 128K

Sim delay tables (probe after an uncontended M1):

| cycle | RTL result | Ferranti ULA (Fuse notation) | same? |
|:--|:--|:--|:--|
| M1, read, write at #4000 | pattern at T1 | `C:4` / `C:3` | yes |
| internal T-state, address #4000 | pattern at that T | `C:1` | yes |
| refresh (IR in #4000-#7FFF) | none | none | yes |
| port #00FE (high byte free, A0=0) | pattern at T+1, then 3 T | `N:1, C:3` | yes |
| port #40FF (high contended, A0=1) | pattern on each of the 4 T | `C:1, C:1, C:1, C:1` | yes |
| **port #40FE / #7FFE (high contended, A0=0)** | **pattern on each of the 4 T** | `C:1, C:3` | **no** |
| port with high byte free, A0=1 | none | `N:4` | yes |

The one difference: `contend_mem` does not exclude I/O cycles, so a port whose high byte is #40-#7F is
contended on every T-state even when A0=0 (e.g. `IN A,(#FE)` with A=#7F, keyboard row SPACE..B). On a real
Spectrum the ULA contends such an access `C:1, C:3`. **[H]** (sim: identical tables for `io_40ff` and
`io_40fe`, e.g. T1 at group position 2 -> +10, where `C:1,C:3` gives +4)

Also: interrupt acknowledge with PC in contended memory would be contended every T, but INT always falls in
the top border where `contend` is 0, so it never happens. **[H]**

**For the emulator**: at 3.5 MHz in the 48K raster, the existing `ula48` rule is exact except for the A0=0 /
high-byte-contended I/O case, which must use `C:1, C:1, C:1, C:1`; in the 128K raster, `ula128` is exact with the
same I/O exception and with "#C000 slot contended iff #7FFD bit 0" (not the mapped bank). No contention at 7 or
14 MHz, none in the Pentagon or 60 Hz rasters.

---

## D. Corrections to `docs/inprogress/2026-09-28-m1-contention/contention-by-machine.md` §9.2

1. "An M1 waits 3-6 fclk (28 MHz) and a read 2-5 fclk, depending on the DRAM phase" - the in-source table
   (`zmem.v:254-272`) does not match the logic. Only two phases occur, and M1 and reads wait the same: **+2 or
   +3 T14 (4 or 6 fclk) on a miss in two one-word caches (one code, one data), 0 on a hit, 0 for ROM**, phase =
   T parity (A.2-A.3).
2. "External I/O drops to 7 MHz" - it adds **+3 T14** (6 fclk) to the I/O cycle (7 T14 instead of 4); only AY
   `#xxFD` with A15=1 and the VG93 ports in shadow mode (A.4).
3. "(chosen in the AVR setup)" - chosen with **Scroll Lock** (cycles TV/VGA x Pentagon/60Hz/48K/128K), stored in
   PCF8583 NVRAM #FE, sent in SPI config register #50 bits 5:4; not a Z80 port (C.2).
4. Add: contention is always enabled in those rasters and the +2A/+3 pattern is unreachable (`top.v:189-190`);
   onset 14335 / 14361 (B.3); the #C000 slot follows #7FFD bit 0, not the mapped page; high-byte-contended even
   ports are `C:1` x4, not `C:1, C:3` (B.4).
5. "At 3.5 and 7 MHz the CPU stalls only when no cycle is left in the block ... never in practice" - confirmed,
   and the same holds at 14 MHz: video bandwidth never adds a wait (A.5).
6. Add: the speed switch takes effect at the next opcode fetch's refresh (C.1); the TR-DOS entry stall is
   4 fclk (A.6).
