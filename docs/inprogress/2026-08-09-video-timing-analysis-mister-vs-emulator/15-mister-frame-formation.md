# 15 — MiSTer ZX-Spectrum Core: Frame Formation and Pentagon INT Generation

**Date:** 2026-08-10
**Source:** `ZX-Spectrum_MISTer/rtl/ula.sv`, `rtl/T80/T80pa.vhd`, `rtl/T80/T80.vhd`, `ZX-Spectrum.sv`
**Scope:** Complete description of how a video frame is formed in the MiSTer core
(Pentagon mode, `mZX=0, m128=0`) and the exact moment the INT signal is asserted.

This is the authoritative reference for the unreal-ng timing model
(see [14-mister-reference-model.md](14-mister-reference-model.md) for the
emulator-side mapping).

---

## 1. Clocking

The core runs from a single 28 MHz system clock (`clk_sys`). All video and CPU
logic is gated by clock enables:

| Signal | Rate | Meaning |
|---|---|---|
| `clk_sys` | 28 MHz | master clock, all registers clock on its posedge |
| `ce_7mp` / `ce_7mn` | 7 MHz | pixel-clock enables (positive/negative phase) |
| `ce_cpu_sp` / `ce_cpu_sn` | 3.5 MHz | CPU clock enables (rising/falling edge of CPUClk) |

The horizontal counter `hc` advances on `ce_7mn`, i.e. **hc counts 7 MHz pixel
ticks; 2 hc ticks = 1 CPU T-state; hc/2 = T-state offset within the line.**

The CPU clock is derived directly from the horizontal counter:

```systemverilog
wire next_clk = hc_next[0] | (mZX & ulaContend & (memContend | ioContend));

assign ce_cpu_sp = ce_7mn & (~CPUClk &  next_clk_r);   // CPU posedge enable
assign ce_cpu_sn = ce_7mn & ( CPUClk & ~next_clk_r);   // CPU negedge enable
```

For Pentagon (`mZX=0`) the contention term vanishes: `next_clk = hc_next[0]`,
so **CPUClk is exactly bit 0 of hc** — the CPU and the video counters are rigidly
phase-locked, with a CPU rising edge on every tick where hc becomes odd. Pentagon
has no memory or IO contention of any kind.

---

## 2. Counters and frame dimensions

```systemverilog
reg [8:0] hc = 0;   // horizontal, 7 MHz ticks
reg [8:0] vc = 0;   // vertical, lines

if (hc == ((mZX && m128) ? 455 : 447)) begin
    hc_next = 0;
    if (vc == (!mZX ? 319 : m128 ? 310 : 311)) vc_next = 0;
    else                                       vc_next = vc + 1'd1;
end else begin
    hc_next = hc + 1'd1;
end
```

Pentagon (`!mZX`):

| Parameter | Value |
|---|---|
| hc range | 0..447 (448 ticks = **224 T-states/line**) |
| vc range | 0..319 (**320 lines/frame**) |
| Frame length | 320 × 224 = **71680 T-states** (~48.828 µs/line, 20 ms/frame, 50.02 Hz) |

**Origin convention:** `(vc=0, hc=0)` is the **top-left corner of the 256×192
paper (bitmap) area** — *by counters*. It is not hsync and not the border. All
sync/blank events are defined at large hc/vc values relative to this origin.

---

## 3. Paper / border decision

```systemverilog
wire Border_next = ((vc_next[7] & vc_next[6]) | vc_next[8] | hc_next[8]);
```

Decoded: border is active when `vc >= 192` (bits 7&6) or `vc >= 256` (bit 8) or
`hc >= 256` (bit 8). Therefore the **paper window by counters is exactly
vc 0..191, hc 0..255** — a pure power-of-two decode, which is why the counters
are anchored at the paper corner.

---

## 4. Sync and blanking (Pentagon branch)

```systemverilog
if(!mZX) begin
    if (hc_next == 312) HBlank <= 1;
        else if (hc_next == 420) HBlank <= 0;
    if (hc_next == 338) HSync <= 1;
        else if (hc_next == 370) HSync <= 0;
end
...
if(vc_next == 248) VSync <= 1;
    else if (vc_next == 256) VSync <= 0;
if(vc_next == 236) VBlank <= 1;
    else if(vc_next == 272) VBlank <= 0;
```

### Horizontal (per line, in hc ticks / T-states)

| Region | hc | Width | T-states |
|---|---|---|---|
| Paper (counter window) | 0..255 | 256 px | 128 T |
| Right border | 256..311 | 56 px | 28 T |
| **HBlank** | **312..419** | **108 px** | **54 T** |
| — front porch | 312..337 | 26 px | 13 T |
| — HSync | 338..369 | 32 px | 16 T |
| — back porch | 370..419 | 50 px | 25 T |
| Left border | 420..447 | 28 px | 14 T |

### Vertical (per frame, in lines)

| Region | vc | Lines |
|---|---|---|
| Paper | 0..191 | 192 |
| Bottom border | 192..235 | 44 |
| **VBlank** | **236..271** | **36** |
| — VSync | 248..255 | 8 |
| Top border | 272..319 | 48 |

Note the asymmetry: 44 bottom vs 48 top border lines, and (after the pipeline
shift, §5) 40 left vs 44 right border pixels. The visible frame is **340×284**,
not the 352×288 used by many emulators.

---

## 5. Video fetch pipeline (why output lags the counters by 12 px)

The ULA fetches video data ahead of display and pushes it through a shift
register. Within every 16-pixel group (while `~Border_next`):

```systemverilog
case(hc_next[3:0])
    'h8,'hC: vaddr <= {..., vc[7:6],vc[2:0],vc[5:3], hc_next[7:4], hc_next[2]}; // bitmap
    'hA,'hE: vaddr[14:7] <= {..., 3'b110, vc[7:5]};                             // attribute
endcase
```

- Bitmap byte address is set at `hc[3:0] = 8` and `C`, attribute at `A` and `E`
  (the classic interleaved bitmap/attr fetch pairs).
- The shift register is (re)loaded once per character half:

```systemverilog
if(hc_next[2:0] == 4) begin
    SRegister <= VidEN ? bits : 8'd0;
    AttrOut   <= VidEN ? attr : {2'b00,border_color,border_color};
end else begin
    SRegister <= {SRegister[6:0], 1'b0};   // shift out one pixel per tick
end

if(hc_next[3]) VidEN <= ~Border;
```

- `VidEN` (video enable) is sampled from `Border` at `hc[3]`, adding another
  latching stage.

The net effect of address-setup → RAM read → load-at-`hc[2:0]==4` → shift-out is
a **12-pixel (6 T-state) delay between the counter position and the pixel that
actually leaves the ULA**. The core states this explicitly in the Pentagon
border handling:

```systemverilog
//1T update for border in Pentagon mode
if(!mZX & ((hc_next < 12) | (hc_next > 267) | (vc >= 192)))
    AttrOut <= {2'b00, border_color, border_color};
```

i.e. the **visible paper output window is hc 12..267** (not 0..255), and border
color overrides the attribute output everywhere outside it. Pentagon border is
re-evaluated every hc tick (1T granularity); Sinclair models latch border only
at `hc[2:0]==4` (4T granularity).

### Visible line composition (output domain)

| Region | hc (output) | Pixels |
|---|---|---|
| Left border | 420..447 + 0..11 | 28 + 12 = **40** |
| Paper | 12..267 | **256** |
| Right border | 268..311 | **44** |
| (HBlank hides) | 312..419 | 108 |

---

## 6. Pentagon INT generation

```systemverilog
reg        INT    = 0;
reg  [6:0] INTCnt = 1;
assign nINT = ~INT;

// inside if(ce_7mn):
if( mZX && (vc_next == 248) && (hc_next == (m128 ? 8 : 4))) INT <= 1;  // Sinclair
if(!mZX && (vc_next == 239) && (hc_next == 326))            INT <= 1;  // Pentagon

if(INT)  INTCnt <= ((m128 && INTCnt == 71) || (~m128 && INTCnt == 63)) ? 7'd0 : (INTCnt + 1'd1);
if(INTCnt == 0) INT <= 0;
```

### Assertion moment

**INT rises on the 7 MHz tick where the counters take vc=239, hc=326.**

Placing it in the frame:

- vc=239 is the **4th line of VBlank** (VBlank spans 236..271), 9 lines before
  VSync begins at vc=248.
- hc=326 is **14 ticks (7 T) into HBlank** (HBlank spans 312..419), 12 ticks
  before HSync begins at hc=338.
- Offset from the counter origin (paper corner):
  `239 × 224 + 326/2 = 53536 + 163 = 53699 T`.
- Distance from INT to the *next* paper corner (counter domain):
  `71680 − 53699 = 17981 T`.
- Distance from INT to the next **visible** paper pixel (output domain, +6T
  pipeline): **17987 T**.

So on Pentagon, INT fires deep inside the vertical retrace, ~80 lines before the
picture starts — which is why classic Pentagon software conventions (and the
original Unreal Speccy emulator) treat "frame start" and "INT" as the same
moment.

### Pulse length

INTCnt starts at its idle value 1, increments every 7 MHz tick while INT is
high, wraps 63→0 (non-m128), and `INTCnt==0` clears INT on the next tick.
Trace: INT set at hc=326 → cleared at hc=390 of the same line.

**INT is high for 64 hc ticks = 32 T-states** (hc 326..389, T-in-line 163..194).
Sinclair 128 uses the 71→0 wrap: 72 ticks = 36 T-states. There is no separate
"INT acknowledged" reset — the pulse has a fixed length and the CPU may accept it
at any instruction boundary inside that window (or miss it entirely if
interrupts are disabled for the whole window).

### 48K/128K for comparison

- ZX-48K: INT at vc=248, hc=4, 32 T long.
- ZX-128K: INT at vc=248, hc=8, 36 T long.
- Both fire essentially *at* the paper-corner-relative line 248 (start of their
  VSync region), 2/4 T after hc=0.

---

## 7. How the T80 CPU samples INT

Wiring (`ZX-Spectrum.sv`):

```systemverilog
T80pa cpu (..., .CEN_p(ce_cpu_p), .CEN_n(ce_cpu_n), .INT_n(nINT), ...);
```

`T80pa` advances the internal T80 state machine only on `CEN_p` — the rising
edge of the 3.5 MHz CPU clock (ticks where hc becomes odd). Acceptance logic in
`T80.vhd`, evaluated on the clock edge that ends the **last T-state of the last
M-cycle** of every instruction (`T_Res='1'`, `MCycle = MCycles`):

```vhdl
elsif IntE_FF1 = '1' and INT_n = '0' and Prefix = "00" and SetEI = '0' then
    IntCycle <= '1';
    IntE_FF1 <= '0';
    IntE_FF2 <= '0';
```

Key properties:

1. **INT_n is sampled unregistered** at that edge (no synchronizer flop in this
   T80 build). The ULA sets INT on the same `clk_sys/ce_7mn` domain, so an INT
   asserted at hc=326 is first observable at the hc=327 CPU edge.
2. Sampling happens once per instruction, at its final T-state. If the sample
   hits, the **next** M1 becomes the interrupt acknowledge cycle
   (`IntCycle='1'`): 7 T-states (M1 + 2 wait states via `Auto_Wait`), then the
   PC push and vector fetch — 13 T total for IM1, 19 T for IM2.
3. `Prefix = "00"` — INT is never taken between a DD/FD/ED/CB prefix and its
   opcode body.
4. `SetEI = '0'` — the instruction immediately following `EI` cannot take the
   interrupt (standard Z80 EI delay).
5. During HALT the CPU executes internal 4T cycles, so INT acceptance granularity
   while halted is 4 T-states — this is the source of the classic 0..3T Pentagon
   "INT jitter" for HALT-synchronized code, and the reason demos like
   "Across The Edge" ship several fix variants.

---

## 8. Frame timeline (Pentagon, counter domain, origin = paper corner)

| T-state | vc, hc | Event |
|---|---|---|
| 0 | 0, 0 | Paper fetch window starts (visible pixel leaves at +6T) |
| 43008 | 192, 0 | Bottom border starts |
| 52864 | 236, 0 | VBlank starts |
| **53699** | **239, 326** | **INT asserted** |
| 53731 | 239, 390 | INT deasserted (32 T pulse) |
| 55552 | 248, 0 | VSync starts |
| 57344 | 256, 0 | VSync ends |
| 60928 | 272, 0 | Top border starts (VBlank ends) |
| 71680 = 0 | 0, 0 | Next frame paper corner |

Equivalent, expressed relative to INT (the convention used by Pentagon software,
original Unreal Speccy `Paper=17989`, ZXMAK2 `c_ulaIntBegin=0`):

| T after INT | Event |
|---|---|
| 0 | INT rises (vc=239, hc=326) |
| 32 | INT falls |
| 1853 | VSync starts |
| 3645 | VSync ends |
| 7229 | Top border becomes visible |
| 17981 | Paper corner (counter domain) |
| **17987** | **First visible paper pixel (output domain)** |
| 60989 | Bottom border starts |
| 70845 | VBlank starts |
| 71680 | Next INT |

---

## 9. Mapping to unreal-ng

unreal-ng anchors its frame counter with t=0 at 32 blank lines before the top
border and each line starting with the hblank interval (see doc 14). In that
system the MiSTer INT point (vc=239, hc=326) lands at:

```
line     = (239 + 81) mod 320 = 0      // hc=326 is in hblank, which starts the next emulator line
tInLine  = (326 − 312) / 2 = 7
intstart = 7,  intlen = 32
```

which preserves the reference invariant INT → first visible paper pixel
= 17987 T exactly.
