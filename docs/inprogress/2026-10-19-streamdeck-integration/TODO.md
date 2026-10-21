# Stream Deck integration: TODO

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |

## Phases

Depends on the control-surfaces phases ([control surfaces TODO](../2026-10-18-control-surfaces/TODO.md)): S1 core
`ActionRegistry` + RunControl + `/controllers/actions` - **done 2026-10-20** (worktree `scratch/wt-control-surfaces-s1`,
not landed yet): the registry with one worker per instance and the gates of every origin, `RunControl`, REST, MCP
`actions`, CLI `action`, Lua / Python `action()`, and `/api/v1/ws` with calls (`call`, `auth`; topics come with S4);
S2 `ContextState`, TTD published cursor, `seek_by`, `step_by` - **done 2026-10-20** (worktree
`scratch/wt-control-surfaces-s2`, not landed yet): `ctx.set` and `GET /api/v1/surfaces/context` on every plane, the
cursor behind the `position` verb (a 50 Hz poll never parks the machine). The boundary with SD4: S2 keys the context
by instance and did not build `TargetState` (not needed for it); SD4 keeps `TargetState`, view registration, the
`target` op and `GET /api/v1/surfaces/views` / `sessions` (the `/api/v1/surfaces/` prefix now has `context`); S4 the
surface protocol, served as the `surface` topic of `/api/v1/ws` (D7).

| # | Phase | Needs | State |
|:--|:--|:--|:--|
| SD0 | Owner decisions: layout and names (L-1 … L-6), targeting (T-1, T-2), Q-1 … Q-3 | | done 2026-10-19 |
| SD1 | Import the library, sdctl, the daemon (`run`, `pair`, `scenario`) and the tests into `companions/streamdeck/` (`UNREAL_STREAMDECK=OFF` by default), the project's style and build settings, hidapi + json embedded, docs `docs/companions/streamdeck/` (HID reference, performance), notices; 838 checks green | SD0 | done 2026-10-20 (turbojpeg still from the system: SD2) |
| SD2 | JPEG POC + vendored encoder (removes the system turbojpeg and its link warning), WebSocket client on one non-blocking event loop (NFR-8; the automation's cpp-httplib pool pinned to 1 until it moves onto the loop); zero warnings on gcc / MSVC / MinGW | SD1 | done 2026-10-20: `sdb::net::EventLoop` (poll / WSAPoll, timers, posts) + `sdb::ws::Client` (RFC 6455, reconnect with backoff, pings, bounded send queue), `sdctl ws`; checked live on `/api/v1/companions/events` and `/api/v1/websocket`, one thread. JPEG ([POC 032](../../../tools/poc/032-streamdeck-jpeg-encoder/README.md)): own encoder `sdb::jpeg` (turbojpeg's output, 1.37x its time), turbojpeg removed, the link warning gone; gcc 16 / MSVC / MinGW compile clean, full Linux / Windows companion builds: SD8 |
| SD3 | Device multiplexer, wire v2, the studio imported as a client (no USB), late-join replay; sdctl `emu-*` commands follow wire v2 | SD2 | daemon side done 2026-10-20: `sdb/devicewire.h`, `PictureCache` (replay), `DeckHub` (mirrors, own decks, bounded queues, join rules), `run --viewers`, `sdctl viewer`; reference [docs/companions/streamdeck/daemon.md](../../companions/streamdeck/daemon.md). Studio done 2026-10-20: `unreal-streamdeck-studio` (`companions/streamdeck/studio/`), wire v2 client (mirror / own, `Welcome` errors, replay, `DeckState`, own-role feature replies, reconnect with backoff, `Leave`), v1 server only behind `--listen`, no USB, editor + recorder + automation imported, Run through the daemon; `streamdeck-studio-tests`; reference [docs/companions/streamdeck/studio.md](../../companions/streamdeck/studio.md). Rest done 2026-10-20: daemon routes (REST + MCP, `sdb/daemondecks.h`): `/health`, `/decks`, brightness and scenario per deck (`PUT` / `DELETE /decks/{serial}/scenario`, watched), `/decks/{serial}/snapshot` (`Control`), the runtime routes per deck (`?deck=`); `run` without a scenario (idle) and `--wait` (no deck yet: own decks work, the first deck that appears is taken); the studio starts a daemon on launch when none answers, Brightness in the toolbar, Run on the deck it shows, quits cleanly on SIGTERM (`Leave`); `sdctl decks`, `emu-snapshot --api` over the daemon (wire v2), `emu-model` / `emu-record` / plain `emu-snapshot` stay v1 for `--listen` (why: [daemon.md](../../companions/streamdeck/daemon.md#sdctl-emu--and-the-device-wire)); hidapi split into `sdb-hid` (the studio does not link it). Checked live on the Stream Deck + XL. Hot plug done 2026-10-20: an unplug (a failed read / write) closes the deck, the runtime and the viewers stay (`DeckState`, `connected` in `/api/v1/decks`), the device thread's own read rescans every second and replays the picture cache to the replugged deck (`sdb/reconnect.h`, no new thread); another deck after 3 s gets a runtime of its own, mirrors of "the first deck" follow it; `POST /api/v1/decks/{serial}/reconnect` for the same path on purpose ([hot plug](../../companions/streamdeck/daemon.md#hot-plug)) |
| SD4 | Core: `TargetState` + view registration in unreal-qt and the video wall; `target`, `activity`, readout vocabulary, catalogue fields | S1, S2, S4 | open |
| SD5 | Daemon surface client, `action` kind, state-bound faces, contexts on menus, activity; default scenario v2 + its generator | SD3, SD4 | open |
| SD6 | Registry actions for the scenario's REST groups ([migration.md §4](migration.md#4-rest-paths-to-action-ids)), about 40 | S1 | done 2026-10-20 (not landed): 46 core actions in `core/src/emulator/actions/hostactions.cpp` (run, machine, input, BASIC, features and settings, audio, media and snapshots, video, debugger, time travel), each with the companion scope of its route; `RunControl::Steps` / `RunFrames` / `RunTStates`; the catalogue's `params` field on every plane; table in [control surfaces actions.md §1.1](../2026-10-18-control-surfaces/actions.md#11-scenario-actions-sd6). Left to REST: skip_until, coverage, profiler, pchist, ROM protect, GS reset, video record, instance start / stop. `ttd.seek_by`, `run.step_by`, `ctx.set`: S2 |
| SD7 | Supervision in unreal-qt, settings page, pairing from the deck and the studio | SD5 | open |
| SD8 | Windows and Linux ports, udev rule, packaging on all three | SD5 | ports done 2026-10-20: the daemon, sdctl, the studio and both test binaries build with zero warnings and pass (1011 + 94 checks) on Linux gcc 14 (the CI image, `docker/linux/build.sh --streamdeck --test`), MSVC 2019 and MinGW gcc 15, and on macOS; hidraw backend + `companions/streamdeck/linux/70-unreal-streamdeck.rules`, the DEB installs the daemon, the studio and the rule. Fixes on the way: a use-after-free on reload (the navigator read the replaced scenario), the studio's Leave lost to a reset on Windows, `shell` / `open` on Windows (cmd.exe, output, timeout, job object). Packaging done 2026-10-20 ([packaging.md](packaging.md) sections 3-5): the release workflow builds with `-DUNREAL_STREAMDECK=ON`; macOS DMG: the daemon a helper in `UnrealNG.app/Contents/MacOS`, the studio nested as `Contents/Applications/Stream Deck Studio.app` on UnrealNG.app's Qt (macdeployqt `-executable=`, `cmake/MacOSNestedRPath.cmake` as a guard, the plugins added from the real path in `main.cpp`; the packaged ad-hoc-signed helper opened a Stream Deck + XL), signed inside-out, a relative link at the DMG root; Windows ZIP: both exes beside `unreal-qt.exe`, windeployqt on the studio; Linux tarball and AppImage: both binaries, the udev rule (`lib/udev/rules.d`, `usr/lib/udev/rules.d`); `unreal-streamdeck --help` (exit 0); release checks per platform (layout, signature, both `--help` from the package). Open: `data/streamdeck/` in the packages (waits on SD5), the companion's debug symbols in the symbol archives, the AppImage rule text in the settings page (SD7), the parent watch on Windows / Linux, a hardware run on Windows and Linux (SD9) |
| SD9 | Conformance suite in CI, hardware acceptance ACC-1 … ACC-7 | SD7, SD8 | CI and the hardware-free conformance done 2026-10-20: `cmake-ci.yml` builds the 5 companion targets and runs both test binaries on Linux (CI image), macOS, Windows MSVC and MinGW, plus the Qt-free targets with gcc 16 ([testing.md §6](testing.md#6-cross-platform)); the device wire v2 / daemon route conformance suite against the real `DeckHub` and `DaemonDecks` with fake decks (`tests/conformance_test.cpp`, coverage table in [testing.md §3.1](testing.md#31-device-wire-and-daemon-routes-hardware-free-in-ci)). Open: the host-side conformance ([§3.2](testing.md#32-with-a-host-needs-sd7), needs SD7) and the hardware acceptance ACC-1 … ACC-7 |
| SD10 | Move the plugin and utility entries of `tools/` into `plugins/` and `utilities/` (one mechanical commit) | SD0 | done 2026-10-20: `tools/{wireshark,imhex,idapro}` -> `plugins/`, `tools/{diskconverter,diskinfo,porttrace,python,workload-monitor,neogs}` -> `utilities/` (`git mv`), references in code comments, tests, CI path filters, docs and recipes updated; group READMEs |

## Owner questions

Asked one at a time.

| # | Question | Proposal |
|:--|:--|:--|
| L-1 | Group names | **Decided 2026-10-19:** clients at the root for now; `companions/`, `plugins/`, `utilities/` |
| L-2 | Move the clients | **Decided:** later, all at once, one centralised change |
| L-3 | Names of the daemon and the studio | **Decided 2026-10-19:** `unreal-streamdeck`, "Stream Deck Studio" (`unreal-streamdeck-studio`) |
| L-4 | `libsdb` naming style | **Decided 2026-10-19:** the project's style on import |
| L-5 | Import | **Decided 2026-10-19:** as new code, no history, minimal set ([migration.md §1](migration.md#1-manifest)) |
| L-6 | JPEG encoder | **Decided 2026-10-19:** small vendored baseline 4:4:4 encoder after a measurement against turbojpeg; libjpeg-turbo vendored if much worse. **Measured 2026-10-20 (POC 032): own encoder `sdb::jpeg`** (TooJpeg + libjpeg's integer arithmetic): turbojpeg's scan data, 1.37x its encode time (2.37 ms per + XL frame vs ~90 ms of USB) |
| T-1 | "No id" on the WebAPI with views | **Decided 2026-10-19:** the followed instance when views exist; selected (else sole) headless |
| T-2 | Where pins are set in the GUI | **Decided 2026-10-19:** the settings' surfaces table + each window's status-bar surface icons (Pin here / Unpin) |
| Q-1 | Default for starting the daemon | **Decided 2026-10-19:** when a deck is attached. **Added 2026-10-20:** the studio also starts it when none answers, deck or not (`run --wait --with-parent`: its own decks need the daemon; a deck plugged in later is taken), and stops it on quit |
| Q-2 | The studio on macOS | **Decided 2026-10-19:** nested in UnrealNG.app (shared Qt), alias at the DMG root |
| Q-3 | Activity look by default | **Decided 2026-10-19:** key flash only; options: off, or flash + strip message; `ttd.seek_by` ignored |

## Later

- Move the clients (`unreal-qt/`, `unreal-screen-viewer/`, `unreal-videowall/`, `testclient/`, the planned ones) into
  a clients group, all at once, in one centralised change ([source-layout.md §7](source-layout.md#7-moving-existing-code)).

- Raw deck controls as a controller-manager device (bind deck keys in the bindings table) — control surfaces "raw
  controls from processes".
- Several host processes (unreal-qt + the video wall with their own WebAPIs): port allocation, discovery, a shared
  focus order.
- Several decks with different targets shown side by side in the studio.
- Haptics or LED feedback to companions (no Stream Deck use).
- Other process-side surfaces through the same daemon (MIDI controllers with displays, Loupedeck): decide when one is
  wanted.
- Per-instance companion scopes (companion F3).

## Errata in other documents

| Document | Fix |
|:--|:--|
| control surfaces `architecture.md §6` | "the Qt client sets the selected instance when the focused one changes" → `TargetState` ([targeting.md](targeting.md)) (applied 2026-10-20, S1) |
| control surfaces `protocol.md` §2, §4, S4 | no own route `/api/v1/surfaces/ws`: the surface protocol is the `surface` topic of the one WebSocket `/api/v1/ws` ([protocol.md §0](protocol.md#0-transport-one-websocket), D7); `SurfaceHub` becomes the topic's handler in the shared hub (applied 2026-10-20, S1) |
| control surfaces `protocol.md` `instance` op | replaced by `target` ([protocol.md §2](protocol.md#2-target)) (applied 2026-10-20, S1) |
| control surfaces `requirements.md` "what exists" | the Surface Dial driver and `ttd.jog_*` / `ttd.scrub_*` landed on master (`720fbb53e`, `f3d35d587`) after the design (applied 2026-10-20, S1) |
| controller manager `debug-and-ttd.md:252-259` | the Stream Deck integrates as a companion: link this design |
