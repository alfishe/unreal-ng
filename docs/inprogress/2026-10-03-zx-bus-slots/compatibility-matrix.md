# ZX-bus slots: compatibility matrix

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft; the matrix in code is generated from the card and machine declarations ([architecture.md](architecture.md) §3), this page is its readable form and the review checklist |
| **Rules** | [requirements.md](requirements.md) §3.3, [open-questions.md](open-questions.md) Q1, Q2, Q5 |

## 1. Functions per card (first catalog)

| Card id | Card | Slot kind | Functions | Ports (IORQGE ✓ / passive ·) |
|---|---|---|---|---|
| `ay` | the machine's own AY (default socket content) | `ay-socket` | `ay-socket` | `#FFFD`, `#BFFD` |
| `ts` | TurboSound (2 × AY) | `ay-socket` | `ay-socket` | `#FFFD`, `#BFFD` |
| `tsfm` | TurboSound FM (2 × YM2203) | `ay-socket` | `ay-socket` | `#FFFD`, `#BFFD` |
| `gs` | General Sound (Z80) | `zxbus` | `gs` | `#B3`, `#BB`, `#33` |
| `gs-lw` | General Sound, lightweight player | `zxbus` | `gs` | same |
| `neogs` | NeoGS | `zxbus` | `gs` | same + ZX-DMA bus overlay; media `sd.ngs` |
| `moonsound` | ZXM-MoonSound (OPL4) | `zxbus` | `opl4` | `#C4-#C7`, `#7E`, `#7F` |
| `covox-fb` | Covox on `#FB` | `zxbus` | `covox-fb` | `#FB` · |
| `soundrive` | SounDrive 1.05 (4 ch) | `zxbus` | `soundrive`, `covox-fb` | `#0F #1F #4F #5F` ·, `#F1 #F3 #F9 #FB` · |
| `multisound` | ZX-MultiSound | `zxbus` | `ym`: `ay-socket` (shadow) + `midi`; `saa`; `gs`; `sd`: `soundrive` (DIP dependent) | `#FFFD` ✓, `#DFFD` ·, `#BFFD` ✓, `#FF`/`#1FF` ·, `#B3` ✓, `#BB` ✓, `#0F #1F #4F #5F` · |
| `zxnetusb` | ZXNETUSB (W5300) | `zxbus` | `net.zxnetusb` | `#AB` |
| `zx-wifi` | ZX-WiFi (ESP on `#EF`) | `zxbus` | `serial.ef` | `#xxEF` |

Notes:
- `ay-socket` as a *function* of a ZX-bus card means "takes over the AY ports by IORQGE": it shadows the socket's
  content instead of competing with it (Q2).
- `soundrive` occupies `covox-fb` too because its mode-2 ports include `#FB`.
- Later cards (not in the first migration): `beta128`, `ide.nemo`, `ide.smuc`, `kempston-joystick`, `kempston-mouse`,
  `divmmc`, `zxm-soundcard-extreme`, `tsfm-pro`, standalone `saa`.

## 2. Card × card

✓ compatible · ✗ incompatible (function clash) · ⊘ pointless (shadowed; treated as incompatible) · `opt` depends on options

| | ay/ts/tsfm (socket) | gs | gs-lw | neogs | moonsound | covox-fb | soundrive | multisound | zxnetusb | zx-wifi |
|---|---|---|---|---|---|---|---|---|---|---|
| **ay/ts/tsfm** | one socket | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ⊘ if `ym` (only `ay` stays) | ✓ | ✓ |
| **gs** | ✓ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✗ if `gs` | ✓ | ✓ |
| **gs-lw** | ✓ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✗ if `gs` | ✓ | ✓ |
| **neogs** | ✓ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✗ if `gs` | ✓ | ✓ |
| **moonsound** | ✓ | ✓ | ✓ | ✓ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **covox-fb** | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ |
| **soundrive** | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ if `sd` | ✓ | ✓ |
| **multisound** | ⊘ if `ym` | ✗ if `gs` | ✗ if `gs` | ✗ if `gs` | ✓ | ✓ | ✗ if `sd` | ✗ (a second one) | ✓ | ✓ |
| **zxnetusb** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ✓ |
| **zx-wifi** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ |

## 3. Worked examples of plans

**A. One card pushes out three.** Installed: socket `tsfm`, `zxbus.1 = gs`, `zxbus.2 = soundrive`. Request: plug
`multisound` (all DIP on) into `zxbus.3`.

| Plan item | Content |
|---|---|
| removed | `zxbus.1 gs` (clash `gs`), `zxbus.2 soundrive` (clash `soundrive`) |
| socket | `tsfm` would be shadowed -> replaced by `ay` (reported in `removed` with its options) |
| shadowed | the socket's `ay` (by `zxbus.3`) |
| lost functions | `covox-fb` (the SounDrive's mode-2 ports; the MultiSound decodes mode-1 ports only) |
| API without flag | refused, HTTP 409 with this plan |
| API with `replaceIfIncompatible` | applied; reply = this plan |
| UI | applied; warning lists the three removals, the shadowing and the lost `#FB` Covox, with Undo |

**B. Options avoid the clash.** Installed: `zxbus.1 = neogs`. Request: `multisound` with `dip=ym,saa,sd` (GS off). No
clash: both stay; the MultiSound's GS ports are not decoded.

**C. Turning an option on later.** Same as B, then `slots set zxbus.2 dip=ym,saa,gs,sd`. Plan: remove `neogs` (and its
`sd.ngs` medium: refused if dirty without `mediaDisposition`).

**D. Replacing with less.** Installed: `multisound` (all on). Request: socket `tsfm`. Plan: `multisound` removed
(⊘ pair, the socket card would be shadowed), lost functions `gs`, `saa`, `soundrive`, `midi`.

**E. Bus fit.** Machine 128K (Sinclair edge). Request: `multisound`. Fit: `zxbus` card on `sinclair-edge` -> needs an
adapter; without one: UI explains "a NemoBus card does not fit the 128K's edge connector on real hardware" and asks to
confirm; API refuses without the flag; with it, `fit = unrealistic`.

## 4. Built-in devices (machine side)

Fixed built-ins block a card that needs the same function, even with the flag; switchable ones are switched off and
reported. Shadowing applies to IORQGE claims.

| Machine | Bus | Built-in devices (functions) | Notable rules |
|---|---|---|---|
| 48K | `sinclair-edge` | beeper | no AY socket; `ay-socket` cards need an AY interface card (later) |
| 128K, +2 | `sinclair-edge` | `ay` in the socket | MultiSound: adapter or override |
| +2A, +3 | `sinclair-edge` (+3 variant) | `ay`, +3 FDC | as 128K |
| Pentagon 128 / 512 / 1024 | `zxbus` | `ay` socket, `beta128`, Kempston joystick | MultiSound shadows the socket |
| Scorpion, ProfScorpion | `scorpion` / `zxbus` (SL-0 to confirm) | `ay` socket, `beta128`, SMUC (`ide.smuc`, RTC) | |
| ATM 450 / 710 | `zxbus` + `atm-internal` | `ay`, `beta128`, ATM IDE | ATM2IOESP on `atm-internal` |
| ATM3 / ZX-Evo (Baseconf) | `zxbus` | TurboSound in the FPGA (`ay-socket`, switchable?), `beta128`, Z-Controller (`sd.zc`), Kempston, PS/2 | MultiSound shadows the FPGA TurboSound (the configuration the card's author tested) |
| TS-Conf | `zxbus` | as ZX-Evo + board Covox (`covox-fb`, fixed) | `covox-fb` card refused (fixed built-in); ZX-WiFi refused (`#xxEF` reserved) |
| Profi v3 / v5 | `zxbus` (Profi bus, SL-0) | `ay`, palette on `#7E` (fixed) | `moonsound` refused: `#7E` clashes with the fixed palette (today an INI comment) |
| Sprinter | `isa8` (own design); `zxbus` through the ISA adapter card | `ay`, Covox-Blaster | ZX-bus cards only behind the adapter |

Every "SL-0" item is confirmed from schematics and recorded in the machine's declaration with its source.
