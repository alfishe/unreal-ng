# TurboSound FM — Hardware Reference

**Purpose:** normative description of what the board does, each claim tied to a primary source.
The design ([tsfm-tdd.md](tsfm-tdd.md)) implements this document; where they disagree, this document wins.
**Verified:** 2026-09-12.

## 0. Sources, ranked

| Rank | Source | Where | What it settles |
|---|---|---|---|
| 1 | NedoPC board logic (CPLD) source, 2022 and 2006 revisions | [materials/nedopc-cpld-2022/turbofm.tdf](materials/nedopc-cpld-2022/turbofm.tdf), [materials/nedopc-cpld-2006/turbofm.tdf](materials/nedopc-cpld-2006/turbofm.tdf) — from http://nedopc.com/TURBOSOUND/ts-fm.php | Port protocol, reset state, clock, FM mute |
| 1 | NedoPC rev. C schematic | [materials/tsfm-rev-c-schematic.png](materials/tsfm-rev-c-schematic.png) | Analog mix, reset wiring, clock wiring |
| 2 | NedoPC rev. C manual (`tfm_c.pdf`); ALCO *TFM programmer's guide* v1.11 (`tfm-prg.pdf`) | same site (not copied: binary PDFs) | Intended use, player protocol |
| 2 | TFM Compiler 1.2 player source `TFMCOM12.$H` (Alone Coder) | `tfmtools.zip`, same site | What real software does on the ports |
| 3 | Unreal Speccy (zx-evo fork) `io.cpp`, `sndrender/sndchip.cpp` | `/Volumes/TB4-4Tb/Projects/emulators/github/zx-evo/pentevo/unreal/Unreal/` | Behaviour of the reference emulator since 2007 |
| 3 | ymfm @ `81aec25c` | `/Volumes/TB4-4Tb/Projects/emulators/github/ymfm` | YM2203 chip internals (reverse-engineered) |
| 4 | MiSTer `rtl/turbosound.sv` (alfishe fork) | MiSTer ZX-Spectrum core | FPGA re-implementation. **Not ground truth** — it differs from the CPLD in three places (§3.4) |

Revision 1 of the design took the port protocol from rank 4. This document replaces it with rank 1.

---

## 1. What the board is

Two Yamaha YM2203 chips on a card that plugs into the AY-3-8910 (40-pin) or AY-3-8912 (28-pin) socket of the host machine. A small programmable logic chip (EPM7032S) sits between the socket and the two YM2203s.

Each YM2203 has two halves:
- **SSG** — a three-channel square-wave generator, register-compatible with the AY-3-8910 / YM2149 (registers `0x00–0x0F`);
- **FM** — three channels of four-operator FM synthesis (registers `0x10–0xFF`), feeding an external YM3014 DAC.

So the board is a normal TurboSound (two AY-compatible chips) plus two FM synthesizers.

It is an **add-on**: any machine with an AY socket can host it. A 48K has no AY socket and needs an AY interface first. ZX Evo rev. D added a TurboSound connector; several FPGA cores (TSConf, BaseConf, MiSTer ZX-Spectrum) have it built in.

---

## 2. Clock

**The board has no oscillator of its own.** It takes the AY clock pin from the socket and doubles it.

CPLD:
```
CLK1 : INPUT;  % AY clk generator %
CLK2OUT = INTDELAY_OUT xor CLK1;      % multiplied on 2 frequency %
```
Schematic: `CLK2` drives pin 38 (`CLK`) of both YM2203s. No crystal on the board.

So **YM2203 clock = 2 × host AY clock**:

| Host | AY clock | YM2203 clock | FM sample rate (default prescaler /6 → clock/72) |
|---|---|---|---|
| Pentagon, ZX Evo | 1.75 MHz | 3.5 MHz | 48 611 Hz |
| Sinclair 128K / +2 / +3 | 1.7734 MHz | 3.5469 MHz | 49 262 Hz |

Unreal Speccy does the same: `Chip2203->OPN.ST.clock = int(conf.sound.ayfq*2)`.

**What this means for the emulator.** unreal-ng derives the AY clock from the CPU: `PSG_CLOCK_RATE = CPU_CLOCK_RATE / 2` (`core/src/emulator/sound/audio.h:17-18`). The per-model physical frequency difference is already absorbed by the frame length. Therefore:

> **One YM2203 master clock = one CPU T-state, on every model.**

Worked example (default prescaler /6):
- FM sample period = 12 × 6 = **72 T-states**
- busy time after a data write = 32 × 6 = **192 T-states**
- one AY generator tick = 16 T-states, so one FM sample = 72 / 16 = **4.5 AY ticks**, on every model.

(ZX MultiSound is a different card with its own 3.5 MHz oscillator. Not covered here.)

---

## 3. Port protocol

### 3.1 How the socket signals map to Spectrum ports

The Spectrum drives the AY bus pins from port decoding:

| Z80 operation | AY bus phase (BDIR, BC2, BC1) | Meaning at the socket |
|---|---|---|
| `OUT (#FFFD), v` | 1, 1, 1 | latch address |
| `OUT (#BFFD), v` | 1, 1, 0 | write data |
| `IN (#FFFD)` | 0, 1, 1 | read |

BC2 is tied high on Spectrum-family machines. The CPLD notes "used only BC2=1 modes".

### 3.2 Control words (`OUT #FFFD` with value `0xF8–0xFF`)

CPLD:
```
_SELECT = _A9 or not(BC2 and BC1 and BDIR and A8 and DA[7..3] all 1) or not(_RES);
CUR_CHIP.d = DA[0];   GET_STAT.d = DA[1];   FM_DIS.d = DA[2];   (clocked by _SELECT)
_WR = not(BDIR and BC2 and A8) or _A9 or (BC1 and DA[7..3] all 1);
```

So an address-phase write whose top five bits are `11111` does two things:
- it updates three latches;
- it **does not reach either YM2203**: `_WR` is held inactive, so the chip's own address latch is untouched.

| Bit | Latch | 0 | 1 |
|---|---|---|---|
| 0 | `CUR_CHIP` | **first chip** (D1 on the schematic) | second chip (D2) |
| 1 | `GET_STAT` | `IN #FFFD` returns the **status** byte | `IN #FFFD` returns the selected **register** |
| 2 | `FM_DIS` | FM audio **enabled** | FM audio **muted** |

All eight values, with the chip naming used everywhere in this folder (chip 0 = first = D1):

| Value | Chip | IN #FFFD reads | FM audio |
|---|---|---|---|
| `0xFF` | 1 | register | muted |
| `0xFE` | **0** | register | muted |
| `0xFD` | 1 | status | muted |
| `0xFC` | 0 | status | muted |
| `0xFB` | 1 | register | on |
| `0xFA` | 0 | register | on |
| `0xF9` | 1 | status | on |
| `0xF8` | 0 | status | on |

Software agrees on this numbering. TFM Compiler 1.2 (rev. C mode): `statuschip0=%11111000`, `statuschip1=%11111001`. In the TFD music format, `#FD` means "second chip".

**Two old conventions to be aware of.** They are not implemented, only listed so nobody is surprised:
- rev. A boards could not mute FM; their software used `0xFC/0xFD`;
- TFM Compiler's `US031DX` build used `0xFE/0xFF` for status.

### 3.3 Reset state

The CPLD sets these latches on reset (`_RES` low):
- `CUR_CHIP.clrn = _RES` → 0 → **first chip** (the `0xFE` chip)
- `GET_STAT.prn = _RES` → 1 → register read
- `FM_DIS.prn = _RES` → 1 → FM muted

The rev. C manual says the same (FM blocked, register read, chip D1).

Equivalent control word: **`0xFE`**. Both YM2203 `/RES` pins (pin 24) are wired to host reset, so the chips reset too.

### 3.4 Address and data writes

- `OUT #FFFD, v` with `v < 0xF8` is latched as an 8-bit address into the **selected** YM2203, **regardless of the FM mute latch**.
- `OUT #BFFD, v` writes data to that chip's latched address, also regardless of the mute latch.

The mute latch only gates the serial stream to the DAC:
```
FM1_OUT = FM1_IN and not(FM_DIS.q);
FM2_OUT = FM2_IN and not(FM_DIS.q);
```

Consequences:
- With FM muted, FM registers **are still written**. Music set up while muted plays the moment FM is enabled.
- With an FM address (≥ `0x10`) latched, the SSG half ignores data. The previous SSG register is **not** kept selected.
- A control word does not change the latched address. ALCO §5.1: *"the pseudo-register is handled by the logic chip and never reaches the YM2203 — the current register does not change."*

**Where MiSTer `turbosound.sv` differs from the board.** Revision 1 of the design copied all three:

| MiSTer RTL | Board (CPLD) |
|---|---|
| Resets to the `0xFF` chip (`ay_select = 1`) | Resets to the `0xFE` chip |
| In AY mode, addresses ≥ `0x10` are not latched and data is dropped (`ym_acc`) | All addresses `< 0xF8` are latched in both modes |
| A control word clears the address-accepted flag | A control word does not touch the chip |

### 3.5 Reads

`IN #FFFD`, depending on `GET_STAT`:
- **status mode:** the YM2203 status byte;
- **register mode:** the selected register.

Status byte (ymfm `ymfm_opn.h:127-130`):

| Bit | Meaning |
|---|---|
| 7 | busy (a write is being processed) |
| 1 | timer B overflowed |
| 0 | timer A overflowed |

ALCO's table on p. 12 has bits 0/1 swapped; ymfm and the datasheet order are used.

Register-mode read with an FM address latched: **unknown on silicon.**

| Implementation | Returns |
|---|---|
| MiSTer (`ym2149.sv`) | `0xFF` |
| Unreal (`SNDCHIP::read`) | `0xFF` |
| ymfm | `0x00` |

The design uses `0xFF`, the majority emulator behaviour.

---

## 4. How software drives it

### 4.1 Players poll the busy flag

- **ALCO §5.1:** switch to status mode (`0xF8`/`0xF9`), wait until bit 7 = 0 before writing the address, and again before writing the data.
- **TFM Compiler 1.2:** every write goes through `WaitStatus` before the register and before the value.
- **Wild Player** (zx-evo) detects the board this way:
  1. write `0xF8`;
  2. write `0xBF` to register 0;
  3. read status: bit 7 = 0 means TFM is present.

**Consequence:** the busy flag must really set and really clear, on the right T-state.
- If it never clears, the player hangs.
- If it clears at the wrong time, the player uses a different amount of CPU time, and the program runs differently.

Unreal returns a constant `0x7F`: never busy, both timer flags always set. That works for players but is not faithful.

Writes made while busy are **not dropped**. AY-only software runs on the board without polling. Xpeccy's drop-while-busy is wrong for this board.

### 4.2 Players set the prescaler

At init, ALCO §5.3 and TFM Compiler 1.2 write address `0x2F`, then address `0x2D` (no data). ALCO says music plays too high without it.

Prescaler registers are address-only writes:

| Address | FM divider | SSG divider |
|---|---|---|
| `0x2D` | /6 (default) | /4 (default) |
| `0x2E` (only if currently /6) | /3 | /2 |
| `0x2F` | /2 | /1 |

The **FM** rate goes ×2 and ×3 relative to default; the **SSG** rate goes ×2 and ×4.
- ymfm applies the change on the address write (`ymfm_opn.cpp:794-803`).
- jt12 (MiSTer) applies it on the data write (`jt12_mmr.v:246-248`).

No known software stays on a non-default prescaler.

### 4.3 Timers and IRQ

- The YM2203 `IRQ` pin is **not connected** (schematic; ALCO p. 8).
- Timer flags are visible only through the status byte.
- No known player uses the timers.
- CSM mode (timer A keys channel 3) is implemented by ymfm and is reachable by software.

---

## 5. Analog output

### 5.1 Mixer (schematic, rev. C)

An inverting summing amplifier per side (LM358, feedback R21/R22 = 10 kΩ):

| Input | To L via | To R via | Weight (10 k / R) |
|---|---|---|---|
| SSG A of chip 0 and chip 1 | 24 kΩ each | — | 0.42 |
| SSG C of chip 0 and chip 1 | — | 24 kΩ each | 0.42 |
| SSG B of chip 0 and chip 1 | 47 kΩ each | 47 kΩ each | 0.21 |
| FM chip 0, FM chip 1 (after YM3014 + buffer + filter) | 24 kΩ each | 24 kΩ each | 0.42 |

Facts that follow:
- **Stereo is fixed ABC.** B is at half weight on both sides, the same shape as MiSTer's `2·A + B`.
- **FM is mono, centred, both chips summed.** Each FM output has the same resistor weight as SSG channel A.
- **Nothing saturates the chip sum.** MiSTer's 8-bit saturation is an FPGA artefact.
- SSG pins are loaded with 3.3 kΩ to ground (R1–R8).

### 5.2 FM-to-SSG loudness — estimate, not measured

The resistor weights are equal. The loudness ratio then depends on the voltage each source swings:
- **FM:** the YM3014 swings ±Vdd/4 ≈ **±1.25 V** at full scale (datasheet transfer function), then goes through a unity buffer and a 2.2 nF filter stage.
- **SSG:** a YM2203 SSG pin into 3.3 kΩ swings roughly 1 V peak-to-peak at full volume, so about ±0.5 V as AC. This is **unmeasured**.

Estimate: **FM full scale ≈ 2–2.5 × the AC amplitude of one SSG channel at full volume.**

For comparison:
- **MiSTer:** FM full scale ±512 against SSG A ±255 → ratio **2.0**. The estimate agrees with this.
- **Unreal:** calibrated by ear. Its history note reads "FM is 4 times more quiet as in real device"; formula `ay_vol/8192*0.1`.

What "FM full scale" means in practice (ymfm, measured):

| Case | FM word peak | Share of full scale |
|---|---|---|
| One carrier operator at TL=0 | ±8168 | ≈ ¼ (−12 dB) |
| One channel, algorithm 7, all four operators | ±32 672 | ≈ 1 |
| Three channels together | exceeds int16; the DAC word clamps | clamped |

### 5.3 FM mute

FM mute is **confirmed on hardware**: the DAC serial data is forced low (§3.4). Unreal mutes the same way (`fmsoundon0`).

With serial data low, the YM3014 receives exponent 0. Its output is then within 1/128 of full scale of mid-scale, so effectively silence plus a small DC step. The output coupling capacitors (C16/C17) remove that step. An emulator that mutes to 0 is therefore equivalent.

---

## 6. SSG half = YM2149

The YM2203 SSG uses the YM2149 32-step volume curve. This is agreed by every independent source, not measured on silicon:
- ALCO p. 8: "SSG fully equivalent to YM2149"
- ymfm SSG table (from MAME)
- MAME's YM2203 uses that table
- ZXMAK2 maps `Ym2203 → AmplitudeYm2149`

---

## 7. Open hardware questions

| # | Question | Impact | Default taken |
|---|---|---|---|
| H1 | Real FM-to-SSG loudness on rev. C | Mix balance | MiSTer ratio 2.0, configurable trim |
| H2 | Register read with FM address latched | `IN #FFFD` value | `0xFF` |
| H3 | Does a real YM2203 lose writes made while busy? | Only software that ignores busy | Never drop |
| H4 | Why ALCO says music plays too high without the `0x2F`/`0x2D` init | Power-on prescaler state | Reset → /6 |
| H5 | TSFM Pro (YM2203 + SAA1099) logic: does it forward `0xF0–0xF7` to the YM? | Only if Pro is added later | Out of scope |
| H6 | Prescaler applied on address write (ymfm) or data write (jt12)? | Co-simulation only | ymfm |
