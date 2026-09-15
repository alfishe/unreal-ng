# Kempston Mouse — Architecture & Design (TDD)

Normative hardware behaviour: [hardware-reference.md](hardware-reference.md).
Status, decisions log and open questions: [README.md](README.md).

> **Status (2026-09-12): implemented.** This was written before the code. It has been kept
> as the design record, with **"As built"** notes wherever the implementation differs or a
> decision was settled. Line citations were refreshed against the working tree on
> 2026-09-12; the mouse files were still uncommitted then, so lines can move.

---

## 1. Design principles

1. **Feature-gated and model-gated.** The device costs nothing when absent: a cached
   `bool` on the port decoder, checked once per `IN`, following the
   `Memory::UpdateFeatureCache()` pattern (`core/src/emulator/memory/memory.cpp:1736`).
2. **One shared implementation, per-model decode.** The device object and its register
   semantics are model-independent; only the address mask differs per machine (§3).
3. **Host-independent pointer mapping.** One physical host pixel of cursor travel maps
   to one emulated screen pixel, independent of monitor DPI, window size and upscale
   factor (§5). This is a deliberate divergence from every reference emulator, all of
   which use a fixed divisor or multiplier.
4. **Deterministic under TTD.** Mouse input is host-asynchronous, so it must be
   journalled and replayed exactly like keystrokes or replay diverges (§6).
5. **No silent choices on contested hardware.** Where the references disagree — button
   order above all — the behaviour is configurable and the default is argued, not
   assumed.

## 2. What we implement

Per [hardware-reference.md §8](hardware-reference.md#8-variants) there are four axes of
variation. The design covers them through configuration and an overridable per-model
decode predicate, rather than separate device classes:

| Variant axis | Config | Default |
|---|---|---|
| 2-button / 3-button | `mouse.buttons` | 3 (D2 = middle) |
| Wheel mode | `mousewheel` (enum, see §7) | ~~`MOUSE_WHEEL_KEMPSTON`~~ **`MOUSE_WHEEL_NONE`** (see §7.2) |
| Button order | `mouse.swap_buttons` | see §4.2 |
| Decode flavour | per-model decoder, overridable (§3.1) | standard |
| USSR strict decode | `mouse.ussr_variant` | off |
| Joystick-on-mouse | `joymouse` (see §7) | off (deferred) |

**As built:** only the wheel mode (`[INPUT] Wheel=`) and the device type (`[INPUT] Mouse=`)
have an effect. The 2-button option and the USSR variant were not implemented; always 3
buttons, always the standard decode. `SwapMouse=` and `MouseScale=` are applied by the desktop
`MouseManager` when capture starts; `joymouse` is deferred.

There is no "Kempston mouse turbo" to implement, and AMX Mouse is out of scope.

The USSR variant is a fitting, not a machine property, hence configuration rather than a
model override ([hardware-reference §2.1](hardware-reference.md#21-known-documented-decodes-bc4)).

## 3. Port decoding

### 3.1 Decoding is per-model, by construction

**Each model decoder owns its own mouse decode predicate.** This is not a generalisation
we are choosing to allow — it is what the hardware does. BC#4 shows the Kempston
*joystick* decoded four different ways across four machines, one of them (KAY-1024SL)
answering on A0 alone; and ZX Evo decodes the mouse's whole low byte where original
hardware decodes only A5
([hardware-reference §2](hardware-reference.md#2-address-decoding-is-per-model)).

The architecture therefore is:

- A shared **default predicate** implementing the standard decode, used by any model with
  no documented deviation.
- Any model decoder may **override** it with its own bit sensitivity.
- The predicate returns *which register* was selected, not merely yes/no, because the
  register selection is part of the per-model pattern.

```
Standard (as built: MiSTer ZX-Spectrum mouse.v / kemp_sel, 2026-09-12):
    qualify:  A9 = 1, A5-A0 = 011111    (low byte #1F, #5F, #9F or #DF)
    select :  A8 = 0          -> buttons   (A10 don't-care: #FADF and #FEDF both answer)
              A8 = 1, A10 = 0 -> X
              A8 = 1, A10 = 1 -> Y

USSR variant:
    qualify:  A7 = 1, A5 = 0, A0 = 1       (A9 NOT decoded)
    select :  A8 = 0, A10 = 0 -> buttons
              A8 = 1, A10 = 0 -> X
              A8 = 1, A10 = 1 -> Y
```

Note what this rules out: a "low byte == 0xDF" test is **wrong** as a general rule. It is
ZX Evo's behaviour only, and using it everywhere would silently break every mirrored
address that real hardware answers.

**Our standard predicate is more faithful than the ancestor emulator's.** Unreal Speccy
itself (`io.cpp:962`) uses a 14-bit approximation: `port |= 0xFA00` (force A15=1, A13=1)
then exact-compares against three canonical addresses. That excludes mirrors like
`#7ADF` (A15=0) that the schematic says should answer. UnrealSpeccyP's qualifier
(`kempston_mouse.cpp:37-41`) is closer: it gates on `A5 = 0` first, then normalises and
compares — equivalent to the standard decode. The predicate above matches the schematic
directly.

**As built (2026-09-12): aligned with MiSTer, not with the schematic reading.** The first
implementation qualified on A9 = 1, A5 = 0 only. Checked against four references for the
Scorpion ProfROM case (Unreal Speccy, Xpeccy-plus, ZXMAK2, MiSTer), none answers an address
such as `#03C1`: all require the low bits set. MiSTer's decode (`kemp_sel = addr[5:0] ==
6'h1F`, register select `addr[10:8]`: `011` X, `111` Y, `x10` buttons) keeps the A9 = 1 rule
and the `#FEDF` buttons alias, and is now the predicate
(`PortDecoder::Standard_IsPort_KempstonMouse`). Worked example: `#FBDF`, `#FB9F`, `#FB5F`,
`#FB1F` read X; `#FBDE`, `#03C1`, `#FBCF` are not mouse addresses.

### 3.2 Per-model decode matrix

Built from the real `MEM_MODEL` enum (`platform.h:301+`) against BC#4's model column.
Mirror counts follow from the decode as built: 7 free lines (A15-A11, A7, A6) for each
axis, 8 for buttons (A10 as well). The standard rows used to read 8192 / 4096 / 4096 under
the earlier A9/A5-only decode.

| Our model | BC#4 | Documented? | Qualify | Select | Mirrors (B / X / Y) |
|---|---|---|---|---|---|
| `MM_SPECTRUM48` | 1/+1 | **yes** | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_KAY` | 7 | **yes** | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_PENTAGON` | 8, D | **yes** | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_PROFI` | 9 | **yes** | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_SPECTRUM128` | 2 | no — add-on | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_PLUS3` | 3/+3 | no — add-on | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_SCORP`, `MM_PROFSCORP` | 6 | no — add-on | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_ATM710` | A | no — add-on | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_GMX` | B | no — add-on | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_QUORUM` | C | no — add-on | A9=1, A5-A0=011111 | A8, A10 | 256 / 128 / 128 |
| `MM_ATM3` (ZX Evo baseconf) | — | FPGA | **full low byte `#DF`** | A8, A10 | **128 / 64 / 64** |
| `MM_TSL`, `MM_ATM450`, `MM_LSY256`, `MM_PHOENIX`, `MM_NEXT` | — | unknown | default | A8, A10 | as standard |
| *USSR fitting* (any model) | — | variant | A7=1, A5=0, A0=1 (**A9 not decoded**) | A8, A10 | 2048 / 2048 / 2048 |

Three things this matrix makes visible that a prose description hid:

- **`MM_ATM710` is not ZX Evo.** ATM Turbo 2+ v7.10 (BC#4 model A) and ZX Evo baseconf
  are different machines and only the latter has the FPGA's strict decode. Grouping them
  is the same mistake that put Pentevo's turbo formula into the ATM710 decoder — a bug
  fixed in this project on 2026-09-12.
- **ZX Evo is stricter but not exact.** It decodes ten lines, leaving 64 mirrors per axis
  and 128 for buttons — not a single exact port. Anything asserting one exact port match
  is wrong.
- **`MM_KAY` is one of the four documented machines** (BC#4 model 7) and it is the machine
  whose *joystick* decodes A0 alone, so it has both the documented mouse and the worst
  joystick overlap (§3.3).

> **D-3 remains open.** Ten of the rows above are "add-on" or "unknown" — BC#4 documents
> the mouse on four machines only. The standard decode is the proposed default for the
> rest, and the predicate is overridable per decoder so a counter-example costs one
> override. This is an assumption, not verified hardware parity.

### 3.3 The Kempston joystick collision

Per [hardware-reference §3.1](hardware-reference.md#31-kempston-joystick), the mouse
sits inside the joystick's A5-only decode window. Two resolutions exist in the wild; we
take the second:

- **Exclusion list** (UnrealSpeccyP): the joystick handler explicitly rejects the three
  mouse ports.
- **Narrow the joystick** (Xpeccy, ZX Evo FPGA): decode the joystick on a full low byte
  of `0x1F`.

**Decision (original):** narrow the joystick. It matches the FPGA, needs no cross-device knowledge,
and leaves the two devices independently testable. This is a behavioural change to the
existing joystick stub (`portdecoder_scorpion256.cpp:620`, currently `port == 0xFF1F`)
and must be covered by a regression test asserting the joystick still answers its own
ports.

**As built — the joystick was not narrowed.** The only Kempston joystick in this branch is
the Scorpion stub, and it already matched **exactly one address, `#FF1F`**
(`PortDecoder_Scorpion256::IsPort_KempstonJoystick`, `portdecoder_scorpion256.cpp:621-639`),
which is narrower than a `0x1F` low-byte match. `#FF1F` does satisfy the mouse decode
(A9 = 1, A5 = 0), so the overlap is resolved by **order**: in `DecodePortIn` the joystick arm
comes first (`:217`) and the mouse arm after it (`:230`). Worked example: `IN #FF1F` → joystick
(`0x00`); `IN #FB1F` → mouse X counter (not the joystick, which checks all 16 bits). The
other model decoders have no joystick arm to collide with. Regression test:
`PortDecoder_PentagonScorpionMouse_Test.Scorpion256_JoystickNarrowedTo1F` (the name is left
over from the original plan; it asserts the exact `#FF1F` behaviour).

### 3.4 Existing stub to remove

`PortDecoder_Scorpion256` already claims this space with a stub:
`IsPort_KempstonMouse()` matches `(port & 0x00FF) == 0x00DF` — *any* port with low byte
`#DF` — and `DecodePortIn` returns `0xFF` for buttons and `0x00` for X/Y
(`portdecoder_scorpion256.cpp:214-222`, `:640-645`). Note `0x00` on both axes is exactly
the "equal axes" pattern that presence-detection heuristics read as *absent*
([hardware-reference §7](hardware-reference.md#7-presence-detection)). The stub is
replaced, not extended.

**As built:** replaced. The Scorpion now calls the shared standard decode
(`Default_IsPort_KempstonMouse`) and returns the real device registers. The old low-byte
`#DF` test is gone from every decoder.

### 3.5 TR-DOS gating

> **Decided (2026-09-12): common rule.** While TR-DOS is selected, only Beta Disk operations
> happen - no other device answers on any address until the selection signal is released.
> Implemented for the mouse on every model (`CF_DOSPORTS`), and on Scorpion for a TR-DOS
> session (`CF_TRDOS`) or the armed DOS trigger. The Scorpion Shadow Monitor latch (`#1FFD`
> bit 1) on its own does **not** hide `#xxDF` (MiSTer alignment, below). The analysis below is
> kept as background; the open question D-4 is closed.

ZXMAK2 suppresses the mouse while TR-DOS is paged; Xpeccy does the same for
Pentagon/Scorpion/ATM but not for Pentevo/ZX48/Phoenix
([hardware-reference §3.2](hardware-reference.md#32-tr-dos)).

**A plausible hardware mechanism.** The low five bits of `#DF` are `11111` — the same
qualifier the Beta 128 FDC uses (`0xDF & 0x1F == 0x1F`). So a mouse read presents the
Beta's own address qualifier to the bus, and on any Beta implementation that decodes the
system/drive register loosely (A7 = 1 rather than a full A7:A5 match) the FDC would drive
the bus at the same time as the mouse card. That is exactly the kind of contention an
emulator would paper over by suppressing one device.

Two caveats keep this short of proof:

- On a **fully decoded** Beta, `#DF` has A7:A5 = `110`, which is not one of the five
  defined registers (`000` `#1F`, `001` `#3F`, `010` `#5F`, `011` `#7F`, `111` `#FF`), so
  there would be no collision at all. The mechanism only bites on loose decodes.
- Our own `PortDecoder::IsBeta128Port` matches those five ports **exactly**
  (`portdecoder.cpp:514`), so no collision exists in this emulator today regardless.

> **Open question (D-4, strengthened).** The mechanism above explains *why* ZXMAK2 and
> Xpeccy gate, without settling whether real hardware does. Proposed: gate behind a
> per-model flag defaulting *on* for Pentagon/Scorpion/ATM and *off* elsewhere, matching
> Xpeccy's `dos` field.
>
> **Flag name matters here.** The gate is `CF_DOSPORTS` (`0x01`, "tr-dos ports are
> accessible") — the flag that actually puts the FDC on the bus — **not** `CF_TRDOS`
> (`0x02`, the DOSEN trigger). They are different flags with different lifetimes
> (`platform.h:973-974`).

**As built:** gated on **every** model, not per-model. `PortDecoder::Default_IsPort_KempstonMouse`
(`portdecoder.cpp:494-503`) refuses the address when the mouse is not fitted, when
`CF_DOSPORTS` is set, or when a registered peripheral owns that exact port. Two model
details on top of that:
- **Pentagon 128/512:** the mouse is tried only for addresses the decode table left
  unclaimed (`portdecoder_pentagon128.cpp:105-111`).
- **Scorpion** (MiSTer ScorpionZS256 `d98b9ea`, `ZX-Spectrum.sv:1019-1020, 1197-1199`):
  - *TR-DOS selected* (`CF_TRDOS` or the armed DOS trigger, `ScorpionTrDosSelected`): the
    Beta interface decodes only A2-A0 = `111`, like MiSTer's `fdd_sel`. A7 = 1 selects the
    system register, A7 = 0 the WD1793 register by A6-A5. Every mouse address has A2-A0 =
    `111`, so the FDC owns all of them. Worked example: `IN #FBDF` returns what `IN #FF`
    returns (`{INTRQ, DRQ, #3F}`); `IN #0347` returns the sector register, like `#5F`.
  - *Shadow Monitor latch only* (`#1FFD` bit 1, TR-DOS not selected): `#xxDF` and `#xx9F`
    stay the mouse. The exact Beta low bytes `#1F`/`#3F`/`#5F`/`#7F`/`#FF` belong to the FDC,
    which this emulator keeps on the bus under the latch (hardware-reference 12.3). MiSTer
    masks `kemp_sel` on those bytes (`~(scorp & beta_port & scorp_1ffd[1])`). Worked example:
    `IN #FB5F` with the latch set reads the sector register, not X.
  - *Joystick stub `#FF1F`*: idle `#00` with the latch clear (the ProfROM driver plane, page 5,
    `#1FFD = #10`). With the latch set it yields to the FDC status. MiSTer masks it the same
    way, because ROM2 polls the WD1793 status through `#xx1F` (`#0234`).
  - A trace of the ProfROM service-monitor tests showed every joystick read (`PC #0263`) and
    mouse read (`PC #021F/#022E/#023C/#08FF`) happening with `#1FFD = #10` and TR-DOS off.
    None of these changes affects the menu or pointer driver.

## 4. Device object

### 4.1 Placement and wiring

Follows `Keyboard` exactly — the established shared-peripheral pattern:

- Class at `core/src/emulator/io/mouse/mouse.{h,cpp}`, constructed in `Core::Init`
  (`core.cpp:143-157`) and hung on `EmulatorContext::pMouse`, beside `pKeyboard`
  (`emulatorcontext.h:85`).
- Port decoders cache a `Mouse* _mouse` beside the existing `Keyboard* _keyboard`
  (`portdecoder.h:136`, assigned `portdecoder.cpp:34`).
- A shared `PortDecoder::Default_Port_KempstonMouse_In(port, pc)` mirrors
  `Default_Port_FE_In` (`portdecoder.cpp:505-511`), next to the pure address test
  `Standard_IsPort_KempstonMouse` (`:476-488`) and the gated test
  `Default_IsPort_KempstonMouse` (`:494-503`).
- **As built:** `Core::Reset` only re-applies the mouse config. RESET does not reach the
  interface: counters, buttons and wheel keep their values, and power-on values are set
  once, when the device is constructed. This matches MiSTer, where `mouse.v` resets on
  `cold_reset` only. Unreal Speccy, Xpeccy-plus and ZXMAK2 do not reset it either.

Each model decoder adds one arm to its own `DecodePortIn`. There is no common
`DecodePortIn` to hook — every model implements its own (`spectrum48.cpp:79`,
`spectrum128.cpp:98`, `spectrum3.cpp:92`, `pentagon128.cpp:111`, `scorpion256.cpp:190`,
`profi.cpp:89`, plus ATM710/ATM3 on the `atm` branch), so "every model gets it" is seven
to nine small edits, not one.

The exact-port `RegisterPortHandler` / `PeripheralPortIn` mechanism
(`portdecoder.cpp:593-620`) is **not** usable here: it keys on a full 16-bit port and
cannot express a masked decode without registering thousands of entries.

### 4.2 State and register semantics

```
uint8_t  _x, _y;        // 8-bit wrapping counters, unsigned
uint8_t  _buttons;      // active-low, 0xFF = none pressed
uint8_t  _wheel;        // 4 bits, upper nibble of the button register
bool     _present;
```

Register assembly follows the FPGA: `{wheel[3:0], 1'b1, buttons[2:0]}`, so bit 3 is a
constant 1.

**As built** (`mouse.h:144-149`, `mouse.cpp:72-101`):
- All fields are `std::atomic`. Input arrives on more than one thread (MessageCenter worker
  for the desktop, the automation thread, the emulator thread for click release), and
  `Move`/`SetWheel` are compare-and-swap loops so two simultaneous moves both land.
- `_present` and a new `_wheelEnabled` are **fitting**, taken from the config, not device
  state: `Reset()` keeps them and TTD does not save them.
- The wheel nibble is on the bus only when a wheel is fitted. Without one the register is
  `0xF8 | buttons`: bits 3–7 read 1, like a classic wheel-less Kempston mouse (§7.2).

**Axis sign convention — state it explicitly or it will be inverted.** Host screen Y grows
downward while the counter is treated as growing upward, so Y needs a negation somewhere
and the only question is where:

```
host cursor moves RIGHT  ->  _x increases      (+dx as delivered)
host cursor moves UP     ->  _y increases      (-dy as delivered, screen Y is inverted)
```

ZXMAK2 is the only reference that states this in code, negating Y explicitly
(`(byte)(-y / 3)`, [hardware-reference §6](hardware-reference.md#6-axes)). It is a
convention rather than a proven hardware fact — a quadrature mouse reports whatever its
wiring dictates — so like the button order before bootcamp settled it, this wants
validating against real software. The negation belongs in the host mapping layer (§5), not
in the device, so the device stays a pure counter.

Reset must use **two different non-zero values** for X and Y — UnrealSpeccyP uses
`x = 31, y = 85` precisely because software tests for equal axes to infer absence. We
adopt those values and cite the reason in the code, or a later "tidy-up" will zero them
and silently break detection. (MiSTer uses X = 128, Y = 0 for the same reason. Only the
power-on state uses them; a machine RESET keeps the counters, §4.1.)

Counters **wrap** at 8 bits. MiSTer clamps at 0 and 255 instead. We don't copy that: a
driver computing deltas from a stuck counter stops moving the pointer in that direction.

**Button order — settled.** bootcamp and BC#4 give the bit map directly:
`MB MR ML` at bits 2/1/0, i.e. **D0 = Left, D1 = Right, D2 = Middle**, active low
([hardware-reference §4](hardware-reference.md#4-button-register)).

`mouse.swap_buttons` still exists, but as a user preference rather than a hedge against
unknown hardware; the dead `CONFIG::input.mouseswap` field (`platform.h:538`) is its
home. **As built:** `SwapMouse=` is parsed into it (`config.cpp:340`) and the desktop `MouseManager`
swaps left/right when capture starts.

**As built — sign convention.** `dy` + = up is implemented: the Qt layer negates screen Y
(`mousemanager.cpp`, `applyHostMotion`), automation passes `dy` through unchanged. Checked
against one real program: the Scorpion ProfROM service-monitor pointer moves right/down
for `Move(+6, -4)` and back for `Move(-6, +4)`
(`scorpion_kempston_mouse_test.cpp`, `ProfRomServiceMonitorPointerFollowsMouse`). Note ZXMAK2 and zxsp's header comment encode the opposite order, so behaviour
compared against those two emulators will differ — that is them, not us.

### 4.3 Absence

With `_present` false all three registers return `0xFF`, matching Xpeccy's
`mouse->enable` behaviour. Note this makes X and Y equal, which is exactly the "absent"
signature software looks for — correct by construction.

**As built:** "absent" means the decoders do not claim the mouse addresses at all, so the
read falls through to whatever else answers or to the floating bus. `Mouse::ReadRegister`
still returns `0xFF` when absent (`mouse.cpp:74-77`), which is what the automation status
reports as port values.

## 5. Host pointer mapping

This is the part that determines whether the emulated pointer *feels* right, and it is
where we deliberately diverge from every reference emulator.

### 5.1 The rule

**One physical pixel of host cursor travel equals one pixel of emulated screen travel.**
The result then depends on neither monitor DPI, nor window size, nor upscale mode, and
it inherits whatever pointer ballistics the user has already tuned for themselves at OS
level.

```cpp
delta_phys   = delta_logical * devicePixelRatioF();  // Qt reports logical px
delta_native = delta_phys / upscale;                 // upscale = window / framebuffer
acc         += delta_native * scale;                 // scale = 2^mousescale, default 1.0
int step = (int)acc;
acc -= step;                                         // carry the remainder
```

where `scale` is derived from `mousescale` (see §7) as `pow(2, mousescale)`, so
`mousescale = 0` → 1.0 (identity), `mousescale = 1` → 2.0 (double speed), etc.

### 5.2 Three non-obvious traps

Everything below is a trap that implementations routinely fall into.

1. **The accumulator must be floating point.** At 3× upscale one host pixel is 0.33
   emulated pixels. An integer delta truncates to zero and slow movements die
   completely — not "feel sluggish", but produce no motion at all.
2. **Fractional device pixel ratios are real.** 150% scaling on Windows gives 1.5.
   `devicePixelRatio()` (the `int` overload) rounds it away; `devicePixelRatioF()` must
   be used. Our codebase currently calls the int version in one unrelated place
   (`tintedsvgicon.cpp:12`) and `devicePixelRatioF()` nowhere at all — so there is no
   existing precedent to copy, and the wrong one is easy to reach for.
3. **The remainder must be carried.** Dropping `acc -= step` loses sub-pixel motion on
   every event and turns smooth travel into stutter.

The sensitivity adjustment does not disappear, but it becomes a **power-of-two coarse
scale** (`mousescale`, §7) rather than a fine multiplier — the framing is "nudge this if
you dislike the feel", not "tune this until the mouse becomes usable". The default of 0
(= identity) means no scaling at all; the legacy range `[-3; 3]` covers ⅛× to 8× in
octave steps, which is coarse enough that a fine `float trim` is unnecessary. If a
finer-grained control is ever needed, it can be added later without changing the config
format.

### 5.3 Why not the reference approach

ZXMAK2 divides by 3, zxsp by 2, Xpeccy multiplies by a `double` sensitivity, and
UnrealSpeccyP does nothing at all
([hardware-reference §6](hardware-reference.md#6-axes)). All four are window- and
DPI-dependent: the same physical hand movement produces different emulated travel as
soon as the user resizes the window or moves to another monitor. §5.1 is scale-invariant
by construction.

Xpeccy's approach has a further flaw: `sensitivity` is applied at **read time** on an
unbounded accumulator (`xpos * sensitivity`, `common.c:264`), not at injection time.
When the guest polls faster than motion accumulates, consecutive reads return identical
truncated values even while the mouse is moving, because sub-pixel deltas hide in the
fractional part. Applying scaling at injection time and carrying the remainder (§5.1)
avoids this.

### 5.4 OS ballistics: a fork in the road, taken deliberately

Kempston Mouse software is **pointer-style**: Art Studio, Nether Earth and Prince draw
their own cursor and the user tracks it with their eyes. That demands *system*
acceleration, so the emulated pointer feels like every other pointer on the machine.

- **Qt already gives us this.** Qt's deltas carry the OS ballistics curve, so the
  correct behaviour is what we get by doing nothing special.
- **SDL would not.** SDL delivers raw deltas by default and needs
  `SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE` set explicitly.
- Raw deltas are right only for **mouse-look** (Amiga titles, Quake-likes). The Kempston
  Mouse never does mouse-look.

This is recorded as a decision precisely so that nobody later "fixes" it into raw
deltas. For what an SDL front end must do to reach the same behaviour, see
[§9](#9-if-the-front-end-is-sdl-rather-than-qt).

### 5.5 Where upscale is known

Both terms live in `DeviceScreen`: the framebuffer rect is `devicePixelsRect`
(`devicescreen.cpp:40`) and the crop is `_displayViewport` / `_hasViewport`
(`devicescreen.h:108`), applied to the source rect in `paintEvent`
(`devicescreen.cpp:101-109`). So `upscale = widget size / (framebuffer − viewport crop)`,
and the conversion belongs in `DeviceScreen`.

Note the widget aspect is *fixed* at `kNativeWidth/kNativeHeight = 352/288`
(`devicescreen.h:51-54`) regardless of framebuffer or viewport, so the X and Y upscale
factors are **not** necessarily equal and must be computed independently.

`DeviceScreenWrapper` forwards to either the software `DeviceScreen` or the GL path, so
any new input entry point must be added to the wrapper as well
(`widgets/devicescreenwrapper.{h,cpp}`) — the ProfROM viewport bug earlier in this
project came from exactly that wrapper/implementation split.

**As built:** the host side lives in a separate `MouseManager`
(`unreal-qt/src/emulator/mousemanager.{h,cpp}`), owned by `DeviceScreen`. `DeviceScreen`
gives it a callback returning `displaySourceRect().size()` — the emulated-pixel size of the
image actually drawn, crop included — and forwards its Qt mouse, wheel, key and focus
events. The accumulator itself is Qt-free (`core/src/emulator/io/mouse/mousedeltaaccumulator.h`)
and unit-tested. Scale is computed per axis in physical pixels:
`physPerEmuX = width × DPR / source.width`, same for Y. *Not verified here:* no
`devicescreenwrapper` files exist in `unreal-qt/src/widgets` on this branch, so the wrapper
concern above does not currently apply.

### 5.6 Delivery into the core

Keyboard events do not call `Keyboard` directly: they post to MessageCenter tagged with
the emulator UUID (`devicescreen.cpp:169-172, 209-212`). Mouse events follow the same
route, for the same reason — it is what keeps multi-instance routing correct.

Pointer capture (grab + hide cursor, release on a hotkey) is required for relative-mode
input. `CONFIG::lockmouse` (`platform.h:411`) exists and is unparsed.

**Synthetic warp events must be discarded.** Qt has no relative mode, so capture is
implemented by re-centring the cursor with `QCursor::setPos()` after each event. That call
generates *another* mouse-move event carrying the large jump back to the centre. Feeding
it to the accumulator injects a spurious delta on every single movement, which typically
presents as the emulated pointer drifting toward one corner.

Discard it by matching the event position against the warp target (and ignoring the first
event after a warp), rather than by thresholding on delta magnitude — a threshold also
eats legitimate fast movement. SDL avoids this entirely by having a real relative mode
([§9.1](#91-the-three-defaults-that-must-be-changed)).

**As built:**
- Capture starts on a click inside the screen (that click is not sent to the machine).
  `Esc` releases it and the `Esc` press, its auto-repeats and its release are all swallowed,
  so they never tap ZX BREAK. Focus loss and switching emulator instance also release it.
  Buttons held at release time are released inside the machine. `lockmouse` is unused.
- **macOS:** native relative mode (`unreal-qt/src/platform/macos/mousecapture_macos.{h,mm}`).
  The cursor is warped once to the widget centre and detached from the mouse
  (`CGAssociateMouseAndMouseCursorPosition(false)`); travel is read from `NSEvent` deltas,
  which already include the OS acceleration. Reason: `QCursor::setPos` posts a synthetic
  HID event that needs Accessibility permission, which an ad-hoc signed build loses on every
  rebuild — the pointer then silently stopped moving.
- **Other platforms:** the re-centring approach described above, with the warp-event filter
  (position equals the warp target, or first event after a warp).

### 5.7 Headless injection

The Qt and SDL paths are not the only input sources: this project drives the machine
headlessly from CLI, Lua, WebAPI and MCP, and GTest integration tests must be able to move
the mouse with no desktop window at all.

Keyboard already has this shape — `DebugKeyboardManager` injects key events and is also
where TTD journalling is called from (`debugkeyboardmanager.cpp:72,101`). A
`DebugMouseManager` mirrors it, giving one funnel that the UI, the automation layer and
the tests all share, and therefore one place where journalling happens.

Notifications follow the existing `NC_*` convention (`platform.h`), not `MC_*`:

```
NC_MOUSE_MOVE     payload: int dx, int dy   (already quantised emulated pixels)
NC_MOUSE_BUTTON   payload: uint8_t buttonMask
```

Injection takes **quantised emulated-pixel steps**, not host deltas: DPI, upscale and trim
are front-end concerns (§5.1), and a headless caller has no window to derive them from.
This also makes automated tests deterministic — "move 10 px right" means exactly ten
counter increments regardless of any display state.

**As built:**
- The desktop posts `MC_MOUSE_MOVE` / `MC_MOUSE_BUTTON` / `MC_MOUSE_WHEEL` (the `MC_*`
  MessageCenter topics, not `NC_*`), each carrying a `MouseEvent` tagged with the emulator
  id (`mouse.h:13-68`). `Mouse::OnMouse*` accepts only its own id (an empty id is a
  broadcast) and hands the event to `DebugMouseManager::ApplyHost*` (`mouse.cpp:162-196`).
- `DebugMouseManager` (`core/src/debugger/mouse/debugmousemanager.{h,cpp}`) is the single
  funnel. Automation calls are validated (±127 move, ±7 wheel, click frames 1…65535,
  counters 0…255) and return a status with a message; host calls skip the range limits (a
  fast flick can exceed 127 in one event) but keep the replay guard and the journal.
- Delivery is a direct call on the caller's thread, so a change is visible before the call
  returns. Details and the alternatives rejected: [automation-interfaces §4.1.3](automation-interfaces.md#413-how-it-reaches-the-device-direct-call-not-messagecenter).

## 6. TTD integration

### 6.1 Device state

Straightforward, following `ttdscorpionprofrom.{h,cpp}`:

- New `ttd::PeripheralId::KempstonMouse` (`ttdserializable.h:41-52`). The enum value is
  not stored on disk, so insertion order is free.
- Implement `TTDSerializable`: `TTDStateSize`, `TTDSaveState`, `TTDLoadState`,
  `TTDDeviceName`, `TTDPeripheralId`, and **`TTDHashState`** — which must be overridden
  or the mouse silently stops participating in divergence detection (the default returns
  0).
- The blob is a packed POD with explicit `reserved[]` and two `static_assert`s, one on
  `sizeof` and one proving no implicit trailing padding. Blobs are hashed byte-wise, so
  unnamed padding leaks uninitialised bytes into the divergence hash.
**Registration: core device, not model-specific.** `TimeTravelManager` has two
registration paths, and the mouse must use the first:

1. **Core devices, model-independent** (as built: `timetravelmanager.cpp:1052`) — registered
   straight off `EmulatorContext`, exactly as `pTape` and `pBetaDisk` are:
   ```cpp
   _peripherals.Register(PeripheralId::KempstonMouse, _context->pMouse);
   ```
2. **Model-specific latches** (`:1056-1061`) — supplied by the port decoder through
   `CreateTTDSerializers()` / `GetTTDModelStateIds()` (`portdecoder.h:246-250`).

Path 2 is per-decoder and today only `PortDecoder_Scorpion256` overrides it
(`portdecoder_scorpion256.cpp:565,576`). Hanging the mouse there would mean repeating
the override in all seven-to-nine decoders, and any decoder that was missed would
capture no mouse blob at all — a recording that looks correct and restores wrong. The
mouse is a shared peripheral hung off the context like `Keyboard`, so path 1 is both
correct and a single line that covers **every** model at once.

Absence is handled for free: `TTDPeripheralRegistry::Register` ignores a null device
(`ttdperipheralregistry.cpp`), so a machine with no mouse fitted leaves no entry, carries
no blob, and restores nothing — which the registry comment calls out as the whole point
of a registry. Note this also means "mouse present" must be decided **before** peripherals
are registered, or the device will be missing from the session.

Do **not** add the id to `GetTTDModelStateIds()`: that list is checked against the
decoder's own serializers, and a declared id with no matching serializer makes
`TimeTravelManager` refuse the session by design (`:1067-1070`).

State to serialise: `_x`, `_y`, `_buttons`, `_wheel`, `_present`. The float accumulator
is **host-side** and must not be serialised — it is not emulated state.

**As built** (`mouse.cpp:198-256`): `PeripheralId::KempstonMouse = 7`
(`ttdserializable.h:51`). The blob is 8 bytes: `version, x, y, buttons, wheel, reserved[3]`,
with `static_assert`s on size and trivial copyability. `_present` is **not** saved: fitting
comes from the config and is re-applied on reset, so a session never carries it. The
`Mouse` object exists on every model (absence is a flag, not a null pointer), so the blob is
always registered. `TTDHashState` is FNV-1a over X, Y, buttons and wheel.

### 6.2 Input journal: the real work

`TTDInputEvent` is keyboard-only: `{TTDTimePoint time, uint8_t key, bool pressed}`
(`ttdinputjournal.h:61-67`), replay injects into `_context->pKeyboard` alone
(`timetravelmanager.cpp:1357`), and `RecordInputEvent(uint8_t key, bool pressed)`
(`timetravelmanager.cpp:1315`) is called only from `debugkeyboardmanager.cpp:72,101`.
There is **no joystick or mouse journalling at all**.

Mouse input is host-asynchronous exactly like keystrokes, so **replay will diverge**
unless the journal is extended. Two options:

- **(a) Discriminated union.** Add a `kind` field to `TTDInputEvent` and widen the
  payload to carry either a key or a mouse delta/button mask.
- **(b) Parallel journal.** A separate mouse journal merged by timestamp at replay.

**Proposed: (a).** One journal keeps the ascending-`time` invariant trivially true
(the journal drops non-monotonic inserts at the call site) and one merge point is
easier to reason about than two ordered streams. Cost: the on-disk record grows, so the
TTD format version and its round-trip fixtures move with it.

Record the **quantised integer step**, not the float delta — the accumulator is host
state, and replaying raw host deltas would re-derive different steps if `trim` or the
window size changed between record and replay.

**Batch at frame boundaries, or the journal bloats.** A 500–1000 Hz mouse delivers
hundreds of motion events per second, and journalling each one would inflate recordings by
orders of magnitude against a keyboard-only baseline of a few events per second. Accumulate
in the host layer and emit **at most one movement record per emulated frame**, carrying the
summed step. Determinism is unaffected: the guest cannot observe intra-frame motion anyway,
since it only ever sees the counters at the instant it reads the port. Button transitions
are rare and are journalled individually, as keys are.

> **Open question (D-2).** This is the largest single piece of work in the feature and
> it changes a serialised format. It may deserve to land as its own task ahead of the
> mouse, and it would simultaneously unblock joystick journalling, which is missing for
> the same reason.

**As built — option (a), inside this work.**
- `TTDInputKind` = `Key`, `MouseMove`, `MouseButtons`, `MouseWheel`, `MouseCounters`,
  `KeyboardReset` (`ttdinputjournal.h:64-72`). `TTDInputEvent` gained `kind`, `dx`, `dy`,
  `buttonMask`, `wheelSteps` (`:74-84`). `MouseCounters` reuses `dx`/`dy` as absolute X/Y.
- `TimeTravelManager::RecordMouseMove / RecordMouseButtons / RecordMouseWheel /
  RecordMouseCounters / RecordKeyboardReset` stamp the event with the current frame and
  t-state. `DebugMouseManager` calls them **before** changing the device, only while
  recording.
- Replay: `InjectDueEvents(keyboard, mouse, now)` applies each event at its exact time point
  (`ttdinputjournal.cpp:47-105`). While `ttdReplayActive` is set, every live source (desktop,
  automation, the timed click release) is refused.
- **No file format changed.** The input journal is in memory only; the `.ttd` dump has no
  input section (for keyboard or mouse). The checkpoints do carry the mouse blob (§6.1).
- **Frame batching was not done.** Each host event that produces at least one whole pixel is
  journalled separately. Deterministic, but a fast mouse during a long recording grows the
  journal. Open item.
- **Keyboard fixed on the way.** `DebugKeyboardManager::ApplyKey` is now the one journalled,
  replay-guarded path for tap/combo/type/sequence operations too (they used to bypass both),
  the sequence state is guarded by a recursive mutex, and `ReleaseAllKeys` journals one
  `KeyboardReset`. Desktop keystrokes (`Keyboard::OnKeyPressed/OnKeyReleased`) are journalled
  and replay-guarded as well.

## 7. Config and feature gating

`CONFIG` already carries `input.mouse`, `mouseswap`, `kjoy`, `joymouse`, `mousescale`,
`mousewheel` (`platform.h:538-541`), `enum MOUSE_WHEEL_MODE {NONE, KEYBOARD, KEMPSTON}`
(`platform.h:291`) and `lockmouse` (`platform.h:424`) — all inherited Unreal Speccy
members that **`config.cpp` parsed nowhere** before this work.

**As built:** `config.cpp:312-349` parses `[INPUT] Mouse=`, `Wheel=`, `SwapMouse=` and
`MouseScale=` (range-checked to −3…3, otherwise 0 with a warning). A new
`bool mouseConfigured` (`platform.h:542`) records whether `Mouse=` was present at all: a
context with no ini (unit tests) keeps the mouse fitted. `enum MOUSE_TYPE` was added at
`platform.h:294`. Consumers today: `Mouse::ApplyConfiguration` reads `mouse` and
`mousewheel`; the desktop `MouseManager` reads `mouseswap` and `mousescale` (cast to `signed char`)
through its host-settings provider when capture starts, and only captures when the mouse is fitted. A shipped ini even documents the original
keys (`Mouse=KEMPSTON`, `MouseScale`, `SwapMouse`). The fields are reused and given
parsers.

`SUBMODULE_IO_KEMPSTON_MOUSE = 0x0040` is already reserved (`platform.h:137`) with its
logger name (`modulelogger.h:217-218`).

Verified declarations (`platform.h:538-542`, `:424`, re-checked 2026-09-12):

```cpp
uint8_t mouse, mouseswap, kjoy, keymatrix, joymouse;
char    mousescale;          // NOTE: plain char — see signedness hazard below
uint8_t mousewheel;          // enum MOUSE_WHEEL_MODE
bool    mouseConfigured;     // added: [INPUT] Mouse= was parsed
uint8_t lockmouse;
```

### 7.1 `mousescale` — power-of-two coarse scale

The original Unreal Speccy ini documents this field explicitly:
```
MouseScale=0  ; logarithmic scale: ZX_mouse=PC_mouse*2^(Scale), valid: [-3;3]
```

And the implementation (`unreal-speccy/input.cpp:291-299`) applies it as:
```cpp
if (conf.input.mousescale >= 0)
    msx *= (1 << conf.input.mousescale);   // 0→1×, 1→2×, 2→4×, 3→8×
else
    msx /= (1 << -conf.input.mousescale);  // -1→½, -2→¼, -3→⅛
```

So `mousescale` is a **signed integer** in `[-3; 3]` giving a **power-of-two** scale
factor. This is the coarse sensitivity control — the GUI exposes it as a 7-step slider.
In §5.1 the formula uses `scale = pow(2, mousescale)`, which is exactly this.

**Signedness hazard.** The field is declared `char`, whose signedness is
implementation-defined — *unsigned* on ARM, including the Apple Silicon machines this
project is built on. Any code treating it as a signed range silently breaks on the
primary development platform. **Fix at parse time:** read the ini value into an `int`,
clamp to `[-3; 3]`, store as `int8_t` (not `char`). The field type in `CONFIG` should be
changed to `int8_t mousescale;`.

**As built:** the parse reads a `long` and range-checks it, but still stores
`static_cast<char>(scale)` into the `char` field. The hazard is therefore still open for the
first consumer: read it back as `static_cast<int8_t>(config.input.mousescale)`, or change the
field type.

### 7.2 `mousewheel` — three modes, not a boolean

The enum `MOUSE_WHEEL_MODE` (`platform.h:291`) has three values:

| Value | Ini key | Behaviour |
|---|---|---|
| `MOUSE_WHEEL_NONE` (0) | `Wheel=NONE` | Wheel events discarded |
| `MOUSE_WHEEL_KEYBOARD` (1) | `Wheel=KEYBOARD` | Wheel up/down injects ZX key press/release events (cursor up/down by default) |
| `MOUSE_WHEEL_KEMPSTON` (2) | `Wheel=KEMPSTON` | Wheel written to upper nibble of button register (hardware-accurate) |

The `KEYBOARD` mode is useful for software that scrolls menus with cursor keys. The
original implementation maps wheel-up to `VK_MWU` and wheel-down to `VK_MWD`, which are
then bound to ZX keys via the keybinding system. We replicate this: wheel events in
`KEYBOARD` mode post key-press/release pairs through `DebugKeyboardManager`, using
configurable ZX key targets (defaulting to cursor up/down).

**As built:**
- `Wheel=KEMPSTON` puts the wheel counter on the bus; `Wheel=NONE` does not (bits 4–7
  read 1). `Wheel=KEYBOARD` is **not implemented**: it logs a warning and behaves as `NONE`.
  The wheel counter itself still counts in every mode; only its visibility changes.
- **Default decision: `NONE`, not `KEMPSTON`** (reversing §2). Every shipped
  `data/configs/*/unreal.ini` says `Wheel=NONE`, and a config without the key also gets `NONE`.
  Why: software written for the classic, wheel-less Kempston mouse checks the unused bits.
  The Scorpion ProfROM does this at page 5 `#08FB`:
  ```
  PUSH BC : LD BC,#FADF : IN A,(C) : POP BC
  AND #38 : CP #38 : RET NC          ; all three bits set -> keep the mouse
  RES 5,(IY+#27) : RET               ; otherwise switch the mouse off
  ```
  Bits 3–5 must all be 1. Worked example with the mouse idle:
  - `Wheel=NONE`: `#FADF` = `0xFF`; `0xFF AND #38` = `#38` → mouse kept.
  - `Wheel=KEMPSTON`, wheel counter 0: `#FADF` = `0x0F`; `0x0F AND #38` = `#08` → the ROM
    turns the mouse off.
  Pinned by `ScorpionKempstonMouse_Test.ButtonRegisterPassesProfRomDetectionMask` and
  `ProfRomColdBootKeepsMouseEnabled`. `Wheel=KEMPSTON` remains available for ZX Evo-style
  software that expects the wheel nibble.

### 7.3 `joymouse` — deferred

`joymouse` ("emulate Kempston Joystick on mouse") makes host mouse movement control the
*joystick* register rather than the mouse ports — useful for joystick-controlled games
without a joystick. It is a separate feature that does not share any code path with the
mouse device. **Deferred** to a follow-up task; the field is left unparsed for now, and
the ini comment documents it as reserved.

### 7.4 `mouse` — type selector

The `mouse` field's own value semantics are **not** defined anywhere in this codebase —
the legacy ini documents `Mouse=KEMPSTON`, but no enum backs it, so the mapping is ours
to define:

| Value | Ini key | Meaning |
|---|---|---|
| 0 | `Mouse=NONE` | No mouse emulated |
| 1 | `Mouse=KEMPSTON` | Kempston Mouse (this feature) |
| 2 | `Mouse=AY` | AY-Mouse / Korvet mouse (out of scope) |

**As built:** `enum MOUSE_TYPE { MOUSE_TYPE_NONE = 0, MOUSE_TYPE_KEMPSTON = 1, MOUSE_TYPE_AY = 2 }`
(`platform.h:294`). `Mouse=AY` logs "not emulated" and fits no mouse; an unknown value logs
a warning and fits a Kempston mouse; a missing key fits a Kempston mouse.

### 7.5 Feature flag

Feature flag `Features::kKempstonMouse` alongside the existing constants
(`base/featuremanager.h:22-36`), cached on the decoder and refreshed through an
`UpdateFeatureCache()`.

**As built:** feature id `kempstonmouse`, alias `kmouse`, **on** by default
(`featuremanager.h:37, 55, 84-85`; registered `featuremanager.cpp:312-318`). It is not
cached on the decoder. Instead `Mouse::ApplyConfiguration` (`mouse.cpp:54-70`) computes
`present = (Mouse= is KEMPSTON or absent) AND feature on` and stores it in an atomic; the
feature manager calls it whenever features change (`featuremanager.cpp:435-439`), and
`Core::Reset` calls it after a reset. Each `IN` checks the atomic flag. Worked example:
`feature kempstonmouse off` → the next `IN #FBDF` is no longer claimed by the mouse.

## 8. Test architecture

Four layers, mirroring the conventions already in the tree:

1. **Decoder-level, no CPU** — `core/tests/emulator/ports/models/portdecoder_*_test.cpp`.
   Per-model mask behaviour, A8/A10 selection, the `#FADF`/`#FEDF` alias, and the
   joystick-collision regression. This layer is CPU-free deliberately: on a running
   machine the firmware owns the ports and overwrites test writes.
2. **Device unit** — `core/tests/emulator/io/mouse/mouse_test.cpp`. Counter wrapping,
   active-low buttons, wheel nibble, constant bit 3, reset values, swap, absence.
3. **Pointer mapping** — pure arithmetic, no Qt: fractional DPR, upscale > 1 producing
   sub-pixel deltas that must still accumulate to motion, remainder carry, independent
   X/Y factors. This is where §5.2's three traps become executable.
4. **TTD** — round-trip, `BlobIsPaddingFree`, state-hash-distinguishes, registry
   capture/restore, and a **record-and-replay divergence test** covering §6.2.

Every test must be mutation-verified: break the behaviour, confirm the test fails. The
bugs found earlier in this project (ROM notifications, ATM turbo) were all silent, and
all would have been caught by a test that had been checked this way.

## 9. If the front end is SDL rather than Qt

The rule in §5.1 does not change — one physical host pixel still maps to one emulated
pixel, and the accumulator still carries its remainder. What changes is that **every
input to that formula must be obtained differently, and three of them SDL will get wrong
by default.** Qt hands us DPI-corrected, OS-ballistics-applied deltas almost for free;
SDL hands us raw numbers whose units depend on platform, window mode and SDL version.

### 9.1 The three defaults that must be changed

1. **Relative mode is not on.** Without it the cursor hits the desktop edge and deltas
   simply stop — the emulated pointer freezes at a screen boundary that does not exist in
   the guest. Enable relative mode (SDL2 `SDL_SetRelativeMouseMode(SDL_TRUE)`; SDL3
   `SDL_SetWindowRelativeMouseMode(window, true)`), which also hides and un-warps the
   cursor.
2. **Deltas are raw, not ballistics-applied.** SDL2 ≥ 2.0.18 gates this on
   `SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE`, which must be set to `"1"`. Per §5.4 this is
   what we want: Kempston Mouse software is pointer-style, so the emulated pointer has to
   feel like the system pointer. Leaving it off produces input that feels correct in a
   mouse-look game and wrong in Art Studio.
3. **Renderer logical scaling may already be applied.** SDL2's
   `SDL_HINT_MOUSE_RELATIVE_SCALING` defaults to **on**, meaning that if
   `SDL_RenderSetLogicalSize()` is in use, SDL has *already* scaled the relative deltas
   into logical units. Dividing by our own upscale on top of that **double-counts** and
   makes the pointer progressively slower the larger the window. Pick one: either let SDL
   scale and skip our divide, or turn the hint off and do the arithmetic ourselves. The
   second is preferable because it keeps one formula for both front ends.

### 9.2 Getting the DPI ratio

Qt gives `devicePixelRatioF()` directly. SDL has no single equivalent; it is derived:

| SDL | Logical size | Pixel size | Ratio |
|---|---|---|---|
| SDL2 | `SDL_GetWindowSize()` | `SDL_GL_GetDrawableSize()` / `SDL_GetRendererOutputSize()` | pixel ÷ logical |
| SDL3 | `SDL_GetWindowSize()` | `SDL_GetWindowSizeInPixels()` | or `SDL_GetWindowPixelDensity()` |

SDL2 additionally requires the window to be created with `SDL_WINDOW_ALLOW_HIGHDPI`, or
the drawable size equals the window size and the ratio silently reads 1.0 on a Retina
display — the exact failure mode §5.2(2) warns about, just arrived by a different route.
SDL3 is high-DPI by default.

Mouse motion events are reported in **logical window coordinates** in SDL2, so
`delta_phys = delta_logical × ratio` exactly as in §5.1. SDL3 reports relative motion as
`float`, which removes the quantisation but does not remove the need to scale.

### 9.3 Window modes

The upscale factor is `drawable_pixels ÷ framebuffer_pixels`, and it must be
**recomputed on every event that can change it** — not cached at startup:

| Mode | What changes | Notes |
|---|---|---|
| Integer 1×–4× | upscale is exactly 1..4 | the easy case; still recompute on resize |
| Maximized / arbitrary resize | non-integer upscale, usually letterboxed | see below |
| Desktop fullscreen | upscale from the desktop mode | recompute on the window event |
| Exclusive fullscreen | display mode itself changes | ratio and drawable size can both change |
| Window moved between monitors | DPI ratio changes mid-session | SDL2 `SDL_WINDOWEVENT_DISPLAY_CHANGED`, SDL3 `SDL_EVENT_WINDOW_DISPLAY_CHANGED` |

Two subtleties:

- **Letterboxing affects offset, not scale.** For *relative* input only the scale term
  matters, so black bars can be ignored here — unlike absolute pointer positioning, where
  the bar thickness must be subtracted first. Worth stating explicitly because the two
  paths look similar and share a coordinate conversion in most codebases.
- **X and Y scales can differ.** If the presentation does not preserve aspect ratio, the
  two factors diverge and must be computed independently — the same trap as §5.5, where
  the Qt widget's fixed 352×288 aspect makes it easy to assume they are equal.

### 9.4 Per-OS behaviour

| OS | What to know |
|---|---|
| **Windows** | Fractional scaling (125 / 150 / 175 %) is normal, so the ratio is routinely non-integer. The process must be per-monitor DPI-aware v2 or the OS reports virtualised sizes and the ratio is wrong. "Enhanced pointer precision" *is* the OS ballistics curve; with system scale enabled we inherit it, which is what we want. |
| **macOS** | Backing scale is typically 2.0, but scaled display modes yield non-integer effective ratios. The OS acceleration curve is applied to cooked deltas; raw HID deltas bypass it. Trackpads deliver very different delta distributions from mice — a reason to keep `trim` per-input-device if it ever becomes a complaint. |
| **Linux / X11** | No reliable per-monitor DPI; `Xft.dpi` and RandR disagree routinely, and X11 has no fractional scaling, so applications scale themselves. Expect a ratio of 1.0 and a large framebuffer. XInput2 distinguishes raw from cooked motion. |
| **Wayland** | Relative motion requires the `relative-pointer` protocol and confinement requires `pointer-constraints`; cursor warping is not permitted at all, so relative mode is mandatory rather than merely preferable. Fractional scaling arrives via `fractional-scale-v1`; older SDL2 builds report only integer scale, which will quietly mis-scale on a 125 % display. |

### 9.5 Input hygiene

- **Touch-synthesised mouse events.** `SDL_HINT_TOUCH_MOUSE_EVENTS` and
  `SDL_HINT_MOUSE_TOUCH_EVENTS` can cause the same physical gesture to arrive twice.
  Decide explicitly which source feeds the emulated mouse.
- **High polling rates.** A 1000 Hz mouse delivers many small deltas per emulated frame.
  The float accumulator handles this correctly by construction; an integer one would
  round each event toward zero and lose most of the movement — §5.2(1) again, made worse.
- **Focus loss.** Release relative mode on focus-out, or the pointer stays captured. On
  regain, discard the first delta: it may encode the jump back to the window.

### 9.6 Calibration test

The scale-invariance claim is testable and should be tested per front end, not assumed:
inject N physical pixels of motion and assert the emulated counter advanced by N, with
`trim = 1.0`, across every row of the §9.3 table and at two DPI ratios. Any row that
fails identifies exactly which term was stale or double-counted. This is the SDL analogue
of §8's pointer-mapping tests, and it is the only way the "1 px = 1 px" promise survives
contact with four window modes and four operating systems.

## 10. Risks

| Risk | Mitigation |
|---|---|
| Button order wrong (D-1) | Validate against real software before leaving `inprogress` |
| TTD format change (D-2) | Land the journal extension as its own task, with fixtures |
| Joystick narrowing (§3.3) regresses existing software | Regression test on joystick ports |
| Per-model masks unverified for 128/+3 (D-3) | Full decode cannot false-positive |
| Wrapper/implementation split missed (§5.5) | Explicit wrapper test, as with the viewport bug |

**As built — how the risks played out:**
- D-1: resolved from the sources; the ProfROM pointer test also matches.
- D-2: no file format changed (journal is in memory only); landed with the mouse.
- Joystick: not narrowed; exact `#FF1F` kept and ordered before the mouse (§3.3).
- A risk that was not listed bit hardest: **splicing a new decode arm into an existing
  if/else-if chain.** The first Scorpion change added the mouse arm as a fresh `if`, so the
  trailing fallback overwrote every earlier arm's result and the Scorpion read a dead keyboard
  (and lost AY readback, `#1FFD`, SMUC and `#7EFD`). Fixed, and pinned by
  `ScorpionPorts_Test.KeyboardRowReadSurvivesLaterDecodeArms`. See [walkthrough.md](walkthrough.md).
- A second unlisted one: **the wheel nibble breaking detection** (§7.2).
