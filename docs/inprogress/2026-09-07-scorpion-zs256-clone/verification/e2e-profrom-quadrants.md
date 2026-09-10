# E2E record — ProfROM quadrant switching (E2E-4, Task 7)

> Transferred from `scratch/e2e-t7/results.md` (2026-09-09). Raw artifacts
> (probe scripts, logs, screenshots) remain in `scratch/e2e-t7/` and
> `scratch/`. The Task 7 work recorded here is in the working tree, not yet
> committed.

Date: 2026-09-09. Binary: `cmake-build-release/bin/unreal-qt.app` (commit `535b8238` + Task 7 working tree).
Image: `data/rom/scorp_prof401.rom` (512 KB, 8 quadrants). Model: `PROFSCORP`, RAM 1024 KB.

## Probe

`scratch/e2e-t7/probe.py` builds a DI-wrapped routine at #8000 (debugger-driven: pause → write →
pc=#8000 → resume → pause → harvest, ends in `JR $` so the harvest is stable):

1. `#1FFD=2` service ROM at #0000 (strobe gate armed), Q0
2. walk `r#0001 + 4×r#0000` → GAL 0→1→3→1→3→1, saves post-switch svc bytes #A010-#A014
3. `#7EFD=0x10` → window 1 | gal 1 → quadrant 5
4. `#1FFD=0` → page 1 of Q5 at #0000-#3FFF; read #3200/#3201 → #A015/#A016
5. restore → Q0 (service sentinel `r#0000` from Q0 = 37 → #A017), park

Ground-truth bytes from the image file: svc(Q0)=`37 cb…`, svc(Q1/Q2/Q3)[0]=00;
page1@#3200: Q0/Q4 (twins)=`e1 5f`, Q1=`5a 78`, Q5=`43 4f`.

## Result (fresh boot, instance 4933b77f, `scratch/e2e-t7/probe4.log`)

```
buffer A010..A017: 00 00 00 00 00 43 4f 37   -> matches 'correct'
```

- walk bytes `00 00 00 00 00` = svc(Q1)[1], svc(Q3)[0], svc(Q1)[0], svc(Q3)[0],
  svc(Q1)[0] — the Q1↔Q3 alternation of the verified table, read through the
  fast read path with mid-instruction remap
- window bytes `43 4f` = page1(Q5)@#3200 — #7EFD window select + GAL composition
  reached quadrant 5 (inert window would read Q1 `5a 78`; Q0/Q4 are twins so the
  window test is run with gal=1 deliberately)
- sentinel `37` = svc(Q0)[0] after restore strobes (r#0000: 1→3, r#0001: 3→0)

Two additional runs from non-zero start states (gal=3: `cb 37 37 37 37 43 4f 00`,
gal=1: `00 00 00 00 00 e1 5f cb`) also match the table exactly — including the
Q0-transparent no-op (`#0000` from Q0) and the Q4≡Q0 twin pair.

## Q0 boot stability (3 cold restarts)

```
boot 1: #0000..3=f3 c3 44 08  pc=15873 (#3E01, 128K menu)
boot 2: #0000..3=f3 c3 44 08  pc=15873
boot 3: #0000..3=f3 c3 44 08  pc=15873
```

Identical page-0 tag (Q0 BASIC128 role mapped, p7FFD bit4=0 during menu),
deterministic menu pc; after menu timeout the machine settles into the
transparent 48K BASIC boot ("1982 Sinclair Research Ltd", see
`scratch/e2e-t7/boot*.png` / earlier read_text transcript on instance ddd43dcb).

## Notes

- E2E-4's keyboard-driven ROM-disk menu navigation is impractical over WebAPI;
  quadrant transitions were observed via memory instead (allowed alternative in
  the pass criteria).
- Incidental, out of Task 7 scope: `DELETE /emulator/{id}` while the Qt main
  window has adopted that instance SIGSEGVs in `MenuManager::updateMenuStates`
  (via `handleFDDDiskChanged`) — see `/tmp/unreal_crash_1788977067_52015.log`.
  Reproduce: create PROFSCORP instance, wait for the 128K menu's FDD polling,
  DELETE the instance. Workaround in this session: leave instances alive.

## Artifacts

- `scratch/e2e-t7/probe.py` / `probe_dbg.py` — probe driver + debugger-driven variant
- `scratch/e2e-t7/probe4.log` — passing run; `probe2.log`/`probe3.log` — debugging history
- `scratch/e2e-t7/boot-stability.log` — 3 cold restarts;
  `render_screen.py`/`read_text.py` — screen tools
- app logs: `scratch/e2e-t7-app4.log`, `scratch/e2e-t7-app5.log`
