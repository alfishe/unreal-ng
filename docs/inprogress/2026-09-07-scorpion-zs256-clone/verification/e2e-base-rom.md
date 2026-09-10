# E2E record — base ROM scope (Tasks 0-6)

> Transferred from `scratch/e2e/results.md` (2026-09-09). Raw artifacts (traces,
> screenshots, patched disk, ids) remain in `scratch/e2e/` per the testing-plan
> transcript rule. The work recorded here is committed as `3f49622c`
> ("feat(emulator): Scorpion ZS-256 clone model (base ROM scope)").

Date: 2026-09-08. Binary: `cmake-build-release/bin/unreal-qt.app` (post Beta128
mirror fix). Model: SCORPION, ROM `data/rom/scorpion.rom` (64K, shipped from the
app bundle `Contents/Resources/rom/`).

## E2E-1 — Cold boot to BASIC 128 — PASS

- Fresh instance, `POST /emulator/start {"model":"SCORPION"}`.
- OCR of `GET /capture/ocr`: authentic boot menu `128 TR-DOS / 128 BASIC /
  Calculator / 48 BASIC / 48 TR-DOS` + banner `1992-94 Scorpion ZS 256`.
- Screenshots: `scratch/e2e/e2e1-boot.gif` / `e2e1-boot.png`.

## E2E-2 — Boot menu → TR-DOS session with a real disk — PASS (all 4 criteria)

Preconditions: `Satisfaction.trd` in drive 0. A patched copy
`Satisfaction-noboot.trd` (boot entry renamed, offset 0x40 in sector 1) was used
to suppress the boot-file autorun so the TR-DOS command prompt is reachable.

1. **Trap-based session** — port trace (`scratch/e2e/porttrace-e2e2-fixed.json`,
   200k events): monitor menu RAM-probe (7FFD=0x00..0x07 / 1FFD=0x02), RAM-resident
   monitor part sets 7FFD=0x10 + 1FFD=0x10, trap fires, session opens
   (cf_trdos=True), TR-DOS 5.03 boots and talks to the FDC. Also reproduced via
   `RANDOMIZE USR 15616` (#3Dxx fetch trap) from 128K BASIC.
2. **CAT lists the catalog** — final OCR:
   `Title: / 5 File(s) / A:SATISFAC<B>255 satisf-A<C>255 / satisf-B<C>255
   satisf-C<C> 33 / notboot<B> 11 / 1735 Free / A>K`. Byte-identical listing on
   a PENTAGON control instance (same disk).
3. **File load → session close → BASIC ROM** — the unpatched disk's `boot`
   file chain executed live: sector-by-sector load (OUT #5F sector++, OUT #1F
   0x80 READ SECTOR, DRQ handshake at IN #FF 0x7C), then RAM execution at
   #5Exx; session subsequently closed with #0000 back at a BASIC ROM.
4. **FDC reads non-0xFF** — 98k mirror IN #1F + 79k IN #FF decoded with real
   values (status 0x06/0x24/0x20, DRQ/INTRQ readback 0x7C/0xBC) vs 110,556
   floating 0xFF reads before the fix.

## E2E-3 — MNI button — PASS

- 128 BASIC booted; signature `E2E3SIG!` written at #C000 via
  `POST /memory/write` (read back OK).
- `POST /nmi {"magic":true}` → #0000 = `37 CB 7C C3 D9 04 18 61` (service
  monitor), pc in monitor, iff1=0, monitor register display on screen.
- Keyboard exit (SPACE then ENTER) → monitor unlatched (ROM1 momentarily, then
  ROM0), #C000 signature `E2E3SIG!` intact, iff1=1, `128 BASIC` screen restored.

## Root cause fixed during E2E-2

`PortDecoder_Scorpion256` matched Beta128 ports with exact 16-bit compares, so
real TR-DOS 5.03 mirror accesses (`IN A,(#1F)` → bus #FF1F; `OUT (#3CFF),A`
drive select) never reached the WD1793 (IN floated 0xFF; mirrored system-port
OUTs were eaten by the border arm). Fix: `TryBeta128MirrorPort()` low-byte
match + canonical-key dispatch in both DecodePortIn/DecodePortOut (same
normalization the Pentagon decode table performs). Regression tests:
`ScorpionPorts_Test.FdcMirrorPortsRoundTripInsideSession`,
`.FdcMirrorSystemPortOutBeatsBorder`, `.FdcMirrorPortsGatedOutsideSession`.

## Follow-ups (general, not Scorpion-specific; Pentagon shows identical symptoms)

- TR-DOS prompt ignores raw WebAPI keyboard taps (keys DO reach LASTK/KSTATE);
  the BasicEncoder injection path works. Use `POST /basic/run` for TR-DOS
  commands.
- `BasicEncoder` TR-DOS injection leaves the emulator **paused** — resolved on
  master in `52bc38a2` ("Auto-resume emulator after TR-DOS command injection").
- Boot menu keys need 0.4s+ holds; digits don't register (ENTER selects).
- Satisfaction demo's boot chain runs but its title screen waits on something
  (motor/index timing?) — addressed on master in `b6d11af7` ("Tie index strobe
  to disk rotation timing, not HLT edges").

## Quality gate

- Build: zero warnings (touched TUs).
- Full suite: **2249/2249 PASSED** (221 suites, 27.7s) — includes the 3 new
  mirror regression tests. Logs: `scratch/full_suite_t9.log`,
  `scratch/build-mirrorfix.log`.

Artifacts: `scratch/e2e/` (traces, screenshots, ids, patched disk).
