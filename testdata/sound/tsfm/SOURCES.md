# TurboSound FM test material

Third-party files for TSFM development and regression listening. Design: `docs/inprogress/2026-09-10-turbosound-fm/`.

Retrieved 2026-09-12. All files are freely distributed by their publishers; authors' rights remain with the authors.

| File | Downloaded from | Publisher page | SHA-256 |
|---|---|---|---|
| `TFMWORKS.SCL` | [tfm.zip](http://nedopc.com/TURBOSOUND/tfm.zip) (unpacked) | [NedoPC — TurboSound FM](http://nedopc.com/TURBOSOUND/ts-fm.php) | `0cf8449e3bb69947973ec534bcc98b1db87fc5c112edba2d888d1c312159fc59` |
| `TSFM-EL.TAP` | [TSFM-EL.TAP](https://zxart.ee/releasefile/id:539651/TSFM-EL.TAP) | [ZX-Art — TurboSound FM Tunes Collection](https://zxart.ee/eng/software/demoscene/art-pack/music-collection/turbosound-fm-tunes-collection/) | `389ff968ab763d8826e531bd268a8901d34327c93f9208fb2d30522af1f56e47` |
| `AYtest_v0.2.scl` | [AY_YM_TS_test_v02.ZIP](http://nedopc.com/TURBOSOUND/tools/AY_YM_TS_test_v02.ZIP) (unpacked) | [NedoPC — TurboSound FM](http://nedopc.com/TURBOSOUND/ts-fm.php) | `5eba8570996f7e2f6bfb75f694e4e22e5cdc787e56be48d26c3b6ea6653bc3e0` |
| `AYtest_v0.2.tap` | [AY_YM_TS_test_v02.ZIP](http://nedopc.com/TURBOSOUND/tools/AY_YM_TS_test_v02.ZIP) (unpacked) | [NedoPC — TurboSound FM](http://nedopc.com/TURBOSOUND/ts-fm.php) | `ebdf851f78fde36a538402fe83457ede00eab25ed40fac904eebcfaad03581bf` |

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

A byte scan of `TSFM-EL.TAP`'s player (not a full disassembly) shows the control words and prescaler writes the design expects:
- loads `#F8` / `#F9` (rev. C status-mode chip selects), and also `#FD` / `#FE` / `#FF`;
- writes the `#2F` / `#2D` prescaler addresses at init.

This matches hardware-reference §3.2 and §4.2.
