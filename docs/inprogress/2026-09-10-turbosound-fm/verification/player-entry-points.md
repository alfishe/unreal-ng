# TSFM Player Entry Points (P0 record)

Disassembly of the TFM Music Compiler 1.12 player shipped in
`testdata/sound/tsfm/TSFM-EL.TAP`, produced with `z80dasm 1.2.0`
(`-a -l -t -u -g 25000`). The player drives the TurboSound FM board from
Pentagon RAM; the harness (`core/tests/_helpers/tsfmplayerharness.{h,cpp}`)
pokes and runs it. Revisions of the player block require re-deriving
everything below.

## TAP layout

| # | Block | Load at | Length | Content |
|---|-------|---------|--------|---------|
| 0/1 | `TSFM v2` | BASIC | 568 | MB03+/Lanex turbo-loader stub (not used by the harness) |
| 2/3 | `_tsfmplaye` | 25000 (`0x61A8`) | 1883 | The player |
| 4/5 | `lnxdata` | 31000 (`0x7918`) | 4704 | Loader/graphics code (Z80; not referenced by the player) |
| 6.. | 106 tune blocks | 32768 (`0x8000`) | 1697–15794 | `TFMcom1.12` tunes; final block `end` (`"MMM"`) is a terminator |

Load order on hardware: player, `lnxdata`, tune — the tune overwrites the
`lnxdata` tail from `0x8000` (only 31000..32767 survives). The harness pokes
in the same order. `lnxdata` is not referenced anywhere in the player code
(no `0x7918` constant); it exists for the MB03+ disk menu.

## Entry points

| Address | Trampoline at | Role |
|---------|---------------|------|
| `0x61A8` (25000) | — | Main entry: `ld hl,08000h; call 0x61CC; ei; halt`-loop |
| `0x61D5` (via `0x61CC`, 25003) | `jp 0x61D5` | init: HL = tune address; relocates 6 pattern pointers; patches channel stubs; **falls through into the chip-reset sequence at `0x622A`** |
| `0x62B1` (via `0x61CF`, 25007) | `jp 0x62B1` | per-frame routine, called once per interrupt by the main loop |
| `0x622A` (via `0x61D2`, 25010) | `jp 0x622A` | chip reset / mute sequence (shared by init fall-through and the keypress exit) |

The main loop (entered with HL = tune, IM1, interrupts disabled — its own
`ei` enables them before `halt`):

```asm
61a8  ld hl,08000h      ; tune address
61ab  call 61cch        ; init (includes chip reset)
61ae  ei
61af  halt              ; one interrupt = one frame
61b0  xor a / out (0feh),a     ; border 0
61b3  call 61cfh        ; frame routine
61b6  ld a,007h / out (0feh),a ; border 7
61ba  in a,(0feh) / and 01fh / cp 01fh
61c1  jr z,61afh        ; loop while no key half-row bit low
61c3  call 61d2h        ; stop (chip reset again)
61c6  exx / ld hl,02758h / exx / ret
```

## init details (`0x61D5`)

* `tune+9` is the version character. `"TFMcom1.12"` tunes carry `'2'`
  (`0x32`) there → init patches the frame routine's epilogue at `0x62D2`
  to `RET` (output every frame). Any other character patches `LD A,n`
  there, enabling a 1-in-6 frame-rate divider for old-format tunes.
* Reads six 16-bit pattern pointers from `tune+10 .. tune+21` (offsets
  relative to the tune base), converts them to absolute addresses and
  stores them through a relocation table at `0x68F7` (last 12 bytes of
  the player block) into the six channel stubs (`ld hl,nnnn` operands at
  `0x636B/0x646B/0x656C/0x666D/0x676D/0x686E`).
* Patches each channel stub's `ld a,nnnn` operand (`0x6366` etc.) to
  `0xFF` (channel "not loaded yet" marker).
* **Falls through into `0x622A`** — the chip-reset sequence is part of
  init, not only of the stop path.

## Port protocol observed in the player (matches hardware-reference.md)

Register/data pair helper (`sub_628A`, `0x628A`), used by reset/stop:

```asm
628a  ld b,d            ; b = 0xff
628b  in f,(c)          ; poll #FFFD status (bit 7 = busy)
628d  jp m,628bh
6290  out (c),a         ; register address -> #FFFD
6292  ex af,af'
6293  in f,(c)          ; poll again
6295  jp m,6293h
6298  ld b,e            ; b = 0xbf
6299  out (c),a         ; data -> #BFFD
```

The bulk channel writer (`0x62DF..0x62F2`, outi pair loop) and the
interpreter write helper (`0x6337..0x6358`) use the same shape:
address → `#FFFD`, data → `#BFFD`, busy poll before each write. This is
the `WaitStatus` loop for P4's `TsfmBusy.PlayerWaitLoop` test.

Frame routine (`0x62B1`):

```asm
62b1  ld de,0ffbfh      ; d = 0xff (chip 0), e = 0xbf (chip 1)
62b4  ld c,0fdh
62b6  ld b,d / ld a,0f8h / out (c),a   ; #FFFD <- 0xF8 (control word)
62bb  call 6365h / 6465h / 6566h       ; channels 1..3
62c4  ld b,d / ld a,0f9h / out (c),a   ; #FFFD <- 0xF9 (control word)
62c9  call 6667h / 6767h / 6868h       ; channels 4..6
62d2  ret                           ; (patched by init for version '2')
```

Chip reset/mute (`0x622A`): `#FFFD ← 0xF8`, then register/data pairs
zeroing registers `0x0D` down to `0x01` (data `0x00`), FM register ranges
`0xB3..0x30` and `0x8F..0x80` (data `0x0F`), key-off/register mops
(`0x0F/0x28`, `0x2A`, `0x2B`, `0x2E`, `0x2D`, `0x7F/0x4F..0x40`,
`0x2F`, `0x2D`), finally `#FFFD ← 0xF9` (`sub_62A3`).

## Behaviour on the current legacy TurboSound device

Legacy `#FFFD` reads return the selected AY register, not a status word.
The reset sequence selects registers while polling, so it parks in the
WaitStatus loop at the first selected register whose value has bit 7 set.
On a fresh machine that is the mixer register (`0x07`): the AY resets
`_registers[AY_MIXER_CONTROL]` to `0xFF` ("mute all outputs"), so the
observed traffic ends with `#FFFD ← 0x07`
and the CPU spins at `0x6293/0x6295` forever. Captured traffic (tune
`03 DJ Tepp`, frame 0):

```
#FFFD <- 0xF8, then pairs (0x0D,0)(0x0C,0)(0x0B,0)(0x0A,0)(0x09,0)(0x08,0),
#FFFD <- 0x07  [park in WaitStatus at 0x6293/0x6295]
```

The park is fully deterministic (traffic identical across fresh emulator
instances — asserted by `TrafficIsDeterministicAcrossInstances`). The
player completes only on the TSFM device (P4+), where `#FFFD` reads
return the status byte whose busy bit self-clears after 32·prescale
T-states. `AYtest_v0.2` remains the correct material for legacy
TurboSound behaviour.

## Harness

`TsfmPlayerHarness` (`core/tests/_helpers/tsfmplayerharness.{h,cpp}`):

* `Setup(emulator, tuneIndex)` — parses the TAP, pokes player/lnxdata/tune
  in hardware load order, installs a `Z80::busTraceHook` tracer, enters
  the main loop at `0x61A8` (HL = tune, IM1, `iff1 = 0`).
* `RunFrames(n)` — runs the machine; records every `#FFFD/#BFFD` write,
  per-frame write counts, control-word tallies and the traffic hash.
* `GetFramesSinceLastWrite()` — hang detector for the P4 gate ("player
  harness runs to the end of a tune without hanging").
