# TAP fixtures

Tape images used by the loading tests and the sweep (`tools/verification/tape/tape-sweep.sh`).

## Non-standard loaders (Dizzy X releases)

Seven releases of the same game, each with its own cracked loader. They are the stress test
for tape emulation: custom pulse routines, R-register encryption, watchdog-style checks.
Results per release: [`nonstandard-loader-investigation.md`](../../../docs/inprogress/2026-08-30-fast-tape-loading/nonstandard-loader-investigation.md).

| File | Note |
|:--|:--|
| `DIZZY_X_KID__DR.tap` | **Checks the ROM.** Requires byte `#20` at ROM `#006D` (original Sinclair 48K ROM) - three times, once per interrupt. On a machine with a patched 48K ROM (unreal-ng Pentagon uses `48for128.rom`) it wipes memory and derails. Annotated disassembly: [`docs/disasm/software/dizzy-x-kid-dr-loader/`](../../../docs/disasm/software/dizzy-x-kid-dr-loader/README.md) |
| `DIZZY_X_ALEX_S__MAX_IWAMOTO.tap`, `DIZZY_X_CHEFRANOV_VALENTIN.tap`, `DIZZY_X_EMELYANOV_PAVEL.tap`, `DIZZY_X_HACKER_SHURIK.tap`, `DIZZY_X_SAN-SAN.tap`, `DIZZY_X_TIMOFEY_YUNAEV__ROBERT_MAKSIMOV.tap` | Other releases, see the investigation notes |
