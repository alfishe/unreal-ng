# ZXM-MoonSound test material

Third-party fixtures for the MoonSound (ZXM-MoonSound / YMF278B / OPL4) integration: the complete
software set from the card author's site — 26 runnable TR-DOS disks (demo and music collections,
MFM sample collections and the MoonService diagnostic utility), plus the author's source-code
archives for every program. Real guest software that detects the card through the port protocol
and plays music. Integration design: `docs/inprogress/2026-09-13-moonsound/`.

Retrieved 2026-09-14. All files are freely distributed by the publisher; authors' rights remain
with the authors.

## Copyright

- All programs are by **Mick** (Mick Laboratory, http://micklab.ru) — the ZXM-MoonSound card
  author. Music: **Naruto** (Moonsound 1), **Qix** (Moonsound 5), **Bart Roymans** (Moon Music 1),
  **Near Dark** (Moonsound 14); co-author **AAA** (Moonsound 13, Moon Music 2). Pictures:
  **Andrew_curds** (Moonsound 3-5), **r0bat** (Moon Music 1).
- The **YMF278B (OPL4) chip and its YRW801 wave data ROM are Copyright (c) 1993 Yamaha
  Corporation**. The disks contain no ROM data of their own — PCM samples play from the wave ROM
  image shipped with the emulator, whose attribution is in
  [data/rom/README-ROMS.md](../../../data/rom/README-ROMS.md).

## Source

- Publisher page (card and software by the same author):
  http://micklab.ru/My%20Soundcard/ZXMMoonSound.htm
- Files base URL: `http://micklab.ru/file/zxm_moonsound/soft/` (each `.rar` downloads by name)
- Each disk's original `.rar` is kept next to the extracted `.trd` so the distributed bytes stay
  verifiable; `*src.rar` are the author's source archives (kept as distributed, not extracted).
- The Unreal-variant archives also contain `unreal_60hz.jpg` — the author's screenshot of the
  Unreal emulator configured for the 60 Hz music (left inside the archives).
- The page's `yrw801m_1993.rar` unpacks to `YRW801-M - Yamaha - 1993.rom`, byte-identical to
  `data/rom/opl4/yrw801-m-yamaha-1993.rom` (repo copy renamed per the no-spaces kebab-case rule;
  verified 2026-09-14, MD5 `42af93619160ef2116416f74a6cb12f2`).

## Contents

All disks are standard double-sided TR-DOS images (655360 bytes) with a `boot` file that auto-runs
when the machine is booted into TR-DOS with the disk in drive A. Melodies are MoonBlaster (MSX)
material in the MWM or MFM format, switched with the listed key.

- **Moonsound 1-14** (`moonsound[_N].trd`, 2015-2016) — the demo series, "musical greeting card"
  programs for the card: 1 — 4 tunes by Naruto, keys A-D; 2-4 — 6 tunes each, keys A-F, pictures
  by Andrew_curds; 5 — 14 lyrical tunes by Qix (picture Andrew_curds); 6 — 12; 7 — 18; 8 — 13;
  9 — 19; 10 — 12; 11 — 30 short; 12 — 7 (apparently game covers); 13 — 19 (with AAA); 14 — 12 by
  Near Dark; key Space from 5 on. Wave-ROM sample playback — the classic detection + playback
  smoke tests.
- **Moon Music 1 / 2** (`moonmusic_1.trd`, `moonmusic_2.trd`, 2016) — MWM collections: 1 — 12
  tunes by Bart Roymans (picture r0bat), 2 — 14 tunes (with AAA). **The music is 60 Hz** — the
  author warns it may not run on some real machines.
- **Moon Music 1 / 2, Unreal variant** (`moonmusic_1u.trd`, `moonmusic_2u.trd`) — the same
  collections rebuilt for the Unreal emulator family (this emulator's direct ancestor) with the
  60 Hz timing. The most relevant disks here.
- **MFM Music sample 1-4** (`mfm_sample[_N].trd`, 2016) — MFM-format collections: 1 — 2 tunes;
  2 — 15; 3 — 13 plus an animation that needs a >128K machine with bit-7 `#7FFD` paging; 4 — 8;
  Space or auto-advance.
- **MoonService v0.1-v0.3a** (`moonservice_v0*.trd`, 2015-2016) — the card's service utility:
  v0.1 — SRAM test (the user-instrument RAM); v0.2 — adds the wave-ROM integrity check (16 KB
  block checksums against reference values); v0.3 — adds flash-ROM firmware update (needs a
  Z-Controller SD interface — real-hardware only, expected not to work under emulation); v0.3a —
  fixes erase timing for MX29F016 flash. The best single detection/diagnostic probe.
- **`moonservice_v03a_service.bin`** — the assembled MoonService v0.3a image from the author's
  source archive (`moonservice_v03asrc.rar`, org `$6000`, 10451 bytes, covers `$6000-$88D3`).
  Guest-level integration test fixture: loaded straight into RAM it runs the author's own
  detection protocol and parks the detected device ID (`$20` = YM278B) at `$88D0`.
- **`moonsnd.rom`** (from `moonservice_v03.rar`) — the wave ROM image MoonService writes into the
  card's flash; byte-identical to `data/rom/opl4/yrw801-m-yamaha-1993.rom` (kept as received).

## Testing notes

- Requirements: `[SOUND] MoonSound=1`, wave ROM present (`[ROM] MOONSOUND` /
  `[MOONSOUND] WaveRom` key — the shipped `data/configs/scorpion/unreal.ini` has both), and a
  model with a TR-DOS (Beta-128) interface to boot the `.trd` (e.g. SCORPION).
- Expected: the programs detect the card (wave register 2 device-ID readback, memory-window probe
  through `#7E`/`#7F`), then play FM and wave-ROM PCM music through the MoonSound FM / MoonSound
  PCM mixer sources.
- `moonservice_*.trd`: detection and the SRAM / wave-ROM tests are the useful part; the flash-ROM
  update (v0.3+) targets real card hardware (AM29F016/MX29F016 via Z-Controller SD) and is not
  expected to work under emulation.

## Checksums

| File | SHA-256 |
|---|---|
| `moonsound.trd` | `b2cadc5020aa0d1f54328e6409233dc2a8978b1aa2220f9fb948092c373fd442` |
| `moonsound_2.trd` | `0fe6c25694d2f6bb9faa63b6563e3bf569392203d427cd90fe10aa2eab5c17c0` |
| `moonsound_3.trd` | `81ac34d897546177daf48aa333fb2f641a10ac8a9a329b65f7bc259feb088498` |
| `moonsound_4.trd` | `6a001f839c9b50015b061fe291abde448627d9b61c2aec37bab2b8d5b1639d1e` |
| `moonsound_5.trd` | `b3c083ab425bfe6383cdc2181a5ed6eca8f2a6cd6691faf20e20059f795b2594` |
| `moonsound_6.trd` | `b8bff371df7a0fcec106a7fe57a478add4980ad55c443d3e21107e0c378dadb0` |
| `moonsound_7.trd` | `b39d38daa54c0c372dab38d9994d60f9f1f8c5dcd4fa38c4448decaacbfefcad` |
| `moonsound_8.trd` | `4cf382d5251ed418e34023e94fc3631f7ba613f19c882122e6feaa659394a311` |
| `moonsound_9.trd` | `e6c418dede76dc3de1bc5053231264507cc9da8f258e39108b84662edeb72ac0` |
| `moonsound_10.trd` | `8c93fa7dbe6daec5ed5b710a8a879772fd147809a334a2afb71ae23484a74467` |
| `moonsound_11.trd` | `37c463f32e01b2b57554bd8d36cd5aebd9de909ede722791a02965af55a3a767` |
| `moonsound_12.trd` | `fa49af6e08bb1ed8719eab1477bee166778ef63438625fa4f97f7ca06eaa974b` |
| `moonsound_13.trd` | `6ffbdb0533306059494ca2c723898ff06758728cbc403bc49df64c5350b77b08` |
| `moonsound_14.trd` | `fa6edc4458bf5ce9e8710ed197515914936b5aa2bc8ec81e9298816fec4cae86` |
| `moonmusic_1.trd` | `1e348c49255d293af8a882f9e722f84f17a86aaf5534414aed005d9931f56436` |
| `moonmusic_1u.trd` | `07a23d556dd1c5b896172440da7aeb2779d1c4de23a232a9780e5a81d6e93334` |
| `moonmusic_2.trd` | `c8edc05ee4ab3ac6df1eb7848f069728f9481245c9c1610ea551060b864c1874` |
| `moonmusic_2u.trd` | `5758cd3e9956e2280b803620d08e5479ce7b0b9513971527c39f4f32c9752dcd` |
| `mfm_sample.trd` | `93e708172f7e8fc7d821ed3aa9fe3c655e977fcf057d43e61beeaf444c5bbd3d` |
| `mfm_sample_2.trd` | `f6ca0eeff3b026ef34336999dfe0ecf955b668e531fdcfd51ee721126a46399f` |
| `mfm_sample_3.trd` | `a38d8cde5fe9eca6dff9c3fb63f9aec7d1921f461493f20bc3ea83a59f19098e` |
| `mfm_sample_4.trd` | `64fe41b752a3a0f5c0d51badf5b933d82c597fc05d88b824a3ce59f135c397a4` |
| `moonservice_v01.trd` | `e9aed749807ca24924731a024037c607966764d2876362d56ad5e2f1669378c2` |
| `moonservice_v02.trd` | `0540c9d0c530688f9eb2089bd84d7720b45fbe04dffd9efd65a5cf7c23bf6ca0` |
| `moonservice_v03.trd` | `10e9cce335fa2c742db20dc558ca928402970336f2163f76a76faf6df278fe0e` |
| `moonservice_v03a.trd` | `aa45672cc2770316dc4b9ff86af6989b0a556b6db218092a9e8e126986b0bceb` |
| `moonservice_v03a_service.bin` | `c5c7fce3e2d68ec8f76bc63f3663cafc83db6a9b090a864374ae6fdadc5d30d3` |
| `moonsnd.rom` | `0481e861f639fa3d6a64a75b735035a54b0875d57aaae33fdbe400146c80423f` |

Source archives (author's code for every disk above): `moonsound_src.rar`, `moonsound_2src.rar` …
`moonsound_14src.rar`, `moonmusic_1src.rar`, `moonmusic_2src.rar`, `mfm_sample_src.rar`,
`mfm_sample_2src.rar` … `mfm_sample_4src.rar`, `moonservice_v01src.rar` … `moonservice_v03asrc.rar`
(24 archives, kept as distributed).
