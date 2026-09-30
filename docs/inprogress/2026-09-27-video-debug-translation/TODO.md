# TODO — mode-aware video debug translation

**Status marker:** in progress - phases 1-3 done 2026-09-29; phase 4 (Qt) postponed 2026-09-30 (not needed by TSConf, which only needed phase 1; resumes with the debugger work, PLAN #42). Design written and reviewed 2026-09-27.

Design: [design.md](design.md). Plan entry: [PLAN.md](../PLAN.md) #42.

## Progress

- [x] Inventory of ZX-only debug paths (design §1)
- [x] Design with mode catalogue for current and future machines (design §4-5)
- [x] Independent review, findings folded in (design §13)

## Remaining (design §11)

- [x] Phase 1 (2026-09-29): `video/map/` types, `IVideoMapper` + `MemView`, `NullVideoMapper`, `VideoMapService` (Layout, BeamAt, SourcesAt / SourcesAtBeam, PixelsFor / PixelsForZ80, Text, Z80 aliases), `VideoState` derived from the latches; mappers `ZxVideoMapper` (every mode `ScreenZX` draws as ZX, incl. P384), `AlcoVideoMapper` (P16, PMC), `AtmVideoMapper` (ATM16/HR/TX/TL), `ProfiVideoMapper` (Profi HR); shared family tables `zxgeometry.h`, `atmgeometry.h`, `profigeometry.h` used by the renderers too, and `videofamily.h` (`FamilyOf`) as the one renderer-family switch. Tests `videomapservice_test.cpp`: colour agreement with the framebuffer, round trip, bit-flip, beam window corners for 10 modes; text grid; Z80 addresses
- [x] Phase 2 (2026-09-29, branch `video-map-frontends`): the field-writer is `StateNode` - `DeviceState::VideoBeam / VideoLayout / VideoPixel / VideoPixelAtBeam / VideoAddress / VideoAddressZ80 / VideoText` (`devicestatevideo.cpp`); WebAPI `/video/beam` (now + `layers[]`), `/video/layout`, `/video/pixel`, `/video/address`, `/video/text` + OpenAPI (`openapi_video.inc`); CLI `beam` and `video layout|pixel|address|text`; Lua / Python `beam_position` and `video_*`; MCP aspects `video_layout`, `video_text` (pixel / address through `invoke_api`); the four beam copies replaced by `VideoBeam`; OCR reads text modes exactly (`ScreenOCR::textLayerScreen`). Not done here: `/state/screen*` stays on `DescribeScreenState` (already mode-aware since #42a)
- [x] Phase 3 (2026-09-29, branch `video-write-log`): `VideoWriteLog` (`video/map/videowritelog.h`) - the latches after each video port write (Screen: `InitRaster`, `SetActiveScreen`, `SetBorderColor`) with the frame T, current + previous frame, 4096 writes per frame then `partial`; published at the frame start under a mutex for other threads. `SourcesAtBeam(t)` answers in that moment's mode (`state_at`, `state_frame`, `state_partial`), a running machine from the last completed frame (`snapshot`); `State()` reads the published latches while running. Tests: `videowritelog_test.cpp`, mid-frame FF77 switch, mid-frame border, queries from another thread while running
- [ ] Phase 4 (postponed 2026-09-30, outside the TSConf scope): Qt beam widget in beam coordinates, pixel inspector, memory-viewer overlay
- [x] Phase 5, TSConf graphics layer (2026-09-30, branch `tsconf-videomap`): `TsConfVideoMapper` (`video/tsconf/`) for ZX / 16C / 256C / TXT in the four geometries, per line with the registers the engine latched; surfaces in 14 MHz pixels (the 720-wide framebuffer's unit); CRAM as 16-bit `Palette` cells. The family's state reaches the mapper through `VideoState::familyView` (`Screen::VideoFamilyView`), so shared code stays TS-free. Tests `tsconfvideomapper_test.cpp`: layout, sources, colour = rendered pixel in every mode x geometry with random RAM / CRAM / offsets, `PixelsFor` covers each source pixel (incl. the 512 scroll wrap and ZX rows 192-255), text cells, border. Every surface (`/video/*`, CLI, Lua, Python, MCP) answers through the service. Not yet: the TSU tile / sprite layers and their mixing
- [ ] Phase 5+: one mapper per new video family as it lands (PHR, Timex/GMX, TSConf TSU layers, Next, Sprinter, ZX-Poly)
