# Status: TODO

## Overview
Technical design to extend the **Pentagon 1024K** machine configuration in Unreal-NG with Alone Coder's **16-color video mode v1.1** (Info Guide #08 / #09, Oct 2005).

## Status Tracker

### Already Implemented (Verified in Codebase)
- [x] Hardware schematics and signal multiplexing analysis (§2)
- [x] Memory layout & byte-packing specifications (§3)
- [x] Cross-reference with Unreal Speccy, ZXMAK2, Xpeccy-Plus (§3.3) — pixel extraction & plane addressing match
- [x] Port `#EFF7` read/write in `PortDecoder_Pentagon1024` — stores in `state.pEFF7`
- [x] Video mode detection: `Screen::DetectModePentagon` maps `EFF7_4BPP` → `M_P16`
- [x] Rendering pipeline: `ScreenZX::DrawAlcoMode` handles `M_P16` pixel fetch
- [x] TTD persistence: `TimeTravelManager` captures `pEFF7` in frame deltas

### Implementation Complete
- [x] **InitRaster() notification** — `Port_EFF7_Out` calls `_context->pScreen->InitRaster()` on video bit change (follows ATM3 pattern)
- [x] **GetActiveSurfaceRAMPages** — M_P16/M_PMC now return `{videoPage^1, videoPage}` = `{4,5}/{6,7}` (was falling back to default)
- [x] **Unit tests** (§8.1–8.4) — 12 port decoder tests + 8 video mode tests = 20 tests passing

### Automation Interfaces Complete
- [x] **WebAPI `/state/screen/mode`** — reports actual video mode, resolution, EFF7 bits
- [x] **WebAPI `/state/paging`** — includes `eff7_flags` object with 16col/512/hwmc/384 status
- [x] **MCP `inspect_state`** — `video` aspect calls `/state/screen/mode`
- [x] **CLI `state screen mode`** — shows video mode details including EFF7 state
- [x] **Lua `screen_video_state()`** — returns table with video_mode, bpp, colors, eff7 state
- [x] **Python `screen_video_state()`** — returns dict with same info

### Remaining Work
- [ ] **SZX snapshot extension** (§7.1) — custom `EFF7` chunk for external snapshot interchange

### Deferred / Out of Scope
- [ ] GigaScreen mode (`EFF7_GIGASCR`) — separate feature
- [ ] 512×192 mode (`EFF7_512`) — separate feature

## Primary Documents
- [`technical-design.md`](technical-design.md) — Comprehensive technical specification and architectural plan.

## Implementation Order
1. Add `Screen::OnPortWriteEFF7()` method
2. Wire notification from `PortDecoder_Pentagon1024::Port_EFF7_Out()`
3. Add unit tests for port decoder video mode notification
4. Add video mode detection tests
5. Add rendering accuracy tests
6. Extend WebAPI/MCP state reporting
7. (Optional) SZX snapshot extension
