# Contention, ULA snow and the test programs: what is left

**Date:** 2026-10-01 · **Plan:** PLAN #61 (its six follow-ups are done) · **Started from:**
[m1-contention TODO](../2026-09-28-m1-contention/TODO.md), [ula-snow TODO](../2026-09-29-ula-snow/TODO.md),
[machine-waits TODO](../2026-09-29-machine-waits/TODO.md), [coemu follow-ups TODO](../2026-09-29-coemu-followups/TODO.md),
[fusetest core defects TODO](../2026-09-30-fusetest-core-defects/TODO.md)

One list of everything still open around memory contention, ULA snow, the turbo wait states and the timing test
programs. Each row says where the details are. Order of work agreed on 2026-10-01: the documentation refresh (D),
then C4, then C3.

## Needs a real machine

The kit: [tools/verification/contention/README.md](../../../tools/verification/contention/README.md) (what to
run, what to photograph, what each answer settles).

| ID | Question | Program |
|:--|:--|:--|
| H1 | 128K / +2: does a port whose high byte points at an odd page at #C000 wait? | ctprobe P-05 |
| H2 | Which Scorpion boards (yellow, green, Turbo+, ProfROM) have Even M1 | ctprobe, the machine line |
| H3 | Where a Scorpion with Even M1 starts the code (the probe's Even M1 correction) | ctprobe P-02 |
| H4 | Does the 128K / +2 snow (MiSTer: no; Weiv's videos: yes); a 48K photo confirms the model | snowtest |
| H5 | Does a +2A / +3 or a clone snow (expected: no) | snowtest |
| H6 | The Scorpion's turbo wait states, measured | none yet (C5) |
| H7 | Which grey +2 revisions got the fixed paging HAL (no read-clocked #7FFD) | fusetest "0x7ffd read" |

## Code

| ID | Item | Status | Details |
|:--|:--|:--|:--|
| C1 | ZX-Evo 48K / 128K rasters and their contention: the rule is derived from the RTL, the rasters are not modeled | deferred to PLAN #55 | [research-zxevo.md](../2026-09-29-machine-waits/research-zxevo.md) section B |
| C2 | ATM3 clock select taken at the next opcode fetch's refresh, not the next frame | in progress in another session (2026-10-01, `tdd-e8b-board-fidelity.md` of the ATM BaseConf folder) | [machine-waits TODO](../2026-09-29-machine-waits/TODO.md) |
| C3 | Scorpion Turbo+: 3.5 MHz while /INT is active; the SC15.3 firmware as an option; the turbo waits cost the Scorpion ~3.5 % per frame (its ROM leaves turbo on) - the slot wait by arithmetic instead of a loop | open, next after C4 | [machine-waits tdd.md](../2026-09-29-machine-waits/tdd.md) section 4, [research-scorpion-turbo.md](../2026-09-29-machine-waits/research-scorpion-turbo.md) |
| C4 | ctprobe: a floating-bus check on a port whose high byte is in contended memory (the defect fusetest found, seen by the harness on all eleven emulators) | open, next after D | [fusetest core defects](../2026-09-30-fusetest-core-defects/TODO.md) |
| C5 | Test programs: one for the turbo waits (Scorpion 7 MHz, ZX-Evo 14 MHz); fusetest under the harness (a wrapper with `DONE`); the Butler 128K suite (`.szx`); HALT and the interrupt acknowledge (M1-10, D-05) are not measurable by the probe's engine | open | [test-programs.md](../2026-09-28-m1-contention/test-programs.md), [fusetest README](../../../tools/verification/contention/fusetest/README.md) |
| C6 | Snow on other emulators: snowtest is visual, the harness compares memory - a dump mode, then xpeccy-plus (`snow` option), ZX-M8XXX, SpecEmu | open | [ula-snow TODO](../2026-09-29-ula-snow/TODO.md) |
| C7 | Performance of the contended machines: fetches from contended memory cost the 48K +17 %, the +3 +28 % | ideas backlog | [baseline.md](../2026-09-28-m1-contention/baseline.md) section 3.2 |
| C8 | The Unreal Speccy family (classic 0.39.0, nedopc, Unreal NS) under the harness on Windows: instructions ready, not run | waiting for a Windows run | [windows-agent-unreal-speccy.md](../../../tools/verification/coemu/windows-agent-unreal-speccy.md) |

## Documentation

| ID | Item | Status |
|:--|:--|:--|
| D1 | test-programs.md section 3 listed X-04 and the cross-emulator runs as open, and fusetest as not done | done 2026-10-01 |
| D2 | contention-by-machine.md section 13 questions 2-3 (Scorpion turbo) answered by the SC15.1 research | done 2026-10-01 |
| D3 | The m1-contention TODO's follow-up 2 still said "Next: follow-up 3"; PLAN #61 still said "follow-up 2 next" | done 2026-10-01 |

## Outside unreal-ng

| ID | Item |
|:--|:--|
| E1 | Report the differences found in other emulators upstream (MAME's 128K 2 ticks late, ZX-M8XXX's internal ticks, spec_chum's internal ticks and TR-DOS paging, Kozynax's "late" ULA, fusetest's broken Pentagon detection); each runner's README has the details |
| E2 | Machines unreal-ng does not have (Leningrad-1, Timex SCLD, Quorum, ATM Turbo 1, Pentagon 1024SL turbo): no primary source for their memory slots ([contention-by-machine.md](../2026-09-28-m1-contention/contention-by-machine.md) section 13) |
