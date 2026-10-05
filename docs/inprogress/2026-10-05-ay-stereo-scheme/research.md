# AY stereo scheme (ABC / ACB) per machine

Question: should Pentagon / TS-Conf / BaseConf / Profi / ATM ship ACB and the classic Spectrums ABC?
Answer (2026-10-05): no. Emulators and hardware sources agree on **ABC for the clones except Profi**,
which is **ACB**. The `[AY] Stereo=` key is now honored (it was parsed by nothing before).

| Source | Finding |
|:--|:--|
| Fuse `sound.h` | ACB is the Melodik interface; ABC is used in the Pentagon / Scorpion |
| Unreal Speccy (all `unreal.ini`) | ABC everywhere; its tape-menu labels the Melodik "AY ACB stereo" |
| Xpeccy+ `machines-reference.md` | Pentagon, Pentagon 1024 SL, ATM Turbo 2+, ZXM-Phoenix, ZX-Evo (BaseConf, TSConf): ABC. Profi v3 / v5: ACB. Sinclair line: ACB |
| Karabas-Pro RTL (Profi) | ACB by default (`soft_sw(7)=0`), ABC selectable |
| ZX-MultiSound schematic | ACB with B in the centre |
| ZX Next `turbosound.vhd` | 0 = ABC (default), 1 = ACB |

A real 128K has no stereo output at all, so "classic = ABC" is a convention, not a hardware fact.

## Decision

- `[AY] Stereo = ABC | ACB | MONO` is read by `Config` into `config.sound.ayStereo` and applied to
  every AY and TSFM SSG chip when the sound stack is built (`SoundManager`).
- Shipped configs: `profi` and `profi3` = ACB, every other machine = ABC.
- Runtime changes (Qt panel, automation) are not written back.

Tests: `Config_Test.AYStereoParsesAndDefaultsToAbc`, `Config_Test.ShippedConfigsCarryTheExpectedAYStereo`,
`SoundManagerVoicing_Test.ConfiguredStereoSchemeReachesEveryChip`.
