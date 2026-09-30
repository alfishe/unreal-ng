# Video debug translation: mode-aware beam, pixel and memory mapping

Status: design, revision 2 (after independent review, see §14). Phase 0
done 2026-09-27 (§10); **phases 1-3 done 2026-09-29** (see "Phase 1 as
built", "Phase 2 as built" and "Phase 3 as built" below); phases 4-5 open. Scope: every machine and video mode unreal-ng emulates today,
plus the ones on the roadmap (TSConf, ZX Next, Sprinter, ZX-Poly, Timex, GMX).

## 1. Problem

Almost every debugging tool that answers "where is the beam?", "what is at
this pixel?" or "which pixel does this byte feed?" works out the answer
itself, with its own copy of the screen rules. Most copies assume the
Sinclair screen: 256x192 pixels, 2 pixels per T-state, bitmap at `0x4000`
in the ZX interleaved layout, attributes at `0x5800`. The renderers are
mode-aware, so the picture is right. But the debug views describe a
different screen from the one being shown, and the copies don't even agree
with each other.

Found on 2026-09-27 (ATM Turbo 2+ in hires mode), confirmed in review:

| Where | What goes wrong |
|-------|-----------------|
| Beam/zone code, written four times: WebAPI `getBeamPosition` ([state_screen_api.cpp](../../../core/automation/webapi/src/api/state_screen_api.cpp)), CLI `HandleBeam` ([cli-processor-analysis.cpp](../../../core/automation/cli/src/commands/cli-processor-analysis.cpp)), Python `beam` ([python_emulator.h](../../../core/automation/python/src/emulator/python_emulator.h)), Lua ([lua_emulator.h](../../../core/automation/lua/src/emulator/lua_emulator.h)) | Paper x is `(T - paperStart) * 2`, but ATM hires/text and Profi hires draw 4 px per T. "paper" means the ZX 256x192 window, not the mode's own window. Its line origin puts ZX paper at T 72..199 while the renderer draws it at T 24..151 (§3.1). So `paper.x` and the picture are 48 T apart **even on a 48K**. `paper.y` ignores the ATM3 AlCo timing override (8 lines off there). |
| `/state/screen/mode` (same file, ~l.180-360) | A fifth, separate set of mode facts (layouts, byte counts), not tied to the renderers. |
| Qt beam widget ([ulabeamwidget.cpp](../../../unreal-qt/src/debugger/widgets/ulabeamwidget.cpp)) | Draws the paper outline from the framebuffer layout (ATM: left offset 0, width 320) but places the beam in 448-dot beam coordinates. The marker and the outline disagree. |
| `ScreenZX::TransformTstateToFramebufferCoords` / `TransformTstateToZXCoords` ([screenzx.cpp](../../../core/src/emulator/video/zx/screenzx.cpp)), `Screen::GetPaperStartTstate` ([screen.h](../../../core/src/emulator/video/screen.h)) | x = T x 2 from line start, and ZX paper start for every mode. |
| Screen OCR ([screenocr.cpp](../../../core/src/debugger/analyzers/rom-print/screenocr.cpp)) | Matches the ROM font against `0x4000`. In ATM / TSConf text modes the characters sit as plain codes in a text page and could be read exactly. |
| Screen digest, `Screen::GetActiveSurfaceRAMPages` ([screen.cpp](../../../core/src/emulator/video/screen.cpp)) | Mode-aware, but only by page, and wrong for ATMTL: returns {1/3, 5/7}, while the renderer reads the dedicated pages 8/10. |

The same facts (pixel position, memory address, zone) exist in at least six
places with different assumptions. Nothing ties any of them to the renderer.
The ATM horizontal window was wrong in the renderer itself (T 32..192 instead
of T 8..168, fixed 2026-09-27), and no debug view could have shown it.

### Phase 1 as built (2026-09-29)

| Piece | Where | Note |
|-------|-------|------|
| Types | `core/src/emulator/video/map/videomap.h` | `std::vector` instead of `FixedVector` (debug path); one layer per family so far, so `PixelSources` holds one `LayerContribution` (composition Select); text layers are addressed in pixels, with `textColumns/textRows` for the grid |
| Interface | `video/map/videomapper.h` | `IVideoMapper::Layout / SourcesAt / BorderSources / PixelsFor / TextAt`; `MemView` reads RAM pages and the ATM font (`InternalTable`) |
| Service | `video/map/videomapservice.{h,cpp}` | stateless, one per query; reads the machine directly (paused use) - the running-machine snapshot is phase 3 |
| Family switch | `video/videofamily.h` (`FamilyOf`) | used by `ScreenZX::SelectRangeRenderer` and the service: a mode is described by the family that draws it (TS / Timex / GMX / PHR are drawn, and so described, as ZX until their renderers exist) |
| Shared tables | `video/zx/zxgeometry.h`, `video/atm/atmgeometry.h`, `video/profi/profigeometry.h` | the renderers now take their constants, address formulas, attribute decodes and palettes from these (G1); AlCo uses the ZX layout and the ATM pixel-pair packing |
| Mappers | `video/zx/zxvideomapper`, `video/alco/alcovideomapper`, `video/atm/atmvideomapper`, `video/profi/profivideomapper` | beside their renderers |
| Tests | `core/tests/emulator/video/videomapservice_test.cpp` | per mode (48K, Pentagon, P384, P16, PMC, ATM16, ATMHR, ATMTX, ATMTL, Profi HR): mapper colour == framebuffer over random memory and palettes; every memory source lists its pixel (round trip); flipping a source's bits changes that pixel as predicted and not a pixel it does not feed; window corners +-1 T / line |

Found by the tests: P384 stores its framebuffer 16 lines higher (it starts
right after vsync), now `ZxGeometry::kP384ExtraTopLines` for the renderer
and the mapper.

### Phase 2 as built (2026-09-29)

The field-writer of §6 is the existing `StateNode` report tree: the core
builds each answer once (`core/src/emulator/state/devicestatevideo.cpp`) and
every interface converts the tree, so names and values are the same
everywhere by construction.

| Question | DeviceState | WebAPI | CLI | Lua / Python | MCP |
|----------|-------------|--------|-----|--------------|-----|
| beam | `VideoBeam` | `GET /video/beam` (+ `layers[]`) | `beam` | `beam_position()` | aspect `timing` |
| layout | `VideoLayout` | `GET /video/layout` | `video layout` | `video_layout()` | aspect `video_layout` |
| pixel | `VideoPixel`, `VideoPixelAtBeam` | `GET /video/pixel?x&y[&layer]` / `?t` | `video pixel x y [layer]` / `video pixel t T` | `video_pixel(x, y[, layer])`, `video_pixel_at(t)` | `invoke_api` |
| byte -> pixels | `VideoAddress`, `VideoAddressZ80` | `GET /video/address?page&offset` / `?z80` | `video address page offset` / `video address z80 A` | `video_address(page, offset)`, `video_address_z80(a)` | `invoke_api` |
| text | `VideoText` | `GET /video/text[?layer]` | `video text [layer]` | `video_text([layer])` | aspect `video_text` |

- Pixel answers carry `state_at: "current"` and `values_at: "current"`
  until the write log (phase 3) can answer for an earlier T.
- At the beam, a border point reports `border: true` with its register and
  palette sources (the border rows count, which `in_visible_area` does not).
- Screen OCR: a text layer is read exactly from its codes
  (`ScreenOCR::textLayerScreen`); font matching stays for bitmap modes.
- Tests: `devicestatevideo_test.cpp` (field names and values per report),
  `mcp-tools-test.cpp` (the two aspects), `screenocr_test.cpp` (ATMTX text).

### Phase 3 as built (2026-09-29)

- **Write log of latches, not port writes** (`video/map/videowritelog.h`).
  After a video port write, `Screen` notes the latches (`VideoLatches`: mode,
  `#7FFD`, `#EFF7`, `#FF77`, `#DFFD`, `aFE`, `#FE`, border attribute / index,
  ATM border bright, active screen) with the frame T - from the three places
  every decoder already goes through (`InitRaster`, `SetActiveScreen`,
  `SetBorderColor`). The state at T is the last entry at or before T, else
  the frame start. No decode logic is repeated, which §4.6's "replay of the
  log" would have needed.
- **Cost:** an entry only when the latches differ from the last one; the
  common case (beeper writes to `#FE` with the same border) is a 12-byte
  compare and no T computation. 4096 entries per frame, then `partial`.
- **Frames:** current and previous; at the frame start (`Screen::InitFrame`)
  the finished frame is published under a mutex - one lock per frame
  instead of §4.9's seqlock, simpler and cheap enough at 50 Hz.
- **Answers:** `SourcesAtBeam(t)` uses the latches of that moment (labels
  `state_at: t`, `state_frame`, `state_partial`). Paused machine: the
  current frame once T has passed, else the previous frame. Running
  machine: the published last completed frame (`snapshot: true`), and
  `State()` also reads the published latches, so no query sees a
  half-changed state. `rendered_rgb` is given only when the framebuffer's
  geometry is that moment's mode (§4.6: a mid-frame switch is drawn in the
  new geometry).
- **Benchmark budget: within noise** (A/B run 2026-09-30, after the merge
  the user asked for on 2026-09-29). The hot-path addition is one 12-byte
  compare per `#FE` write. A = `fcf9448d` (the parent), B = `4e8e72e2` (the
  write log), `BM_HostFrame_*` CPU time, rounds A B A B A B B A B A at load
  7-11 (performance-guidelines §4):

  | Frame | min A µs | min B µs | B vs A, pairs | Mean |
  |:--|--:|--:|:--|--:|
  | 48K fast | 1112.4 | 1111.2 | -0.4 +0.1 -0.0 -0.2 +0.5 | -0.0 % |
  | 48K debug | 1157.7 | 1153.5 | +0.3 -0.9 -1.9 -2.2 +0.1 | -0.9 % |
  | Pentagon fast | 1498.2 | 1495.7 | +0.5 -0.1 +1.2 -0.9 -0.7 | +0.0 % |
  | Pentagon debug | 1542.9 | 1545.9 | +0.3 -0.6 +1.1 +0.2 -0.8 | +0.1 % |
  | Scorpion fast | 1718.5 | 1718.7 | +1.3 +0.0 +1.8 -0.5 +0.2 | +0.6 % |
  | Scorpion debug | 1805.2 | 1805.5 | -0.9 -0.1 +1.5 -0.9 +0.1 | -0.0 % |

  Minimums within ±0.4 %, no sign holds across the pairs: no measurable cost.
- Tests: `core/tests/emulator/video/map/videowritelog_test.cpp`;
  `videomapservice_test.cpp` - mid-frame `#FF77` switch (test 6), mid-frame
  border colour, queries from another thread while running (test 7).

## 2. Goals

- **G1. One translation, owned by the video code.** The renderer for a mode
  and the description of that mode's geometry and memory come from the same
  constants. Every debug consumer asks one service. No consumer computes
  screen geometry itself.
- **G2. Every mode, including layered and region-based ones.** This covers:
  - single-bitmap modes,
  - planar modes,
  - text modes,
  - hi-colour attribute modes,
  - layered hardware where one pixel is chosen from, or *blended* from,
    several layers (TSConf, ZX Next),
  - hardware where the mode is chosen per screen region by data in memory
    (Sprinter per-block descriptors, ZX-Poly data-driven per-cell mode).
- **G3. Four directions of question:**
  1. beam -> where (T-state to zone and pixel),
  2. beam -> fetch (which bytes the video circuit reads at this T),
  3. pixel -> sources (which bytes, bits and registers make this pixel's
     colour),
  4. memory -> pixels (which pixels this byte affects).
- **G4. Physical locations, not Z80 addresses.** Video reads physical RAM
  pages, VRAM, font tables, palette RAM, sprite RAM, and registers. The Z80
  address is reported as extra information and depends on current paging.
- **G5. Honest about time.** Say plainly which answers depend on the moment:
  register state (can change mid-frame), memory values (can change
  mid-frame), and what the renderer actually drew. Never present a guess as
  exact (§4.6).
- **G6. Same answer everywhere.** WebAPI, MCP, CLI, Python, Lua and Qt show
  the same fields with the same names and values.
- **G7. Zero cost when not debugging.** Nothing new on the per-T render path.
  Recording goes in port handlers, which are cold code.
- **G8. Checked against the renderer.** Tests prove, per mode, that the
  mapping and the renderer agree (§11).

Not goals: changing how any mode renders. Emulating modes that aren't
emulated yet. Replacing framebuffer viewport/cropping. Making the renderer
draw mixed-mode frames (it cannot today, §4.6).

## 3. Words used in this document

| Term | Meaning |
|------|---------|
| T / T-state | One tick of the base 3.5 MHz clock. Turbo does not change it: the frame is measured in base T. |
| Beam position | (line, T in line), with the origins defined in §3.1. |
| Dot | Smallest horizontal output unit. ZX: 2 dots per T (7 MHz). ATM hires/text, Profi hires, TSConf text, Next 640-wide Layer 2: 4 dots per T (14 MHz). |
| Surface | A logical picture in its own units: 256x192 px, 320x200 px, 80x25 text cells, a 512x512 sprite sheet. |
| Layer | A surface as it appears on screen: its window in the beam, scroll, clip, priority, transparency. Classic modes have one layer. |
| Region | A part of a layer with its own mode, units, palette and scroll (Sprinter 16x8 blocks, ZX-Poly cells in modes 6/7). Most layers have a single region covering everything. |
| Window | The part of the beam where a layer is visible (lines x T range). |
| Source | One contributor to a pixel: a location (memory space, page, offset, bit mask, *or* a register) and a role (pixel bits, attribute, plane, char code, font row, palette entry, tile/sprite descriptor, mode descriptor, selector...). |
| Video state | The latches and settings a mode's geometry and memory layout depend on (§4.1). |

### 3.1 Beam origin (one convention, defined once)

The code uses three different horizontal origins today:

| Consumer | T 0 of a line is... | ZX paper at |
|----------|---------------------|-------------|
| Renderer LUT, `Transform*`, ATM/Profi renderers, Qt beam widget | start of the left border | T 24..151 |
| `RasterState` horizontal zones, WebAPI/CLI/Python/Lua beam, `UlaContention` | start of horizontal blank (blank, then border, then paper) | T 72..199 |
| Obsolete `RASTER` table (TSConf-era, `screen.h`) | own convention | T 70.. |

Vertically everyone agrees: line 0 is the first vsync line, and the frame T
is the CPU's `t` within the frame.

**Convention for this design:** the *renderer's* origin. T 0 of a line is the
first T of the left border, and horizontal blank/sync is at the end of the
line (ZX: T 176..223). Frame T 0 is the renderer's T 0. The INT fires at
`intstart + 1`. This is what the picture shows, so it is the only origin
where "pixel under the beam" can be checked against the framebuffer (G8).

**Phase 0 (must come first):**
1. Settle which of the two live origins is right, using INT-relative
   hardware numbers:
   - 48K first paper pixel 14336 T after INT,
   - Pentagon 17989 T,
   - ATM 14395 T (`intstart` values are already calibrated on the renderer
     origin).
2. If contention (`UlaContention`) really is 48 T off from the picture,
   that's a core timing bug and gets its own fix and tests. If the renderer
   is off, the renderer gets fixed. Either way, afterwards there is one
   origin.
3. `/video/beam` field values that change are listed in the API changelog.

There is no "golden JSON unchanged" test, because the current values are
wrong.

**Worked example.** ATM Turbo 2+ in 16-colour mode, beam at line 100, T 50:

- *Beam -> where:* line 100 is inside the ATM window (lines 68..267), row
  100 - 68 = 32. T 50 is inside T 8..168, so x = (50 - 8) x 2 = 84 and
  x_end = 85. Zone `display`, layer `atm16`, pixel (84, 32).
- *Pixel -> sources:*
  - byte group j = 84 / 8 = 10, plane q = (84 / 2) % 4 = 2;
  - plane 2 lives at page (videoPage - 4) + 0x2000, so with 7FFD.3 = 0 that
    is RAM page 1, offset 0x2000 + 32 x 40 + 10 = 0x250A;
  - bits `b6,b2,b1,b0` for the left pixel of the pair;
  - then the palette cell with that 4-bit index.
- *Memory -> pixels:* page 1, offset 0x250A feeds pixels (84, 32) and
  (85, 32) of layer `atm16`.

The same question on a 48K at line 100, T 50 gives zone `display`, layer
`zx`, pixel ((50 - 24) x 2, 100 - 72) = (52, 28):
- pixel byte at page 5 offset `ZXLine(28) + 52/8`,
- attribute byte at page 5 offset `0x1800 + (28/8) x 32 + 52/8`.

## 4. Architecture

```
  renderer constants (constexpr tables per family) ----+
          |                                             |
          v                                             v
     renderer (hot path, unchanged)            IVideoMapper (per family, debug only)
                                                        ^
    port handlers --T-stamped writes--> VideoWriteLog   |  VideoState (derived from latches)
                                              \         |
                                               v        |
                                    VideoMapService (core, one per emulator)
                                    Layout / BeamAt / SourcesAt / PixelsFor / Text / FetchAt
                                               |  plain structs, one field-writer
             +-----------+-----------+---------+---------+---------+
             WebAPI      MCP         CLI       Python    Lua       Qt
```

### 4.1 Video state

`VideoState` is *derived*, not stored. It is assembled from the latches and
config the machine already keeps (`EmulatorState`, `CONFIG`), so TTD
restore gets it for free (§4.8). It holds everything any current mapper
depends on:

- `mode` (`DetectVideoMode` result), `mem_model`, and the timing-relevant
  config: M_P16/M_PMC use ZX48 timing on ATM3 and Pentagon timing elsewhere.
- Latches: `p7FFD`, `pEFF7`, `pDFFD`, `pFF77`/`aFF77`, `aFE` (ATM1 mode
  source), `p7EFD` (GMX), Timex `FF`.
- Border: index as latched, `atmBorderBright`, Profi inverted-border rule.
- Colour settings: `profiMonochrome`, flash phase, ULA+ state.
- Palettes are *referenced* (as `Register`/`Palette` sources), not copied.

Families that don't exist yet add their block when they land (phase 5+). No
`reserved[]`, no versioning. ZX-Poly's block is per module (4 x 7FFD plus
the #3D00 mode register), because each module pages its own screen.

### 4.2 Mapper interface

One interface per mode *family*, next to that family's renderer. It works on
a `VideoState`, never reads live registers, and never touches the
framebuffer. Memory it needs to read (sprite tables, Sprinter block
descriptors, ZX-Poly selector cells) goes through a read-only memory view
passed in, so the service controls which moment's memory is used.

```cpp
class IVideoMapper
{
public:
    virtual VideoLayout Layout(const VideoState& s, const MemView& m) const = 0;
    virtual BeamInfo    BeamAt(const VideoState& s, uint32_t tInFrame) const = 0;
    virtual PixelSources SourcesAt(const VideoState& s, const MemView& m, LayerId layer, uint32_t x, uint32_t y) const = 0;
    virtual void        PixelsFor(const VideoState& s, const MemView& m, const SourceRef& ref, std::vector<SurfaceArea>& out) const = 0;
    virtual bool        TextAt(const VideoState& s, const MemView& m, LayerId layer, uint32_t col, uint32_t row, TextCell& out) const { return false; }
    // FetchAt: deferred, §4.7
};
```

Families and where they live:

| Family | Modes | Location |
|--------|-------|----------|
| ZX | M_ZX48, M_ZX128, M_PENTAGON128K, M_SCORPION, M_PROFI (std), M_P384 | `video/zx/` |
| AlCo / BaseConf z-modes | M_P16, M_PMC, M_PHR | `video/alco/` (`ScreenAlco`) |
| ATM | M_ATM16, M_ATMHR, M_ATMTX, M_ATMTL | `video/atm/` |
| Profi hires | M_PROFIHR | `video/profi/` |
| Timex / GMX | M_TIMEX, M_GMX | future |
| TSConf | M_TSZX, M_TS16, M_TS256, M_TSTX (graphics layer); tile/sprite layers future | `video/tsconf/` |
| ZX Next | ULA, LoRes, Layer 2, tilemap, sprites | future |
| Sprinter | block-descriptor graphic/text modes | future |
| ZX-Poly | modes 0-7 over four modules | future |

**Single source of truth (G1, G8): not true yet, and phase 1 makes it
true.** Today:
- the ATM renderer keeps local constants (224 T, 24 border lines, 40
  bytes/line) inside `ScreenAtm::Draw`;
- Profi hard-codes `PAPER_START_T` and friends in `screenprofi.cpp`;
- only `ScreenAtm::SCREEN_START_T/END_T` are shared.

Phase 1 moves every family's geometry and layout constants into one
`constexpr` table per family (`atmgeometry.h`, `profigeometry.h`...) that the
renderer and the mapper both include. The renderer is not rewritten to call
the mapper; that would put it on the hot path.

### 4.3 Geometry types

```cpp
struct Rational { uint16_t num, den; };          // dots per T

struct Span { uint16_t firstT, tCount; Rational dotsPerT; };   // one horizontal segment of a line

struct LayerWindow
{
    uint16_t firstLine, lineCount;
    Span span;                                    // display span of the layer
    uint8_t lineRepeat;                           // 1; 2 for Next LoRes / doubled lines
};

struct Transparency { enum Kind : uint8_t { None, ByIndex, ByRgb } kind; uint16_t value; };

struct LayerDesc
{
    LayerId id;                                   // "zx", "atm16", "ts_t0", "next_l2", "next_sprites", "sprinter"...
    SurfaceDesc surface;                          // size + unit (pixel | text_cell) + bpp
    LayerWindow window;
    int32_t scrollX, scrollY; bool wrapX, wrapY;
    ClipRect clip;                                // Next clip windows etc.
    uint8_t priority;                             // front = 0; Next reorders via layer-priority register
    Transparency transparency;                    // Next: L2/ULA by RGB (NR 0x14), sprites/tilemap by index
    bool perPixelPriority;                        // Next L2: palette bit 15 lifts a pixel above sprites
    bool hasRegions;                              // Sprinter / ZX-Poly 6-7: see RegionAt
};

struct BorderDesc { FixedVector<Span, 3> spansOnPaperLines; FixedVector<Span, 1> spansOnBorderLines; SourceRef colour; };

struct VideoLayout
{
    uint16_t tstatesPerLine, lines, vSyncLines, vBlankLines;
    BorderDesc border;                            // Profi hires: 2 dots/T at the sides, 4 across the paper window
    FixedVector<LayerDesc, 8> layers;
    CompositionRule composition;                  // §4.5
    FramebufferMap fb;                            // beam -> renderer storage, per-segment (for Qt overlays)
};
```

**Regions.** A layer with `hasRegions` answers `RegionAt(x, y)` with the
region's mode, units (pixels or text cells), palette, scroll, and the
`ModeDescriptor` or `Selector` source that chose it. Two cases:
- **Sprinter:** a 16x8-block descriptor held in VRAM (1024-byte row stride,
  256 KB VRAM).
- **ZX-Poly modes 6/7:** the module-0 cell where INK == PAPER, or FLASH,
  picks the per-cell mode.

`SourcesAt` includes the region's descriptor as a source, so "why is this
cell text and that one graphics" is answerable.

### 4.4 Sources

```cpp
enum class Space : uint8_t {
    Ram, Rom, Vram, Palette, SpriteRam, TileRam, CopperRam,
    Register,        // colour or mode that comes from a port latch (FE border, Timex FF, Next fallback colour)
    InternalTable    // emulator-internal data with no machine address (ATM font table in atmfont.h)
};

enum class SourceRole : uint8_t {
    PixelBits, Attribute, Plane, CharCode, CharAttr, FontRow,
    TileDescriptor, TileGraphic, SpriteDescriptor, SpriteGraphic,
    PaletteEntry, Border, ModeDescriptor, Selector
};

struct SourceRef
{
    Space space;
    uint8_t module;          // ZX-Poly module 0..3
    uint16_t page;           // page in that space; register number for Space::Register
    uint32_t offset;         // byte offset in the page (32 bits: Next/Sprinter VRAM)
    uint8_t width;           // 1 or 2 bytes (TSConf CRAM and Next palette entries are 16-bit)
    uint16_t bitMask;        // bits used by this pixel
    SourceRole role;
};

struct LayerContribution
{
    LayerId layer;
    FixedVector<SourceRef, 10> sources;           // ZX: pixel + attribute + (ULA+) palette; TS text: char + attr + font row + CRAM
    uint16_t colourIndex;                         // index before palette, value as of `values_at` (§4.6)
    uint32_t rgb;                                 // after palette
    bool transparent, clipped, offSurface;
};

struct PixelSources
{
    FixedVector<LayerContribution, 8> layers;     // front-to-back, every layer covering the point
    Composition result;                           // how the final colour was formed, §4.5
    uint32_t finalRgb;
    uint32_t renderedRgb; bool renderedKnown;     // what the framebuffer actually holds, when readable
};
```

The service adds Z80 aliases (`z80: [0x4000]`, or empty when unmapped) using
current paging.

### 4.5 Composition

Most modes: one layer, `Composition{op: Select, inputs: [0]}`.

Layered hardware:
- **TSConf:** fixed order S0, T0, S1, T1, S2 over graphics, index 0
  transparent: `Select` of the first non-transparent layer.
- **ZX Next:** layer order from the priority register, with these rules:
  - Transparency is by RGB for ULA/Layer 2 and by index for sprites/tilemap.
  - Layer 2 per-pixel priority comes from palette bit 15.
  - Tilemap per-tile "ULA over tilemap" bit.
  - Priority modes 6/7 *add* or *subtract* Layer 2 and ULA/tilemap colours.
  - Stencil mode ANDs ULA with tilemap.
- **ZX-Poly:** four module planes combined into one 4-bit index (modes 4-7).

```cpp
struct Composition { enum Op : uint8_t { Select, Add, Subtract, And, Combine4 } op; FixedVector<uint8_t, 4> inputs; };
```

The result says what is visible, what is behind it, why hidden layers lost,
and, for blends, which inputs made the colour.

**Sprites** are not a grid. `SurfaceArea` carries a transform (scale x1..x8,
rotate, mirror X/Y, anchor + relative position for Next relative sprites) so
`PixelsFor(sprite graphic byte)` can return every on-screen area that sprite
currently covers. Sprite attributes are read from sprite RAM / SFILE through
`MemView`, not from `VideoState`. TSConf walks all 256 SFILE descriptors (85
is only the per-line bandwidth limit), Next all 128 sprites.

### 4.6 Time: what can and cannot be answered exactly

Facts (verified in review):
- Video mode changes take effect **immediately at the port write**. The port
  handler calls `InitRaster` (`portdecoder_atm710.cpp` FF77,
  `portdecoder_pentagon1024.cpp`, `portdecoder_profi.cpp`), and not at line
  start. `VideoControl::mode_next` exists but is never used.
- `SetVideoMode` reallocates the framebuffer, so **the renderer cannot draw a
  frame with two geometries.** A mid-frame FF77 switch is drawn as if the new
  mode had been active all along, from that point on, into the new buffer.
- With ScreenHQ off, the whole frame is rendered at frame end with the final
  state.
- The renderer catches up once per instruction, so render boundaries are
  exact only to within one instruction.

Therefore the service answers with explicit labels:

| Answer | Label | Meaning |
|--------|-------|---------|
| Geometry and addresses | `state_at: <t>` | Computed from the video state in force at T `t` (from the write log below). Exact, because addresses depend only on the latches. |
| Values (colour index, RGB) | `values_at: "current"` | Memory and palette as they are *now*. Multicolor, #FF palette changes and CRAM writes mid-frame make these differ from what was on screen at `t`. |
| Rendered pixel | `rendered_rgb` | What the framebuffer holds, which is ground truth for what was drawn. Only when the framebuffer matches the layout (same geometry, ScreenHQ on or frame complete). |

**VideoWriteLog.** Port handlers that change video state append
`(frameT, register, value)` to a per-frame log (current + previous frame, a
few hundred entries at most, dropped to `state_at: "partial"` when full).
This is cold code, not the render path. `VideoState` at any T of the current
or previous frame = state at frame start + replay of the log entries up to T.

Older frames: TTD seek (§4.8).

Planned extension, not in phase 1: a per-line colour-memory snapshot for
multicolor debugging. The cost has to be measured first.

### 4.7 Fetch (beam -> bytes read) — deferred

`FetchAt` must agree with contention and floating-bus emulation, and that
model lives in `UlaContention`. Duplicating it in the mapper would create a
seventh copy of the facts. So `FetchAt` is deferred until phase 0 settles
the beam origin. Then it is built *on* `UlaContention` for the ZX family, and
added per family only where a fetch model exists.

### 4.8 TTD

TTD checkpoints sit at frame boundaries, and an intra-frame seek replays
execution. `VideoState` derives from latches that TTD already restores, and
`VideoWriteLog` is rebuilt by the replay because it's written by the port
handlers the replay runs. So nothing is added to TTD checkpoints, and no
format version bump is needed.

### 4.9 Threading

WebAPI/MCP/CLI handlers run on their own threads and today read `cpu->t` and
`_rasterState` live, without synchronisation. The service fixes that:

- **Paused machine:** queries read state directly. This is the normal
  debugging case.
- **Running machine:** the emulator thread publishes a
  `VideoSnapshot{VideoState at frame start, VideoWriteLog, frame T,
  framebuffer generation}` at frame end, double-buffered with a seqlock. The
  query thread reads the last published snapshot. Answers carry
  `snapshot_frame` and are one frame behind by design.
- Cached layouts are keyed by the immutable `VideoState` value, so a cache
  hit can't belong to a different state.

### 4.10 VideoMapService queries

| Call | Answers |
|------|---------|
| `Layout(t?)` | frame geometry, border spans, layers (window, scroll, clip, priority, transparency), composition rule |
| `BeamAt(t)` | line, T in line, dot, `vertical_zone` (vsync, vblank, top_border, display_rows, bottom_border, beyond_raster), `horizontal_zone` (left_border, display, right_border, hblank), combined `zone`, and per covering layer: id, surface x / x_end / y, region |
| `SourcesAt(layer, x, y)`, `SourcesAtBeam(t)` | `PixelSources` |
| `PixelsFor(ref)`, `PixelsForZ80(addr)` | surface areas per layer |
| `Text(layer)` | text grid (codes, attributes, font location) |

## 5. Mode catalogue

Beam coordinates use the origin in §3.1. 48K / Scorpion / Profi std / ATM:
224 T x 312 lines, paper lines 72..263 (8 vsync + 16 vblank + 48), T
24..151. 128K / +2 / +3: 228 T x 311 lines, paper from line 71 (8 + 15 +
48). Pentagon: 224 T x 320 lines, paper from line 80 (16 + 16 + 48). The
implementation takes all of these from the family tables, not from this
document.

| Machine | Mode | Surface(s) | Window | Dots/T | Sources per pixel | Status today |
|---------|------|-----------|--------|--------|-------------------|--------------|
| 48K / 128K / +3 / Pentagon / Scorpion / Profi std | ZX | 256x192 px | ZX paper | 2 | pixel byte (ZX interleave) + attribute; page 5 or 7 (7FFD.3); ULA+ palette when on | rendered |
| Pentagon | P384 overscan | 256x192 + wider border | ZX paper; border to 384x304 | 2 | as ZX | rendered |
| Pentagon 1024 / ZX-Evo | P16 (AlCo 16c) | 256x192 px | ZX paper | 2 | 4 planes: pages {vp^1, vp} x {+0, +0x2000}, one byte per pixel pair; #FF palette | rendered |
| Pentagon 1024 / ZX-Evo | PMC (hw multicolor) | 256x192 px | ZX paper | 2 | bitmap + 8x1 attribute. **Sources disagree**: renderer reads both from the same vp byte, BaseConf RTL puts the attribute at vp + 0x2000, `/state/screen/mode` says 768 attribute bytes. The mapper follows whatever the renderer is fixed to. | rendered (suspect) |
| Pentagon 1024 | PHR (512x192) | 512x192 px | ZX paper | 4 | two pixel planes | detected, drawn as ZX (stub) |
| ATM Turbo 2+ / ATM3 | ATM16 | 320x200 px | lines 68..267, T 8..168 | 2 | 4 planes {vp-4, vp} x {+0, +0x2000}, linear 40 B/line; #FF palette | rendered |
| ATM Turbo 2+ / ATM3 | ATMHR | 640x200 px | same | 4 | pixel + attribute bytes, even/odd groups from +0 / +0x2000 | rendered |
| ATM Turbo 2+ | ATMTX | 80x25 cells | same | 4 | char code + attribute + font row (`InternalTable`) | rendered |
| ATM3 / ZX-Evo | ATMTL | 80x25 cells | same | 4 | char + attribute in linear 64-byte rows on dedicated **page 8 (vp=5) / page 10**; font as above | rendered |
| Profi | PROFIHR | 512x240 px | lines 48..287, T 24..151 | 4 (border 2 at sides) | pixel page 4/6 (first byte of each 16-px cell at +0x2000) + attribute page 0x38/0x3A same offset; 16-entry palette, monochrome option (`Register`) | rendered |
| Timex / GMX | hi-colour 8x1, 512x192 hires, GMX 320x200 | per mode | per mode | 2/4 | pixel + attribute planes; hires ink/paper from port FF (`Register`); GMX via 7EFD | not implemented |
| TSConf | ZX / 16c / 256c | 256x192, 320x200, 320x240, 360x288 windows over a 512-wide scrolled surface | active origins (dots,lines): 256x192 @ (140,80), 320x200 @ (108,76), 320x240 @ (108,56), 360x288 @ (88,32); converted to §3.1 origin in the TSConf table | 4 (surface in 14 MHz pixels, 2 per dot, as the 720-wide framebuffer) | 16c: 1 byte / 2 px; 256c: 1 byte / px from VPage + GX/GY offsets; CRAM (16-bit) | mapped (`TsConfVideoMapper`, per line with the latched registers) |
| TSConf | text | cells | same | **4** | 128 B chars + 128 B attrs per row at VPage, font at VPage^1 | mapped |
| TSConf | tile layers T0, T1 | 64x64 tiles of 8x8, 512x512, scrolled | display window | 2 | tile descriptor (T_MAP_PAGE) + graphic (T0/T1_G_PAGE) + CRAM | future |
| TSConf | sprites S0..S2 | from 512x512 sheet | display window, 256 descriptors | 2 | SFILE descriptor + graphic (SG_PAGE) + CRAM | future |
| ZX Next | ULA, LoRes, Layer 2 (256x192 / 320x256 / 640x256), tilemap (40x32 / 80x32, text mode, 512 tiles), sprites (anchors, relative, scale, rotate) | per layer | per layer + clip windows | 2 (up to 4 for 640-wide L2) | per layer; palettes 9-bit; blend/stencil composition | future |
| Sprinter | per-16x8-block modes (graphic 256c/16c, text), per-block palette and scroll | regions | per mode | per mode | `ModeDescriptor` in VRAM (256 KB, 1024 B row stride) + data in VRAM | future |
| ZX-Poly | modes 0-7; mode 5 = 512x384; modes 6/7 choose per cell from module-0 data | 256x192 or 512x384; regions in 6/7 | ZX paper | 2 (4 in mode 5) | same offset in up to 4 modules' RAM (`module` 0..3), per-module 7FFD; `Selector` for 6/7 | future |

Each future row is checked against the types in §4: every field it needs
exists (regions, composition ops, transparency kinds, 16-bit sources,
`Register` / `InternalTable` spaces, per-module latches, sprite transforms).
Family-specific `VideoState` blocks are added when the family lands.

## 6. Front-end surface

All front ends use one core field-writer (`VideoMapFields::Visit(result,
visitor)`) with JSON (WebAPI/MCP), text (CLI) and dict/table (Python/Lua)
visitors, so field names can't drift (G6).

**WebAPI / MCP** (the existing endpoint is `/state/screen`, not `/screen`):

| Endpoint | Change |
|----------|--------|
| `GET /video/beam` | Kept: `tstate`, `line`, `dot_in_line`, `beam_x/y`, `zone`, `vertical_zone`, `horizontal_zone` (incl. `top_border`/`bottom_border`), `in_visible_area`, `in_paper`, `paper{}`, `raster{}`, `frame_timing`. Values move to the §3.1 origin (changelog). `paper{}` stays for ZX layers; new `layers[]` (id, x, x_end, y, region), `state_at`, `snapshot_frame`. |
| `GET /video/layout` (new) | `VideoLayout` |
| `GET /video/pixel?x=&y=&layer=` / `?t=` (new) | `PixelSources` with Z80 aliases, time labels |
| `GET /video/address?space=&page=&offset=` / `?z80=` (new) | `PixelsFor` |
| `GET /video/text?layer=` (new) | text grid |
| `GET /state/screen`, `/state/screen/mode` | rebuilt from `Layout` (replaces the literal "standard" and the hand-kept mode facts; closes PLAN #3) |
| `inspect_state` aspects `timing`, `video` | built from the same structs; new aspect `video_map` |

**CLI / Python / Lua:** `beam` keeps its fields and adds layer lines. New
`video layout|pixel|address|text` commands mirror the endpoints.

**Qt debugger:**
- Beam widget: frame drawn in beam coordinates from `Layout` (every layer
  window, border spans, blanking), beam from `BeamAt`. No framebuffer
  offsets.
- Pixel inspector (new): click the screen view, then `SourcesAtBeam` shows
  every source (space/page/offset/Z80 address/bits/role), colour index, RGB,
  rendered RGB and composition, with a jump to the memory viewer.
- Memory-viewer overlay (later phase): bytes feeding the picture; hover shows
  the pixels they feed.

**Removed:** the four beam copies, `TransformTstateToZXCoords`,
`GetPaperStartTstate`, and the hand-kept facts in `/state/screen/mode`.
`TransformTstateToFramebufferCoords` and `GetActiveSurfaceRAMPages` are
re-implemented on the service, which also fixes the ATMTL pages.

**Screen OCR:** exact `Text(layer)` for text layers; font matching only for
bitmap layers, reading the bitmap through `SourcesAt`.

## 7. Performance

- Mappers run only when queried; they are not on the render path.
- `VideoWriteLog` append happens only in port handlers that already call
  `InitRaster` or change video latches (cold). Check: `core-benchmarks`
  `Frame*` before/after, budget below noise.
- Snapshot publish at frame end copies at most a few KB. Same benchmark
  budget.
- `PixelsFor` is a formula per family. Sprites walk the sprite table (Next
  128, TSConf 256 descriptors).

## 8. Edge cases

- **One T covers 2 or 4 pixels:** `BeamAt` returns `x` and `x_end`.
- **Border colour:** `Border` source plus the palette cell (ATM / Profi /
  AlCo / TSConf / Next), or `Register` (FE) on plain ZX. Profi's inverted
  border index is part of its family table.
- **Palette:** palette-based modes add the palette entry (`width` 2 where
  16-bit) as the last source, so "why this colour" is fully answered.
- **Flash / ULA+ / Timex hi-colour:** included in `colourIndex`, with the
  flash phase from the state.
- **Surfaces bigger than the window** (TSConf 512-wide, Next L2 offsets,
  sprite sheets): addressable by surface coordinates, `offSurface`/`clipped`
  tell whether they're visible.
- **Turbo:** beam values are base T. `BeamAt` also returns the CPU clock
  multiplier.
- **Mid-frame mode switch:** addresses exact via the write log; rendered
  picture follows §4.6 (drawn in the new geometry from the switch onwards).
  The answer says both.
- **Model without a mapper:** `NullMapper` returns frame geometry and
  `layers: []`, `mapped: false`. Never ZX rules for a non-ZX mode.

## 9. Relation to existing geometry

`RasterDescriptor` stays for framebuffer allocation and the ZX LUT. Family
tables (§4.2) feed both descriptors and mappers. The overloaded ATM
`screenOffsetLeft = 0` (storage) versus the T 8..168 window (beam) becomes two
fields: `window` and `fb`. `RasterState`'s horizontal zones move to the §3.1
origin in phase 0, or are replaced by `Layout`.

## 10. Side issues found during the design (fixed 2026-09-27)

- **Beam origin (phase 0 done).** `RasterState` horizontal zones moved to the
  renderer origin. Contention, floating bus, the four beam copies (now one
  `Screen::DescribeBeam`) and the Qt beam widget follow it. INT-to-first-pixel
  recalibrated per model by reference consensus (48K 14340, 128K/+3 14366,
  Scorpion 14336); contention onset kept at the classic INT+14335 / INT+14361.
- **128K picture ~205 T off the INT** (doc 18 follow-up): fixed by the same
  recalibration.
- **Sinclair contention on clones:** contention/Ferranti fetch were keyed on
  the video mode, so ATM in its ZX mode (`M_ZX48`) and ATM hires modes got
  Sinclair ULA contention. Now keyed on the machine.
- **PMC attribute fetch:** now pixel address + 0x2000 (BaseConf RTL,
  UnrealSpeccy `draw_pmc`); `/state/screen/mode` byte counts corrected.
- **Digest pages:** ATMTL uses page 8/10, PMC only the video page.
- **ATM3 `paper.y`** and 4 px/T `paper.x` in `/video/beam`: fixed via
  `DescribeBeam` (timing descriptor + mode pixel clock).
- **`TransformTstateToFramebufferCoords` / `GetPaperStartTstate`** now follow
  the mode's window and storage (ATM, Profi hires).
- Test-only: `DiskAutostart_Models_Test` sampled every 5 frames and could step
  over the 3-4 frame window in which the boot sits at PROG.

- **Pentagon `intstart`:** 71635 lands at 17988 T, not the 17989 T its comments
  claimed. 71634 (17989 T) was tried and breaks *Across the Edge*, so 71635
  stays; comments now state 17988 T as the demo-verified value.

## 11. Testing

1. **Renderer agreement, per mode (main test, G8).** For sampled pixels of
   every layer: flip exactly the bits `SourcesAt` names, render, check that
   pixel changed and its neighbours didn't. Reverse direction: flip one byte,
   compare rendered changes with `PixelsFor`. Uses the existing
   `ScreenZXCUT`/ATM fixtures. Sampled, to stay within the 50 ms/test budget.
2. **Round trip.** Every pixel of every surface: each source from
   `SourcesAt(x,y)` lists (x,y) in `PixelsFor(source)`. For sprites, only
   placed sprites (an unplaced sheet pixel has no screen position).
3. **Beam geometry.** Corners of each window ±1 T / ±1 line, values from
   family references (RTL for ATM/TSConf, Profi sources).
4. **Origin (phase 0).** INT-relative distances: 48K 14336, Pentagon 17989,
   ATM 14395 from INT to first paper pixel, checked through `BeamAt`.
   Contention onset checked against the same origin.
5. **Front-end parity.** WebAPI, CLI, Python, Lua `beam` and `pixel` for the
   same state compared field by field.
6. **Write log.** FF77 switch at line 150: `SourcesAt(t at line 100)` uses
   the old mode's addresses, `t at line 200` the new; `rendered_rgb` for line
   100 reports the new-geometry buffer honestly (§4.6).
7. **Threading.** Query loop from a second thread while running; no torn
   snapshot (TSan build in CI where available).

## 12. Phases

| Phase | Content | Done when |
|-------|---------|-----------|
| 0 | Settle the beam origin (§3.1); fix whichever of contention / renderer is off; changelog entry. | test 4 |
| 1 | Family constant tables shared with renderers (ATM, Profi, AlCo, ZX). Types, `IVideoMapper`, `VideoMapService`, `VideoState` derivation, `NullMapper`. Mappers: ZX, AlCo, ATM, Profi HR. | tests 1-3 for these modes |
| 2 | Front ends through one field-writer: `/video/beam` (extended), `layout`/`pixel`/`address`/`text`, `/state/screen*` rebuilt, MCP aspect, CLI/Python/Lua; remove the four copies; OCR text path. | test 5 |
| 3 | `VideoWriteLog` + snapshot publishing + time labels. | tests 6-7, benchmark budget |
| 4 | Qt: beam widget in beam coordinates, pixel inspector. Memory-viewer overlay after. | manual check on 48K / Pentagon / ATM16 / ATMHR / ATMTX / Profi HR with screenshots |
| 5+ | Each new family (PHR, Timex/GMX, TSConf, Next, Sprinter, ZX-Poly) lands with its mapper, `VideoState` block and tests 1-3. `FetchAt` per family where a fetch model exists. | per family |

## 13. Open questions

1. `zone: "paper"` for ZX layers: keep as alias for one release, or rename to
   `display` at once together with the phase-0 value change? Proposal:
   rename at once, since values change anyway, with one changelog entry.
2. Per-line colour-memory snapshots for multicolor debugging (§4.6): worth
   the cost? Decide after phase 3 measurements.
3. Qt pixel inspector: its own dock, or inside the debug visualization
   window?

## 14. Review record

Independent review on 2026-09-27 (fresh agent, code- and reference-checked).
Changes from revision 1:

| Finding | Severity | Resolution |
|---------|----------|------------|
| Three horizontal beam origins in the code; rev 1 used one silently, and its golden-JSON test would have locked in the wrong one | blocker | §3.1 convention + phase 0 + test 4; golden test dropped |
| Future platforms not representable: Next blend/stencil, RGB vs index transparency, L2 per-pixel priority, tile/sprite transforms; Sprinter per-block descriptors in VRAM; ZX-Poly per-module 7FFD, 512x384 mode 5, data-selected cells; colours from registers; 16-bit palette entries; missing latches/config in `VideoState` | blocker | §4.3 regions, spans, transparency kinds; §4.4 `Register`/`InternalTable`, 16-bit sources, new roles; §4.5 composition ops + sprite transforms; §4.1 derived state with the missing latches; catalogue rows rewritten |
| Catalogue errors: 128K geometry, PMC attribute, TSConf text 4 dots/T, TSConf 256 SFILE descriptors, Next max 4 dots/T, ATMTL pages 8/10 | major | §5 corrected; PMC flagged in §10 |
| Problem table: digest wrong for ATMTL, `/state/screen/mode` is another copy, ATM3 `paper.y` | major | §1 corrected |
| Per-line log premises false: `mode_next` unused, mode changes at port write, framebuffer realloc, ScreenHQ-off frame-end render | major | §4.6 rewritten: write log at port handlers, explicit time labels, renderer limitation stated; test 6 rewritten |
| Mid-frame memory changes not covered by register log | major | `values_at: "current"` + `rendered_rgb` (§4.4, §4.6) |
| Single source of truth not true today | major | §4.2 states it; phase 1 creates shared family tables |
| `FetchAt` would duplicate `UlaContention` | major | deferred, built on `UlaContention` (§4.7) |
| Threading not addressed | major | §4.9 paused/direct, running/seqlock snapshot, state-keyed cache; test 7 |
| TTD checkpoint storage unnecessary | major | §4.8: derived state, log rebuilt by replay; no format change |
| More beam fields to keep; endpoint name `/state/screen` | minor | §6 |
| ATM font is an internal table | minor | `Space::InternalTable` |
| Profi hires border has mixed dots/T | minor | `BorderDesc` spans |
| Render catch-up per instruction | minor | stated in §4.6 |
| Round trip fails for unplaced sprite pixels | minor | test 2 limited to placed sprites |
| Over-engineering: TS/Next/Sprinter blocks now, TTD versioning, `FetchAt`, "why hidden" before Next exists, memory overlay | defer | family blocks and `FetchAt` moved to phase 5+; overlay after phase 4; composition kept in the types (cheap) but only Select is implemented before layered families land |
