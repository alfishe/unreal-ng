# The workbench: a framework for the debugger family

- **Date:** 2026-09-28
- **Status:** proposal for review. Companion to [proposition.md](proposition.md)
  (which decides *what* we build) — this document decides *what it is built
  on*.
- **Direction set by the user (2026-09-28):**
  - the primary front-end is native (Qt, SDL or ImGui); Tauri and an SDL
    terminal emulation are the second tier; mobile companions are native
    (iOS, Android, Windows tablet);
  - the whole MVP, but modular: nothing is dumped on the user at once;
  - the minimum must not look thin, and any hardcore widget or any number of
    windows on several monitors must be addable;
  - the concept sits **between VS Code / Visual Studio and Adobe Premiere**:
    video and audio monitoring matter as much as code, for streamers,
    composers and demo makers;
  - Lua / Python plug-ins for timing and animation effects and for
    rebuild automation.

> **In one line.** A workbench shell (panels, workspaces, command palette,
> any number of windows) whose every panel is a view on the debugger
> protocol; a timeline with video, audio and event tracks as the central
> editor; monitors and scopes as first-class panels; everything extensible
> from Lua and Python.

## Contents

- [1. Concept: the IDE meets the video editor](#1-concept-the-ide-meets-the-video-editor)
- [2. The shell](#2-the-shell)
- [3. Panels, documents and workspaces](#3-panels-documents-and-workspaces)
- [4. The timeline](#4-the-timeline)
- [5. Monitors and scopes](#5-monitors-and-scopes)
- [6. Windows and monitors](#6-windows-and-monitors)
- [7. Modularity: how the MVP ships in pieces without looking thin](#7-modularity-how-the-mvp-ships-in-pieces-without-looking-thin)
- [8. Plug-ins: Lua and Python](#8-plug-ins-lua-and-python)
- [9. Toolkit choice: Qt, SDL + ImGui, or both](#9-toolkit-choice-qt-sdl--imgui-or-both)
- [10. Delivery tiers](#10-delivery-tiers)
- [11. Mobile and tablet companions](#11-mobile-and-tablet-companions)
- [12. What the framework must guarantee](#12-what-the-framework-must-guarantee)
- [13. Decisions requested](#13-decisions-requested)

---

## 1. Concept: the IDE meets the video editor

Two families of professional tools already solved our two halves:

| From the IDE (VS Code, Visual Studio) | From the video editor (Premiere, DaVinci Resolve) |
|---|---|
| activity bar, side bars, editor groups, panels | a **timeline** with tracks, a **playhead**, markers, in/out range |
| command palette: every action by name | **program monitor** (what plays now) and **source monitor** (a clip) |
| keybindings, settings, extensions | **scopes**: waveform, vectorscope, audio meters |
| debug view: variables, watch, call stack, breakpoints | **workspaces** per job: Editing, Color, Audio, Effects |
| problems, output, terminal | render queue, export presets |

A Spectrum debugger with TTD is exactly both: the machine's history **is** a
timeline (frames on a time axis, the TTD position is the playhead,
bookmarks are markers), and the machine's screen and sound **are** monitors.
Code tools sit around it. That is the product shape.

## 2. The shell

| Element | What it does |
|---|---|
| **Title and menu bar** | native menus; the classic Unreal key map as an optional profile |
| **Activity bar** | switches workspaces (Code, Wall, Beam Lab, Analyzer, Card, Time, Source, Studio) |
| **Command palette** | every action from every module and plug-in, by name, with its key; also "go to address / label / frame / bookmark" |
| **Status bar** | session state (running / paused and why), CPU, model, frame and T-state, recording, TTD position, active plug-ins |
| **Transport bar** | play / pause / frame step / step / step back / record, the same on every workspace (the Premiere transport) |
| **Notifications** | the HUD model's messages, also in the shell |
| **Settings** | one settings model; per-workspace overrides; synced to the core `FeatureManager` where relevant |

## 3. Panels, documents and workspaces

- **Panel** (a "view"): a unit of content from the widget catalog
  ([debugger model](../2026-09-28-debugger-model/widget-catalog.md)) or from a
  plug-in. It declares its data subscriptions, its actions, its minimum size
  and its **empty state**. Panels can be docked, tabbed, floated, or moved to
  another window.
- **Document** (an "editor"): something with its own navigation and history:
  disassembly at an address, a memory region, a source file, a timeline range,
  a disk image. Several documents of one kind can be open side by side.
- **Workspace**: a saved arrangement of panels and documents, with defaults
  (which CPU, which overlays, which tracks). Built-in workspaces per role;
  users save their own; plug-ins can ship workspaces.
- **Auto-presence:** a panel for a device that is not fitted does not appear
  (capabilities from the protocol). A 48K shows no GS panel; a TSConf shows
  its register board.

## 4. The timeline

The central editor of the Time and Studio workspaces, and a strip at the
bottom of every other workspace.

```text
 frame  1200     1210     1220     1230     1240     1250
        |--------|--------|--------|--------|--------|
 VIDEO  [▣][▣][▣][▣][▣][▣][▣][▣][▣][▣]  screen thumbnails, flicker marks
 BORDER ▁▁▃▃█▁▁▃▃█▁▁▃▃█                 border color per frame / per line
 AY A   ~~~∿∿∿~~~∿∿∿~~~                 per-channel waveform
 AY B   ∿~~~∿∿~~~∿∿~~~
 BEEPER ▁▁▁▂▁▁▁▂▁▁                       level
 GS     ~~∿~~∿~~∿~~                     card channels (when fitted)
 CPU    ▆▆▇▆█▆▆▇▆                       T-states used per frame, contention share
 EVENTS ·  ·· ·   ·· ·                  port writes, INT, breakpoints, loads
 CODE   [menu][ level_init ][ game_loop ...]   routine spans (from call trace)
 MARKS        ▼bug      ▼fixed                  TTD bookmarks, user markers
                    ▲ playhead = TTD position
```

- **Playhead = TTD position.** Dragging it seeks; the monitors follow.
- **Tracks are providers**: video, border, each sound channel, CPU load,
  events, code spans, memory activity, plug-in tracks. A track declares its
  data and its zoom levels (per frame, per line, per T-state).
- **Zoom** from the whole session down to T-states of one frame; at the
  deepest zoom the event and bus tracks become the Beam Lab's views.
- **Markers and ranges**: bookmarks, breakpoint hits, loads, user markers;
  an in/out range feeds export (video, audio stems, TTD clip) and run
  comparison (two ranges or two branches side by side).
- **Trimming recordings**: the same in/out range exports a trimmed,
  self-contained TTD recording (start state re-computed at the in-point,
  deltas re-packed, verified by replay):
  [use-cases-extended §5](use-cases-extended.md#5-ttd-recording-editor-and-visualizer).
- **Branches** (TTD what-if) appear as stacked lanes, like versions of a
  clip: the active lane bright, a tick where a branch leaves its parent, the
  name at the lane end, click to switch; forks to other models hang off their
  fork point with a model badge. The status line shows the position (seconds
  and frame), the distance to the branch end (does *play* replay or record?),
  frames stored and memory used. The speed box goes negative for reverse
  playback. Design: [model what-if and branched history](../2026-09-29-model-what-if/design.md) §7.

## 5. Monitors and scopes

| Panel | For | Content |
|---|---|---|
| **Program monitor** | everyone | the emulated screen, live or at the playhead; overlays from modules and plug-ins (beam, events, heat, provenance, captions) |
| **Model what-if grid** | DM, compo organizers, emulator developers | the same moment opened on several machines, clips in a synchronized grid with wipe / difference views, timing overlays and a verdict ([ttd-offline-analysis §6c](ttd-offline-analysis.md#6c-model-what-if-open-this-moment-on-other-machines)) |
| **Instant replay** | streamers, casters, referees | a queued moment (from highlight or integrity detectors, or a "clip that" hotkey) played with slow motion, freeze and overlays, then back to live; usable as a scene source ([ttd-offline-analysis §6a](ttd-offline-analysis.md#6a-live-segment-streaming-from-offline-to-near-live)) |
| **Source monitor** | RE, DM, GD | a frame from another point, a branch or a second emulator; wipe / side-by-side / difference against the program monitor |
| **Video scopes** | DM, streamers | palette usage per frame, attribute clash map, flicker map (gigascreen), per-line color changes, border timing histogram |
| **Audio meters and scopes** | MU, streamers | per-channel scopes (AY/TS/TSFM/GS/Covox/beeper), mix meter with loudness (LUFS) for streaming, spectrum, stereo placement |
| **Chip boards** | MU, HW | the Wall's device boards, live |

**Export presets** (render queue): video with or without overlays, audio
stems per channel, register dumps (PSG / YM / VGM for AY music), a TTD clip
with markers, a still with annotations.

## 6. Windows and monitors

- Any panel or document can be torn off into its own window; any window can
  hold a workspace.
- Layouts are saved **per monitor configuration** (laptop alone vs laptop +
  two screens) and restored when the configuration returns.
- A window can be marked **presentation** (no chrome, fixed layout): the Wall
  on a second screen for a stream or an exhibition.
- Several emulator instances: each window says which instance it follows; a
  source monitor can follow another instance (compare two machines live).
- **Scenes and a compositor** for shows (streams, compos, exhibitions):
  preview / program scenes over several instances, viewports, looks (CRT,
  palette, color correction, scaling filters), audio mixes, overlays and
  scripts, with smooth transitions; driven from an on-screen deck, a Stream
  Deck (including models with dials and a touch strip), phone and tablet
  companions over Wi-Fi, Bluetooth or USB, and OBS. Requirements:
  [use-cases-extended.md §4](use-cases-extended.md#4-scenes-transitions-and-control-surfaces).

## 7. Modularity: how the MVP ships in pieces without looking thin

The MVP is complete but delivered as **modules**. Each module registers panels,
documents, tracks, commands and workspaces with the shell. A module can be
enabled per workspace; the shell does not care how many exist.

| Module | Registers |
|---|---|
| `core.session` | transport, status bar, program monitor |
| `code` | disassembly, registers, stack, call stack, breakpoints, watches, navigation |
| `memory` | memory documents, search, patching |
| `wall` | device boards |
| `beam` | beam overlay, event viewer, raster stepping, bus strip |
| `analyzer` | heat, provenance, code/data map, graphics browser |
| `time` | timeline, markers, branches, run diff |
| `source` | source documents, SLD, hot reload |
| `card` | card CPU panels |
| `studio` | scopes, meters, export presets |

**Not thin with little inside** — design rules:

1. **Designed empty states**: every panel explains what it will show and
   offers the one action that fills it ("load symbols", "start recording").
2. **Few panels, large and polished** in the default workspace (screen,
   code, registers, timeline strip); more appear when needed.
3. **Live motion by default**: the program monitor, the timeline strip and
   the chip meters move while the machine runs, so even the minimal
   workspace looks alive.
4. **One visual language** (the model's tokens), so a module added later
   looks like it was always there.
5. **Progressive disclosure**: advanced panels are one command away, never in
   the default layout.

## 8. Plug-ins: Lua and Python

Both languages already exist as automation surfaces. The plug-in API adds UI
and event hooks on top of the same protocol.

| Extension point | Examples |
|---|---|
| **Commands** | "rebuild and hot reload", "export level map", "find text in any encoding" |
| **Triggers and hooks** | on frame, on line, on breakpoint, on port write, on load, on TTD seek |
| **Panels** | declarative (fields, tables, plots, meters from a description) — renders the same in every front-end; or a **2D canvas** (lines, rectangles, text, images) for custom drawing |
| **Timeline tracks** | a game's state as a track (lives, level, score), a demo part map, a music pattern track |
| **Screen overlays and effects** | captions, highlights, animated callouts synced to events (streams, tutorials), transitions between parts |
| **Importers / exporters** | symbol formats, graphics formats, music register dumps |
| **Analyzers** | a signature matcher, a sprite layout heuristic, a loader classifier |
| **Build integrations** | watch a folder, run sjasmplus / z88dk / make, parse errors into the problems panel, hot reload on success |

Rules: plug-ins run off the emulation thread except for explicitly declared
per-frame hooks with a time budget; they reach the machine only through the
protocol (so a plug-in works in every front-end and in automation); each has a
manifest (name, version, permissions: read-only, edits machine state, file
system, network); errors never stop the emulator.

## 9. Toolkit choice: Qt, SDL + ImGui, or both

What we have: the emulator application is Qt 6; the HUD is split into a
Qt-free model and a Qt presenter, with an SDL3 presenter planned; the
Unreal-monitor POC draws its text grid with SDL3; no ImGui yet.

| Criterion | **Qt 6** (+ an advanced docking library, GPU canvas widgets) | **SDL3 + Dear ImGui** (docking + multi-viewport branch) |
|---|---|---|
| Docking, floating, tabs | good with an advanced docking library (Visual-Studio-like); native windows | built in (docking branch); viewports become OS windows |
| Several monitors | native, reliable on all three OSes | works; weaker on macOS and Wayland (viewport quirks) |
| Live, dense, hardcore widgets (heat maps, event plots, scopes) | custom painting (QPainter, or GPU through QRhi) — more code per widget | immediate mode redraws every frame — **very little code**, ideal for live data |
| Polish ("not thin", Premiere-like) | native menus, text, dialogs, fonts, accessibility, HiDPI; styling via QSS and custom paint | tool look by default; can be themed well, but text input, IME, dialogs, accessibility need extra work |
| Plug-in UI from Lua / Python | declarative panels map to widgets; a canvas maps to QPainter | immediate-mode calls map naturally from scripts |
| Existing code | the whole app, debugger widgets, HUD presenter, menus | none (TUI POC uses SDL3 for drawing only) |
| Mobile | Qt runs on iOS/Android, but the companions are to be native | not a mobile UI toolkit |
| Build and licensing | large dependency, LGPL (already accepted) | tiny, MIT |

**Recommendation: Qt 6 as the primary shell, with a canvas abstraction for
heavy panels.**

- The shell (windows, docking, menus, command palette, dialogs, text,
  accessibility, several monitors) is what makes the product look finished;
  Qt does it best and we already own it.
- Heavy live panels (event plot, heat map, bus strip, scopes, timeline) are
  drawn through **one canvas interface** (2D primitives + textures). Its first
  implementation is QPainter / QRhi inside Qt. The same interface can be
  implemented on ImGui draw lists (SDL3 second tier) and on a web canvas
  (Tauri), so heavy panels are written once.
- Plug-in panels use the same two forms (declarative description, canvas),
  so a plug-in never depends on the toolkit.
- **ImGui stays an option, not a foundation:** the SDL3 tier (player,
  terminal-emulation skin, kiosk / streaming wall) can host the same panels
  through the canvas interface and ImGui for controls.

If the priority were "fastest path to many hardcore panels" over "finished
product look", SDL3 + ImGui as primary would be defensible; the canvas
abstraction keeps that door open either way.

## 10. Delivery tiers

| Tier | Front-ends | Purpose |
|---|---|---|
| **1 (primary)** | the native workbench (Qt 6) | everything; heavy views in process |
| **2** | Tauri web client; SDL3 terminal emulation (the classic Unreal monitor grid, Kozynax-style ZX-font UI); SDL3 player with HUD | remote and headless, showcases and streams, retro look, low-end machines |
| **3 (companions)** | native iOS, Android, Windows tablet apps | second screen and remote control ([§11](#11-mobile-and-tablet-companions)) |
| **Satellites** | VS Code (DeZog now, DAP later), CLI / MCP / Lua / Python automation | where coders and agents already work |

## 11. Mobile and tablet companions

Native apps over the protocol (no emulator on the device required), for a
second screen next to the keyboard or on a stage:

| Companion view | Use |
|---|---|
| **Wall** | device boards live; the spectator and streamer view |
| **Transport and timeline** | play, pause, step, scrub, markers, like a video editor's jog wheel |
| **Scopes and mixer** | per-channel audio meters and mute; composer's mixer |
| **Watches and pokes** | a trainer panel, RAM watch, quick pokes |
| **Notifications** | breakpoint hit, build finished, hot reload done |
| **Touch input** | keyboard, joystick, Kempston mouse for the emulated machine |
| **Show deck** | scene buttons with live thumbnails, T-bar, levels (the same deck as the on-screen one and the Stream Deck) |

The iOS client branch (#47, embed layer) is the natural starting point for
iOS; Android and Windows tablet follow the same protocol.

## 12. What the framework must guarantee

1. Every panel works from the protocol alone (so it can live in any tier).
2. Frame-coherent updates: all live panels show the same frame.
3. Zero cost for panels that are not visible (subscriptions end with the
   panel).
4. Layouts, workspaces and plug-in sets are files: shareable, versioned.
5. Keyboard-complete: every action has a command and a bindable key.
6. Automation parity: every module's actions are also CLI / WebAPI / MCP /
   Lua / Python calls.

## 13. Decisions requested

1. **Toolkit for tier 1:** Qt 6 with a canvas abstraction (recommended), or
   SDL3 + ImGui?
2. **Docking library** for Qt: an advanced docking system (Visual-Studio-like
   floating and tabbing across monitors), or Qt's own dock widgets?
3. **Plug-in UI forms:** declarative + canvas (recommended), or also native
   widgets for Qt-only plug-ins?
