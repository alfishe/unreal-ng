# Kempston Mouse — Hardware Reference

Normative behaviour for the Kempston Mouse interface. Every claim here carries a
primary-source citation; where sources disagree the disagreement is recorded rather
than smoothed over, and the resolution is argued in [design.md](design.md).

Primary sources for the decode and the button map:

- **bootcamp** — `speccy-bootcamp/10_references/io_port_map.md` @ `e50577b1`.
- **BC#4** — Black_Cat, *BC Info Guide #4, Guide to the ZX Spectrum ports*
  (`tslabs/zx-evo`, `pentevo/docs/ZX/zx-ports-full-table.txt`, ver. 28.09.2006). This is
  the per-model table; bootcamp agrees with it character-for-character on the mouse
  patterns.

Emulator reference trees live under `/Volumes/TB4-4Tb/Projects/emulators/github`.

**Decoding is per-model.** There is no single mask that is correct everywhere — see §2.

---

## 1. The three registers

The interface presents three read-only registers. There are no writable ports.

| Register | Canonical port | Contents |
|---|---|---|
| Buttons (+ wheel) | `#FADF` | wheel in the upper nibble, buttons in the low bits |
| X axis | `#FBDF` | 8-bit wrapping counter |
| Y axis | `#FFDF` | 8-bit wrapping counter |

The decode is **not** a full 16-bit port match, and it is not a full low-byte match
either. Per bootcamp:

```
#FADF  xxxxxx10xx0xxxxx  R  Buttons
#FBDF  xxxxx011xx0xxxxx  R  X position
#FFDF  xxxxx111xx0xxxxx  R  Y position
```

Reading those patterns (A15 leftmost), the qualifying lines are:

| Register | Decoded lines |
|---|---|
| Buttons | A9 = 1, A8 = 0, A5 = 0 |
| X | A10 = 0, A9 = 1, A8 = 1, A5 = 0 |
| Y | A10 = 1, A9 = 1, A8 = 1, A5 = 0 |

So the interface qualifies on **A9 = 1 and A5 = 0**, then **A8** selects buttons versus
axis and **A10** selects X versus Y. Every other address line is a don't-care, which is
why bootcamp counts 8192 mirrors for the button port and 4096 for each axis. This agrees
exactly with zxsp's reading of the physical schematic (§2).

The ZX Evo FPGA states the A8/A10 half of the same selection —
`zx-evo/pentevo/fpga/current/z80/zkbdmus.v:106-107`:

```verilog
// FADF - buttons, FBDF - x, FFDF - y
assign mus_data = zah[0] ? (zah[2] ? musy : musx) : {muswhl, 1'b1, musbtn};
```

Only two address lines select the register:

- **A8** (`zah[0]`) — 0 selects buttons, 1 selects an axis
- **A10** (`zah[2]`) — when A8 = 1, 0 selects X and 1 selects Y

When A8 = 0 the axis selector is a don't-care, so the button register answers at both
`#FADF` and `#FEDF`.

Note the FPGA additionally decodes the **whole** low byte — `localparam KMOUSE = 8'hDF`
(`zx-evo/pentevo/fpga/current/z80/zports.v:283`) — which is *stricter* than the original
hardware bootcamp documents. That is a ZX Evo implementation choice, not the general
rule, and must not be generalised: on real hardware only A5 is decoded in the low byte.

## 2. Address decoding is per-model

Every model decoder may have its own flavour and its own bit sensitivity. The interface
is a card, the machine is a bus, and each machine's decoder answers a different subset of
the address lines. Treating this as one global rule is wrong.

### 2.1 Known documented decodes (BC#4)

**Standard Kempston mouse** — listed for models **(1, 7-9)**:

```
#FADF/64223  1111101011011111 xxxxxx10xx0xxxxx Kmou_B
#FBDF/64479  1111101111011111 xxxxx011xx0xxxxx Kmou_X
#FFDF/65503  1111111111011111 xxxxx111xx0xxxxx Kmou_Y
```

**USSR Kempston mouse** — a different flavour of the same interface:

```
#FADF/64223  1111101011011111 xxxxx0x01x0xxxx1 Kmou_B
#FBDF/64479  1111101111011111 xxxxx0x11x0xxxx1 Kmou_X
#FFDF/65503  1111111111011111 xxxxx1x11x0xxxx1 Kmou_Y
```

Decoded lines, verified against the port values:

| Variant | Buttons | X | Y |
|---|---|---|---|
| Standard | A9=1, A8=0, A5=0 | A10=0, A9=1, A8=1, A5=0 | A10=1, A9=1, A8=1, A5=0 |
| USSR | A10=0, A8=0, A7=1, A5=0, A0=1 | A10=0, A8=1, A7=1, A5=0, A0=1 | A10=1, A8=1, A7=1, A5=0, A0=1 |

Note the USSR variant does **not** decode A9 at all, decodes A7 and A0 instead, and
therefore answers only on odd addresses — 2048 mirrors per port against the standard
variant's 8192 (buttons) and 4096 (axes).

### 2.2 BC#4 model legend

```
1/+1 - ZX Spectrum (issue 1-2/3-6)   6 - Scorpion ZS256 Turbo+   B - Scorpion GMX
2    - ZX Spectrum +128,+2           7 - KAY-1024SL/Beta Turbo   C - Quorum 128/+
3/+3 - ZX Spectrum +2a,+2b/+3        8 - Pentagon 128 (1991)     D - Pentagon-1024SL
4    - Timex Computer 2048           9 - Profi-1 (v3.x)
5    - Didaktik Gama                 A - ATM Turbo-2+
```

So the mouse is documented for ZX Spectrum (1), KAY-1024SL (7), Pentagon 128 (8) and
Profi-1 (9). It is **not** listed for Scorpion (6), ATM Turbo-2+ (A), Quorum (C) or
Pentagon-1024SL (D) — on those the mouse is an add-on card, and the decode follows the
card, not the machine.

### 2.3 ZX Evo / Pentevo decodes more strictly

The ZX Evo FPGA additionally decodes the **whole** low byte —
`localparam KMOUSE = 8'hDF` (`zx-evo/pentevo/fpga/current/z80/zports.v:283`) — where the
original hardware decodes only A5. This is a concrete example of a per-model flavour:
a program relying on a mirrored address works on a Pentagon and fails on ZX Evo.

### 2.4 Emulator masks, for comparison

Emulators pick convenience masks, all of them stricter than §2.1 and none matching each
other. Xpeccy matches `(port & mask) == (value & mask)`
(`Xpeccy/src/libxpeccy/hardware/hardware.c:229`):

| Machine | Mask | Lines decoded | Source |
|---|---|---|---|
| Pentagon, pent1024 | `0x05A3` | A0, A1, A5, A7, A8, A10 | `hardware/pentagon.c:34-36` |
| Scorpion | `0x0523` | A0, A1, A5, A8, A10 (no A7) | `hardware/scorpion.c:120-122` |
| ZX48 | `0x0320` buttons, `0x0720` axes | A5, A8, A9 (+A10) | `hardware/zx48.c:67-69` |
| ATM2, Pentevo, Profi, TSLab, Phoenix | `0xFFFF` | all sixteen | `hardware/atm2.c:240-242`, `pentevo.c:312-314` |

zxsp models the physical schematic instead (`zxsp/Source/Uni/Items/KempstonMouse.cpp:11-18`):
base decode `"----.--1-.--0-.----"` — A9 = 1, A5 = 0 — then
`switch ((address >> 8) & 7)`, case 2 and 6 buttons ("A10 not decoded for buttons"),
case 3 X, case 7 Y (`:49-70`). This matches §2.1 standard exactly.

UnrealSpeccyP normalises then compares for equality — `port |= 0xfa00; // A13,A15 not
used in decoding` (`devices/input/kempston_mouse.cpp:35-43`), decoding A0–A8 plus A10.

ZXMAK2 ships `mask = 0xFFFF` with a commented-out `0x05FF`
(`ZXMAK2/src/ZXMAK2.Hardware/General/KempstonMouseDevice.cs:33`).

## 3. Collisions

### 3.1 Kempston joystick

This is the collision that matters. The Kempston *joystick* decodes A5 = 0 alone on
real hardware, and the mouse low byte `#DF` = `1101 1111` also has A5 = 0. The mouse
therefore sits **inside** the joystick's decode window, and a naive joystick handler
will answer mouse reads.

UnrealSpeccyP is the only source that resolves this explicitly, and it does so in both
directions (`devices/input/kempston_joy.cpp:31-35`):

```cpp
// skip kempston mouse ports
port |= 0xfa00;
if ((port == 0xfadf || port == 0xfbdf || port == 0xffdf)) return false;
```

Xpeccy and the ZX Evo FPGA sidestep the problem instead, by decoding the joystick on a
full low byte of `0x1F` rather than on A5 alone (`hardware/pentagon.c:33` mask `0x00FF`;
`zports.v:281` `KJOY = 8'h1F`).

BC#4 shows the joystick decode is itself per-model, and how wide the spread is:

```
#1F/31       xxxxxxxx00011111 xxxxxxxxxxxxxxx1 Kjoy(7)
                              xxxxxxxxxx0xxxx1 Kjoy(D)
                              xxxxxxxx0xx11xx1 KjoyPrn(C)
                              xxxxxxxx0x0xxx11 Kjoy(6)
```

`Kjoy(7)` (KAY-1024SL) decodes **A0 alone** — it answers every odd port in the machine,
mouse ports included. `Kjoy(D)` (Pentagon-1024SL) decodes A5 = 0 and A0 = 1, which the
mouse ports also satisfy. So the overlap is not a quirk of one model: it is structural,
and its severity varies per model. Neither BC#4 nor bootcamp calls the overlap out.

bootcamp records a separate conflict worth carrying into the design: **"The Kempston
joystick port #1F conflicts with the Beta 128 FDC command/status register."**

### 3.2 TR-DOS

ZXMAK2 suppresses the mouse entirely while TR-DOS is paged — all three handlers begin
`if (handled || (m_memory != null && m_memory.DOSEN)) return;`
(`KempstonMouseDevice.cs:204,213,224`).

Xpeccy encodes the same intent through a per-entry `dos` field: Pentagon, Scorpion and
ATM register the mouse with `dos=0` (non-DOS only), while Pentevo, ZX48 and Phoenix use
`dos=2` (don't care).

## 4. Button register

Buttons are **active-low** over an `0xFF` base — a pressed button reads as 0. All
sources agree on this.

Bit 3 reads as a constant **1**, per the FPGA's `{muswhl, 1'b1, musbtn}`
(`zkbdmus.v:107`).

**Bit assignment (bootcamp, decisive):**

```
Bit   7 6 5 4 3  2  1  0
      ? ? ? ? ?  MB MR ML
```

so **D0 = Left, D1 = Right, D2 = Middle**, active low.

This settles a question the emulators do not agree on. The surviving disagreement is
recorded because two widely-used emulators encode the opposite order, and anyone
comparing behaviour against them will see the difference:

- **D0 = right, D1 = left** — ZXMAK2 explicitly remaps to this
  (`KempstonMouseDevice.cs:209-210`):
  ```csharp
  b = ((b&1)<<1) | ((b&2)>>1) | (b&0xFC);
  value = b ^ 0xFF;  // D0 - right, D1 - left, D2 - middle
  ```
  zxsp's header comment says the same (`KempstonMouse.cpp:13`).
- **D0 = left, D1 = right** (agrees with bootcamp) — Xpeccy's default (`hardware/common.c:257-258`), with an
  explicit `swapButtons` flag to invert it (`src/libxpeccy/input/input.h:124`). zxsp's
  own embedded WoS table also contradicts its header:
  `[255 = None], [254 = Left], [253 = Right], [252 = Both]` (`KempstonMouse.cpp:31`),
  where 254 means D0 clear means Left.

Middle button is D2 where a third button exists. zxsp flags three-button support as
uncertain ("existed such a version?", `:17`) and drives only D0/D1, masking with
`mask |= 3` for the "2-button version" (`:56`). Xpeccy and the ZX Evo FPGA both
implement three.

## 5. Wheel

The wheel occupies the **upper nibble of the button register**, not a port of its own.

- FPGA: `muswhl <= mus_in[7:4]` (`zkbdmus.v:66-67`)
- Xpeccy: `res &= 0x0f; res |= ((wheel & 0x0f) << 4);` gated on `hasWheel`
  (`hardware/common.c:247-250`)

## 6. Axes

Both axes are **8-bit unsigned counters that wrap**. There is no saturation and no
clamping. UnrealSpeccyP states it plainest (`kempston_mouse.cpp:60-64`):

```cpp
x += _dx;
y += _dy;
```

on `byte` storage.

Host-delta scaling is entirely an emulator-side convention, and the references do not
agree:

| Emulator | Scaling | Source |
|---|---|---|
| ZXMAK2 | `x / 3`, and **Y negated**: `(byte)(-y / 3)` | `KempstonMouseDevice.cs:219,228` |
| zxsp | `x / scale`, `scale` default 2, live-adjustable | `KempstonMouse.cpp:40,71-76` |
| Xpeccy | `xpos * sensitivity` (a `double` — multiplies) | `common.c:264`, `input/input.h:126` |
| UnrealSpeccyP | none | `kempston_mouse.cpp` |

Note ZXMAK2's Y negation: screen Y grows downward while the mouse counter is treated as
growing upward. Any implementation must make its sign convention explicit.

## 7. Presence detection

There is **no presence bit**. Guest software reads the ports and infers.

The decisive detail is UnrealSpeccyP's reset (`kempston_mouse.cpp:26-31`):

```cpp
x = 31; // assign random different coords (some programs test this)
y = 85;
buttons = 0xFF;
```

Detection heuristics in real software compare X against Y and treat equal values as
"no mouse fitted" — so a zero-initialised mouse can read as absent. Any implementation
must reset to two different, non-zero values.

Xpeccy models absence explicitly: with `mouse->enable` clear, all three ports return
`0xFF` (`hardware/common.c:246,264,269`). It separately tracks `mouse->used`, set on any
read (`common.c:245`), purely to drive a host-side "mouse is being used" indicator —
that flag is never visible to the guest.

## 8. Variants

Across every source examined there are four axes of variation:

1. **Two-button vs three-button** — D2 present or absent.
2. **Wheel vs no wheel** — upper nibble live or reading as part of the `0xFF` base.
3. **Decode width / flavour** — per model and per card (§2). Original hardware decodes
   four lines; ZX Evo decodes twelve; every emulator picks something in between.
4. **USSR variant** — five lines, dropping A9 and adding A7 and A0 = 1, so it answers
   only on odd addresses (§2.1).

There is **no "Kempston mouse turbo"** in any of these sources; searches surface only
unrelated CPU-turbo hits. The AMX Mouse is a separate device with its own decode
(`zxsp/Source/Uni/Items/AmxMouse.h`) and does not share this port space.
