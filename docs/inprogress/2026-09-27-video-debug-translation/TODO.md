# TODO — mode-aware video debug translation

**Status marker:** in progress - phase 1 done 2026-09-29 (branch `video-mapper`). Design written and reviewed 2026-09-27.

Design: [design.md](design.md). Plan entry: [PLAN.md](../PLAN.md) #42.

## Progress

- [x] Inventory of ZX-only debug paths (design §1)
- [x] Design with mode catalogue for current and future machines (design §4-5)
- [x] Independent review, findings folded in (design §13)

## Remaining (design §11)

- [x] Phase 1 (2026-09-29): `video/map/` types, `IVideoMapper` + `MemView`, `NullVideoMapper`, `VideoMapService` (Layout, BeamAt, SourcesAt / SourcesAtBeam, PixelsFor / PixelsForZ80, Text, Z80 aliases), `VideoState` derived from the latches; mappers `ZxVideoMapper` (every mode `ScreenZX` draws as ZX, incl. P384), `AlcoVideoMapper` (P16, PMC), `AtmVideoMapper` (ATM16/HR/TX/TL), `ProfiVideoMapper` (Profi HR); shared family tables `zxgeometry.h`, `atmgeometry.h`, `profigeometry.h` used by the renderers too, and `videofamily.h` (`FamilyOf`) as the one renderer-family switch. Tests `videomapservice_test.cpp`: colour agreement with the framebuffer, round trip, bit-flip, beam window corners for 10 modes; text grid; Z80 addresses
- [ ] Phase 2: front ends (WebAPI/MCP/CLI/Python/Lua) through one field-writer; remove the four beam copies; OCR text path; screen info
- [ ] Phase 3: per-line state log (`state_source`)
- [ ] Phase 4: Qt beam widget in beam coordinates, pixel inspector, memory-viewer overlay
- [ ] Phase 5+: one mapper per new video family as it lands (PHR, Timex/GMX, TSConf, Next, Sprinter, ZX-Poly)
