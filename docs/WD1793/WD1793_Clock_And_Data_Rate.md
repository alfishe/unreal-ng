# WD1793 Controller Clock and Data Rate

How fast the WD1793 (Soviet clone: КР1818ВГ93, "VG93") steps the head, and which disks it can
read, depends on two separate settings: the **controller clock** and the **data rate** of the
board's data separator. This page explains both, walks through every case a Spectrum-family
machine can meet, and shows how unreal-ng models it.

Related pages:
- [WD1793_Timeouts.md](WD1793_Timeouts.md#controller-clock-and-data-rate-in-unreal-ng): the timer
  values per clock, in milliseconds and T-states.
- [WD1793_Command_Type_I.md](WD1793_Command_Type_I.md): step rates and the verify settle.
- [WD1793_Command_Write_Sector.md](WD1793_Command_Write_Sector.md),
  [WD1793_Command_Write_Track.md](WD1793_Command_Write_Track.md): Lost Data while writing.
- Research behind this page, with every source:
  [fdc-clock-and-data-rate research](../inprogress/2026-09-29-fdc-clock-and-data-rate/research.md).

Glossary (terms used below):
- **CLK**: the controller's own clock input (pin 24). 1 MHz on every standard Spectrum disk interface.
- **Data separator** (Russian "ФАПЧ", a PLL): the circuit on the board that turns the drive's raw
  pulses into bits and a read clock. Its frequency decides which bit rate can be read.
- **DD / HD**: double density (250 kbit/s; 720 KB on a 3.5" disk, TR-DOS 640 KB) and high density
  (500 kbit/s; 1.44 MB on a 3.5" disk).
- **Turbo VG**: board hardware that runs the chip at 2 MHz while the head moves.
- **DRQ**: "data request", the chip telling the CPU a byte is ready (or needed).
- **Lost Data**: status bit 2. The CPU did not take (or give) a byte before the next one was due.
- **Record Not Found (RNF)**: status bit 4. No matching sector ID was seen within the revolution limit.
- **T-state**: one Z80 clock cycle; 0.286 µs at 3.5 MHz.

---

## 1. The model in one table

| Setting | Set by | Changes | Does not change |
|---|---|---|---|
| **Controller clock** (`FdcClock`: 1 or 2 MHz) | the board: a fixed divider, turbo VG logic, or a machine latch | step period, head settle, the bit rate the chip **writes** | index pulses, time limits counted in revolutions, the rate the chip **reads** |
| **Data rate** (`FdcDataRate`: 250 or 500 kbit/s) | the board's data separator | which tracks can be read at all | the chip's timers |
| **Recorded rate of a track** | whoever wrote it, and whether the medium is DD or HD | what the separator must match | - |

The chip has **no "HD mode"**. The FD179x datasheet knows two clocks: 1 MHz for 5.25"/3.5"
("mini") drives and 2 MHz for 8" drives. The 8" MFM rate is 500 kbit/s, which is exactly the bit
rate of a 3.5" HD 1.44 MB disk. So an HD floppy is simply the 8" mode of the chip, used with an
HD drive and an HD data separator.

Why 18 sectors fit: at 500 kbit/s one revolution holds

- 300 rpm (3.5" HD): 500 000 bit/s × 0.2 s = 100 000 bits = **12 500 raw bytes**;
- 360 rpm (5.25" HD, and 8" drives): 500 000 × 0.1667 s = **~10 400 raw bytes**.

The controller does not know the rotation speed; it just reads whatever passes under the head.
18 sectors × 512 bytes plus gaps need about 11 500 bytes, so they fit on the 300 rpm track (the
360 rpm 5.25" HD format holds 15 sectors, 1.2 MB).

## 2. Reading comes from the separator, writing from the clock

- **Reading.** The datasheet says the read clock (RCLK) is "derived externally by phase lock loops,
  one shots, or counter techniques" (p.7). The board's separator decides the read rate. Doubling
  CLK alone does not make the chip read faster.
- **Writing.** The chip produces the write pulses itself, from CLK: "all times double when
  CLK = 1 MHz" (p.19). At 1 MHz it writes 250 kbit/s; at 2 MHz it writes 500 kbit/s.

This is why a turbo VG board must drop back to 1 MHz before it writes anything to a DD disk.

## 3. The cases

### 3.1 Standard interface, DD disk (Beta 128, Pentagon, Scorpion, ATM, Profi)

CLK 1 MHz, separator 250 kbit/s, DD disk.

- Step periods 6 / 12 / 20 / 30 ms; head settle 30 ms.
- One MFM byte every 32 µs = **112 T-states** at 3.5 MHz.
- The fastest TR-DOS transfer loop (`IN A,(#FF) : AND #C0 : JR Z : RET M : INI : JR`, at #3FE5 in
  TR-DOS 5.03 / 5.04T) costs 58 T per byte, 88 T in the worst case. 88 < 112, so it keeps up.

Worked example: SEEK from track 0 to track 40, `r1 r0 = 00`, verify on:
40 × 6 ms + 30 ms settle = **270 ms** (945 000 T).

### 3.2 Turbo VG: faster head positioning only (ZX-Evo, Karabas-Pro, Pentagon magazine mods)

The board switches CLK automatically, with no software involved:

- the STEP pulse switches CLK to 2 MHz;
- the first DRQ (ZX-Evo, Karabas-Pro), or the write gate (Pentagon mods), switches it back to 1 MHz.

The separator stays at 250 kbit/s the whole time. TR-DOS is unchanged.

Worked example, the same SEEK as in 3.1: 40 × 3 ms + 15 ms settle = **135 ms** (472 500 T), half
the time. The READ SECTOR that follows raises DRQ for its first byte, the clock is back at 1 MHz,
and the sector reads exactly as on a standard interface: 112 T per byte.

Why data transfer stays at 1 MHz:

- Writing at 2 MHz onto a DD disk lays a 500 kbit/s sector into a 250 kbit/s track. The DD
  separator cannot read it back, and the splice damages the neighbors. The first Pentagon turbo mod
  (Spectrofon #10) switched back too late and destroyed disks this way.
- Reading does not get faster anyway: the separator still delivers 250 kbit/s.
- It is **not** a CPU-speed limit. A DD disk simply has no 500 kbit/s stream to read.

### 3.3 Real HD: clock, separator and medium together (Sprinter, Pentagon AXLR mod)

HD needs all three: CLK 2 MHz, separator 500 kbit/s, and an HD drive with an HD disk.

- **Sprinter Sp2000**: `OUT (#BD),A`. The PLD decodes address line A13, which carries the value of
  A: A = `#21` (port `#21BD`) selects HD, A = `#01` (port `#01BD`) selects DD. In HD the PLD holds
  CLK at 2 MHz and feeds the separator from 14 MHz instead of 7 MHz.
- **Pentagon + AXLR HD mod** (Deja Vu #07/#09, 1999): port `#FF` bit 7 = 1 selects HD, with an extra
  16 MHz oscillator for the separator.

One HD byte arrives every 16 µs. In T-states:

| CPU clock | T-states per HD byte | TR-DOS loop (58-88 T) |
|---|---|---|
| 3.5 MHz | **56** | too slow: Lost Data |
| 7 MHz | 112 | fits |
| 14 MHz | 224 | fits |
| 21 MHz (Sprinter 2000) | 336 | fits easily |

### 3.4 CPU too slow: Lost Data

At 3.5 MHz the TR-DOS loop needs 58 T but a new byte comes every 56 T. The CPU falls 2 T further
behind with every byte. It starts at most ~28 T ahead, so within the first 15-30 bytes of each sector
a byte is overwritten before it was read: **Lost Data** on every sector. The AXLR article says it
plainly: "без турбы это не работает" ("without turbo it does not work"); the mod requires the 7 MHz
CPU turbo.

What the chip does (datasheet p.11-14):

- reading: sets Lost Data and keeps going to the end of the sector;
- WRITE SECTOR / WRITE TRACK: a missed **first** byte ends the command with Lost Data; a missed
  later byte is written as `00`, Lost Data is set, and the command continues.

### 3.5 Rate / medium mismatch: no address marks

A separator at 250 kbit/s cannot lock onto a 500 kbit/s track, and the other way round. The bit
stream it produces is noise; the sync pattern that starts every address mark never appears. So:

| Command | Result |
|---|---|
| READ SECTOR / WRITE SECTOR | Record Not Found after 4 revolutions (800 ms) |
| READ ADDRESS | Record Not Found after 5 revolutions (1 s) |
| Type I with verify (`V=1`) | Seek Error after 5 revolutions |
| READ TRACK | one revolution of noise, with no address mark in it |

Worked example: a 1.44 MB disk in a Pentagon drive. Every READ SECTOR ends after 800 ms with status
`#10` (Record Not Found).

The **Sprinter BIOS density probe** relies on this: it issues READ ADDRESS at one rate; on Record
Not Found it flips the `#BD` latch to the other rate and tries again. A DD disk answers at DD, an HD
disk at HD.

## 4. How unreal-ng models it

Code: `core/src/emulator/io/fdc/fdc.h` (the enums), `core/src/emulator/io/fdc/wd1793.{h,cpp}`,
`core/src/emulator/io/fdc/diskimage.h` (track density). Tests:
`core/tests/emulator/io/fdc/wd1793_clock_test.cpp`.

### 4.1 Clock policies

`FdcClockPolicy` says who drives the clock:

| Policy | Behavior |
|---|---|
| `Fixed1MHz` (default) | always 1 MHz |
| `AutoStepTurbo` | every step pulse selects 2 MHz; the next DRQ selects 1 MHz. A chip reset returns to 1 MHz. The data rate is not touched |
| `Latched` | clock **and** data rate change only through `WD1793::SetLatchedClock(FdcClock, FdcDataRate)`, called by the machine's latch. STEP and DRQ change nothing |

The data-rate check is always on, for every policy: a track is read only when its recorded rate
equals the controller's data rate. The default data rate is 250 kbit/s (DD).

### 4.2 Which machine uses which policy

| unreal-ng model | Policy | Why (evidence) |
|---|---|---|
| `PENTAGON` (128/512/1024) | `Fixed1MHz` | CLK = 8 MHz / 8, separator 250 kHz (KoE Pentagon 1024SL v2.2 CPLD `p1024sl2.tdf`; MAME `beta_m.cpp`). The turbo VG magazine mods are optional: use `TurboVG=1` |
| `48K`, `128k`, `PLUS2` with Beta 128 | `Fixed1MHz` | the Beta 128 interface divides its crystal to a fixed 1 MHz (MAME `beta_m.cpp`) |
| `SCORPION`, `PROFSCORP` | `Fixed1MHz` | "ВГ93 has its own 1 MHz clock" (Scorpion ZS256 Turbo+ docs) |
| `ATM710` | `Fixed1MHz` | 1 MHz from the 8 MHz crystal chain, no turbo VG circuit (ATM Turbo 2+ assembly manual) |
| `ATM3` (ZX-Evo BaseConf) | `AutoStepTurbo` | FPGA `vg93.v`: the STEP rising edge sets `turbo_state` (CLK 28 MHz / 14 = 2 MHz), the first DRQ clears it; the separator stays at 250 kHz. Override in `PortDecoder_ATM3::DefaultFdcClockPolicy()` |
| `PROFI` | `Fixed1MHz` | no evidence of turbo VG on the original Profi. Its FPGA clone Karabas-Pro has STEP/DRQ turbo VG (can be switched off by port `#028B` bit 2); use `TurboVG=1` to model it |
| `PLUS2A`, `PLUS3` | (not used) | these read disks through the uPD765, not the WD1793 |
| Sprinter (planned) | `Latched` | port `#BD` latch sets CLK and separator together (PLD `SP2_MAX.TDF`); see the [Sprinter storage design](../inprogress/2026-09-28-sprinter/tdd-storage.md) §2.3 |

### 4.3 The `[Beta128] TurboVG=` option

In `unreal.ini`:

```ini
[Beta128]
TurboVG=1    ; 1 = AutoStepTurbo (Pentagon turbo VG mods, Karabas-Pro); 0 = Fixed1MHz
```

Leave the key out to keep the machine's own policy. A `Latched` machine ignores the option: its
clock belongs to the machine latch.

### 4.4 Track density

Disk images carry no density field. A track's recorded rate comes from its raw length and encoding
(`DiskImage::RawTrack::RecordedDataRate()`):

| Encoding | DD nominal (250 kbit/s, 300 rpm) | HD nominal (500 kbit/s) | HD when the track is at least |
|---|---|---|---|
| MFM | 6 250 bytes | 12 500 bytes | **9 375** bytes (1.5 × 6 250) |
| FM | 3 125 bytes | 6 250 bytes | **4 688** bytes (1.5 × 3 125) |

Real DD dumps are 6 208 to 6 464 MFM bytes long and stay DD. A loader that builds an HD disk (for
example a raw 1.44 MB PC image) must create 12 500-byte tracks; then the density follows by itself.
WRITE TRACK formats at the rate of the current clock: at 2 MHz the track becomes a 12 500-byte HD
track.

When the rates match, one byte takes one revolution divided by the track length: 112 T on a
6 250-byte track, 56 T on a 12 500-byte track (3.5 MHz T-states).

### 4.5 Adding a new machine

1. **Fixed 1 MHz**: nothing to do; it is the default.
2. **Automatic turbo VG** (STEP to 2 MHz, DRQ back to 1 MHz): override
   `PortDecoder::DefaultFdcClockPolicy()` in the machine's decoder and return
   `FdcClockPolicy::AutoStepTurbo`. `Core::Init` applies it (combined with `[Beta128] TurboVG=`
   through `WD1793::ResolveClockPolicy`).
3. **A density latch** (Sprinter, AXLR-style): return `FdcClockPolicy::Latched`, and in the port
   handler for the latch call, for example:

   ```cpp
   // Sprinter OUT (#BD),A: A13 = 1 (A = #21) selects HD
   fdc->SetLatchedClock(hd ? FdcClock::Clock2MHz : FdcClock::Clock1MHz,
                        hd ? FdcDataRate::Rate500Kbps : FdcDataRate::Rate250Kbps);
   ```

   Reset the latch to DD on machine reset. `SetLatchedClock` returns `false` and changes nothing
   under any other policy.

The policy, the clock and the data rate are saved in TTD checkpoints (WD1793 blob, 254 bytes) and
shown in the FDC device-state report.

### 4.6 Not modeled

- Turbo VG that ends at the read/write strobe instead of DRQ (Sprinter in DD mode, Pentagon mods
  switched by the write gate). `AutoStepTurbo` ends at DRQ; for a DD disk the difference is only
  when positioning ends, not the data.
- The broken Spectrofon #10 mod (writing at 2 MHz onto DD disks).
