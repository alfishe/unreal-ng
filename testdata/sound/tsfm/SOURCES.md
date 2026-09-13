# TurboSound FM test material

Third-party files for TSFM development and regression listening. Design: `docs/inprogress/2026-09-10-turbosound-fm/`.

Retrieved 2026-09-12; the zxart archives were downloaded a second time the same day and are byte-identical. All files are freely distributed by their publishers; authors' rights remain with the authors.

| File | Downloaded from | Publisher page | SHA-256 |
|---|---|---|---|
| `TFMWORKS.SCL` | [tfm.zip](http://nedopc.com/TURBOSOUND/tfm.zip) (unpacked) | [NedoPC — TurboSound FM](http://nedopc.com/TURBOSOUND/ts-fm.php) | `0cf8449e3bb69947973ec534bcc98b1db87fc5c112edba2d888d1c312159fc59` |
| `TSFM-EL.TAP` | [TSFM-EL.TAP](https://zxart.ee/releasefile/id:539651/TSFM-EL.TAP) | [ZX-Art — TurboSound FM Tunes Collection](https://zxart.ee/eng/software/demoscene/art-pack/music-collection/turbosound-fm-tunes-collection/) | `389ff968ab763d8826e531bd268a8901d34327c93f9208fb2d30522af1f56e47` |
| `AYtest_v0.2.scl` | [AY_YM_TS_test_v02.ZIP](http://nedopc.com/TURBOSOUND/tools/AY_YM_TS_test_v02.ZIP) (unpacked) | [NedoPC — TurboSound FM](http://nedopc.com/TURBOSOUND/ts-fm.php) | `5eba8570996f7e2f6bfb75f694e4e22e5cdc787e56be48d26c3b6ea6653bc3e0` |
| `AYtest_v0.2.tap` | [AY_YM_TS_test_v02.ZIP](http://nedopc.com/TURBOSOUND/tools/AY_YM_TS_test_v02.ZIP) (unpacked) | [NedoPC — TurboSound FM](http://nedopc.com/TURBOSOUND/ts-fm.php) | `ebdf851f78fde36a538402fe83457ede00eab25ed40fac904eebcfaad03581bf` |
| `dihalttsmus.TRD` | [dihalttsmus.zip](https://zxart.ee/releasefile/id:323995/dihalttsmus.zip) (unpacked) | [ZX-Art — DiHalt 2007 TurboSound FM Music](https://zxart.ee/prod/323994) | `70c6470344f87787115b98edb0c36a717b8c193b29c152009b1278933447ed0d` |
| `sonic3d.trd` | [sonic3d.zip](https://zxart.ee/releasefile/id:275497/sonic3d.zip) (unpacked) | [ZX-Art — Sonic 3D Blast song #13](https://zxart.ee/prod/275496) | `98ea6f3a60ceead0addbccda3555bf25681b7b9944579b61edf66123d7a7718b` |
| `hu2010fm.scl` | [hu2010fm.zip](https://zxart.ee/releasefile/id:287931/hu2010fm.zip) (unpacked) | [ZX-Art — Husmann 2010 Music](https://zxart.ee/prod/287930) | `8e86c1fe5a5bdcd3ff66fc62857bc15c01d9c7c04fa344b1b56c1fcb3eb793b2` |
| `hny.SCL` | [hny.zip](https://zxart.ee/releasefile/id:304205/hny.zip) (unpacked) | [ZX-Art — Happy New Year ZX.PK.RU](https://zxart.ee/prod/304204) | `0d2492fb43c09abc73081f30f67c0c4714a5b55a8d2eb533051295fbb59f1d43` |
| `BW Demo.trd` | [bwdemofm.zip](https://zxart.ee/releasefile/id:279934/bwdemofm.zip) (unpacked) | [ZX-Art — Black-White Demo](https://zxart.ee/prod/279933) | `1637966adb9f314f1e00362c18bae4006dc2b1c03e1711cd739a504ae70c58fe` |
| `BW Demo_VNN.txt` | [bwdemofm.zip](https://zxart.ee/releasefile/id:279934/bwdemofm.zip) (unpacked) | [ZX-Art — Black-White Demo](https://zxart.ee/prod/279933) | `ac2fb9dc2471141cceb98c6e7e0a206c193ca460e3df6197b9e7d753da883ee4` |

## Contents

- **`TFMWORKS.SCL`** — TR-DOS disk with the first TFM tunes by C-Jeff, Karbofos and Shiru.
  - Six self-contained BASIC programs: `leen`, `dejavu`, `tg2l2`, `snegurka`, `mug`, `cybermot`.
  - Use: TR-DOS listening tests on Pentagon / Scorpion.
- **`TSFM-EL.TAP`** — *TurboSound FM Tunes Collection* (Lanex, 2020), built for eLeMeNt ZX.
  - Blocks: BASIC loader, TSFM player (`_tsfmplaye`, code at 25000), data (`lnxdata`, 31000), and about 100 TFC tunes (each loads at 32768, header `TFMcom1.12`).
  - Use: the player harness (plan P0) — the player and a tune are poked straight into RAM, with no tape loading; also long listening tests.
- **`AYtest_v0.2.scl` / `.tap`** — NedoPC AY / YM / TurboSound configuration test v0.2 (2017). **TurboSound only — it does not test FM.**
  - What it does: detects "NedoPC TURBOSOUND" / "SINGLE AY/YM" / "NO CHIP", reports whether the 2nd chip is enabled, runs a register CRC check, per-channel (left / right / middle) and envelope tests, then plays TurboSound music (Vortex Tracker / ProTracker player) or a beeper/TapeOut test.
  - How that was verified (2026-09-12): the code block is packed, so it was run in unreal-ng (Pentagon, tape) and the unpacked program was inspected.
    - Its UI strings contain no FM / YM2203 / TFM text.
    - Its chip-detect routine (`#A29E`, `#A331`) selects chips only with `OUT #FFFD,#FE` / `#FF` and probes AY registers only.
    - The unpacked code contains no `LD A,#F8`–`#FB` control-word loads (a byte scan, not a full disassembly).
  - Use: TurboSound regression, run on **both** devices. TSFM must pass it exactly as TurboSound does, since with FM never enabled TSFM behaves as a TurboSound (design §11). It is not FM coverage.

- **`dihalttsmus.TRD`** — *DiHalt 2007 TurboSound FM Music*: the DiHalt 2007 TFM music compo pack.
  - Contents: `boot` plus 10 tune launchers (`dx`, `LBF`, `drumtest`, `Rainstor`, `Differen`, `Unbeliev`, `Ikuzo!`, `topor`, `znx_type`, `Overheat`). Each launcher is a BASIC file paired with a `t` TFC tune (header `TFMcom1.12`) and a `d` info text; `drumtest` and `Differen` also carry extra code blocks. There is also a `tfmrez` results file and `readme-r` / `readme-e`.
  - Use: TR-DOS listening, and a wide variety of TFM Compiler 1.12 tunes.
- **`sonic3d.trd`** — *Sonic 3D Blast song #13* (Alone Coder, 2013).
  - Contents: TFD song data split over `sonic3d.t/0/1/2`; `retfdbas.H`, the ALASM source of the RE_TFD player; `sonic3D` BASIC launcher.
  - Use: the only non-TFM-Compiler player here (RE_TFD, a TFD stream player). Its source is on the disk, which makes it a readable reference for how players drive the ports.
- **`hu2010fm.scl`** — *Husmann 2010 Music*: six TFC tunes (`minimod1/2/4/6/7`, `POPCORN`) plus `boot`.
- **`hny.SCL`** — *Happy New Year ZX.PK.RU* (Renegade, 2010): a single TFC tune. At 3.6 KB it is the smallest self-contained case for automated tests.
- **`BW Demo.trd`** + **`BW Demo_VNN.txt`** — *Black-White Demo* (VNN, 2010).
  - Music: Part 1 is FM music by Husmann; Part 2 is *Live & Die* by Visual; Part 3 is *Zima* by NVitia.
  - The author's text (Russian, cp1251) says: "Works only on a computer with a TSFM card installed."
  - Use: a demo, not a music pack. It mixes AY and FM music across its parts.

## How TSFM use was verified (2026-09-12)

Two checks were used.
- **Static:** a byte scan of every file on each disk.
- **Live:** each release was run in unreal-ng (Pentagon 128, TR-DOS) with the AY register-write log on. At the time unreal-ng had no FM chip; the device was the plain TurboSound.

| Release | Static evidence | Live run on plain TurboSound |
|---|---|---|
| DiHalt 2007 | Every launcher embeds the TFM Compiler 1.12 player (`TFMcom1.12` header, `LD A,#F8` / `#F9`, `LD A,#2F`); `readme-e` begins "TurboSound FM music from DiHal[t]" and mentions TFM Music Maker | `Rainstor`: TFM player screen ("Rainstorm / Warlord / My 2nd TFM ever"), `OUT #FFFD,#F8`, then stuck in the busy-wait loop at `#62C1` |
| Sonic 3D #13 | `retfdbas.H` source: `statuschip0=%11111000`, `statuschip1=%11111001`, `WaitStatus`, FM register init, `LD A,#2F` | `OUT #FFFD,#F8` at `#612C`, FM TL init `#4F`→`#40`, `#2F`, `#2D`, then stuck at `#6117` (`IN (C)` / `JP M,#6117`) |
| Husmann 2010 | Each tune embeds the TFM Compiler player (`TFMcom1.`, `#F8` / `#F9`, `#2F`) | `minimod1`: TFM player screen, `OUT #FFFD,#F8`, stuck in busy wait at `#62BF` |
| Happy New Year | TFM Compiler player (`TFMcom`, `#F8` / `#F9`, `#2F`) | TFM player screen ("Hapy New YEAR / Renegade"), `OUT #FFFD,#F8`, stuck in busy wait at `#62EF` |
| Black-White Demo | `B-W demo` and `VNN 2010` blocks contain the TFM Compiler player (`TFMcom1.`, `#F8`–`#FB`, `#2F` / `#2D`, `IN A,(C)`) and TFD data | ~60 s from `boot`: AY music only (one writer at `#C170`, registers `#00`–`#0D`, chip 0); no control word or FM write seen in that time. FM use is supported by the author's text and the embedded player, **not** by the live run. |

**What the stalls mean.** Every TFM player that reached playback selected status mode (`#F8`) and then waited for the YM2203 busy bit to clear. The legacy device answers `IN #FFFD` with a register value, so bit 7 stays set and the player never gets past its first busy check. This reproduces design finding A8 / §4.1 of the TSFM design: the busy flag must really clear. These releases double as regression tests for it: on TSFM they must play; on legacy TurboSound they hang.

A byte scan of `TSFM-EL.TAP`'s player (not a full disassembly) shows the control words and prescaler writes the design expects:
- loads `#F8` / `#F9` (rev. C status-mode chip selects), and also `#FD` / `#FE` / `#FF`;
- writes the `#2F` / `#2D` prescaler addresses at init.

This matches hardware-reference §3.2 and §4.2.
