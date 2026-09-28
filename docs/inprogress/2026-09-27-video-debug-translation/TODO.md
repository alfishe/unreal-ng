# TODO — mode-aware video debug translation

**Status marker:** not started. Design written and reviewed 2026-09-27.

Design: [design.md](design.md). Plan entry: [PLAN.md](../PLAN.md) #42.

## Progress

- [x] Inventory of ZX-only debug paths (design §1)
- [x] Design with mode catalogue for current and future machines (design §4-5)
- [x] Independent review, findings folded in (design §13)

## Remaining (design §11)

- [ ] Phase 1: types, `IVideoMapper`, `VideoMapService`, `VideoState`; mappers for ZX, ATM, AlCo, Profi HR; renderer-agreement tests
- [ ] Phase 2: front ends (WebAPI/MCP/CLI/Python/Lua) through one field-writer; remove the four beam copies; OCR text path; screen info
- [ ] Phase 3: per-line state log (`state_source`)
- [ ] Phase 4: Qt beam widget in beam coordinates, pixel inspector, memory-viewer overlay
- [ ] Phase 5+: one mapper per new video family as it lands (PHR, Timex/GMX, TSConf, Next, Sprinter, ZX-Poly)
