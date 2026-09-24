# TODO — UDI weak-bit storage + FSE end-to-end (VORON1)

**Complete (2026-09-23)** — storage, authoring tool, tests and the end-to-end run
all landed; outcome and the corrected interpretation in [design.md §10](design.md).

## Done

- [x] Evidence base assembled: TTD journal timeline (silent derailment at the
      `0x5D8C–0x5E30` check stub), FDI damage census, UDI census (incl. the
      forged single-`R=9` track 0, the honest `C=55` copy at cyl 55, the
      lying-C + CRC-bad `R=192` at cyl 59, unformatted 58/80).
- [x] Datasheet verification of Type II sector matching: the FD179X compares
      ID `C` against the **Track Register** — our `locateSectorForType2` is
      correct; the C compare must NOT be removed (lying-C trap depends on it).
- [x] Storage design: `UDIW` weak-map chunk in the UDI trailer (spec-legal
      comment area, CRC-covered, stripped-on-parse / appended-on-serialize to
      avoid duplication). Multi-rev track type deferred (P2).
- [x] Processing design: no FSE changes needed for fuzzy-data/floating-ID
      shapes; two CRC-interplay verification items listed.
- [x] Authoring plan: `tools/voron1-author-weak.py` + candidate marks M1/M2
      with the decision gate (stub disasm + journal io-mining).
- [x] Documentation wiring (2026-09-22): package README §7 update blockquote
      (M1/M2 + storage), `FlakySectorEmulator.md` §2/§5/§6 design pointers,
      `udi.md` `UDIW` section ("designed, not yet implemented"), and the stale
      `flakysectoremulator.h:17` claim corrected (UDI does not fill `_weak`
      today).
- [x] Step 1 (2026-09-23) — `UDIW` parse+serialize in `core/src/loaders/disk/loader_udi.{h,cpp}`;
      `LoaderUDI_Test` round-trip (chunk applied on load, stripped from the preserved
      comment, re-emitted on save — never duplicated)
- [x] Step 2 (2026-09-23) — FSE synthetic unit tests in `wd1793_universal_track_test.cpp`
      (weak IDAM visible on some revolutions, weak data varies per revolution,
      deterministically)
- [x] Step 3 (2026-09-23) — `tools/voron1-author-weak.py`; weak-marked copy (M1)
      authored in `scratch/`
- [x] Step 4 (2026-09-23) — resolved by correction: the 2026-09-22 "silent derailment"
      was the harness's 48K-mode paging lock, not a protection check (design.md §10.2).
      With the fixed 128K boot both stock and weak images load the game identically
      (149 reads, cyl 0–13 + 64–68) — **cyl 59 is never read in the boot path**, so M1
      is inert there (design.md §10.3)
- [x] Step 5 (2026-09-23) — end-to-end pass done; doc status markers flipped to
      implemented (`udi.md`, `FlakySectorEmulator.md`, `flakysectoremulator.h` header,
      package README §7); final mark list = M1 only

## Remaining

- [ ] Identify the lying-C consumer — the protection docs place it "later in the
      disk"; no boot/menu/intro input triggers FDC access after load, so a live
      weak-bit A/B on this title still needs that consumer found (design.md §10.4)
- [ ] M2 (floating ID) mark — unauthored, gated on the same consumer
- [ ] P2 — multi-revolution track type read support (only when a real
      multi-rev capture exists)

Blocking external dependency: none (step 4's journal tools already exist in
[the package tools](../../disasm/black-raven-voron-protection/tools/README.md)).
