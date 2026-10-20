# Stream Deck integration: TODO

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |

## Phases

Depends on the control-surfaces phases (branch `control-surfaces-design`): S1 core `ActionRegistry` + RunControl +
`/controllers/actions`; S2 `ContextState`, TTD published cursor, `seek_by`; S4 the surface protocol, served as the `surface` topic of `/api/v1/ws` (D7).

| # | Phase | Needs | State |
|:--|:--|:--|:--|
| SD0 | Owner decisions: layout and names (L-1 … L-6), targeting (T-1, T-2), Q-1 … Q-3 | | done 2026-10-19 |
| SD1 | Import the library, sdctl, the daemon (`run`, `pair`, `scenario`) and the tests into `companions/streamdeck/` (`UNREAL_STREAMDECK=OFF` by default), the project's style and build settings, hidapi + json embedded, docs `docs/companions/streamdeck/` (HID reference, performance), notices; 838 checks green | SD0 | done 2026-10-20 (turbojpeg still from the system: SD2) |
| SD2 | JPEG POC + vendored encoder (removes the system turbojpeg and its link warning), WebSocket client on one non-blocking event loop (NFR-8; the automation's cpp-httplib pool pinned to 1 until it moves onto the loop); zero warnings on gcc / MSVC / MinGW | SD1 | done 2026-10-20: `sdb::net::EventLoop` (poll / WSAPoll, timers, posts) + `sdb::ws::Client` (RFC 6455, reconnect with backoff, pings, bounded send queue), `sdctl ws`; checked live on `/api/v1/companions/events` and `/api/v1/websocket`, one thread. JPEG ([POC 032](../../../tools/poc/032-streamdeck-jpeg-encoder/README.md)): own encoder `sdb::jpeg` (turbojpeg's output, 1.37x its time), turbojpeg removed, the link warning gone; gcc 16 / MSVC / MinGW compile clean, full Linux / Windows companion builds: SD8 |
| SD3 | Device multiplexer, wire v2, the studio imported as a client (no USB), late-join replay; sdctl `emu-*` commands follow wire v2 | SD2 | daemon side done 2026-10-20: `sdb/devicewire.h`, `PictureCache` (replay), `DeckHub` (mirrors, own decks, bounded queues, join rules), `run --viewers`, `sdctl viewer`; reference [docs/companions/streamdeck/daemon.md](../../companions/streamdeck/daemon.md). Studio done 2026-10-20: `unreal-streamdeck-studio` (`companions/streamdeck/studio/`), wire v2 client (mirror / own, `Welcome` errors, replay, `DeckState`, own-role feature replies, reconnect with backoff, `Leave`), v1 server only behind `--listen`, no USB, editor + recorder + automation imported, Run through the daemon; `streamdeck-studio-tests`; reference [docs/companions/streamdeck/studio.md](../../companions/streamdeck/studio.md). Rest done 2026-10-20: daemon routes (REST + MCP, `sdb/daemondecks.h`): `/health`, `/decks`, brightness and scenario per deck (`PUT` / `DELETE /decks/{serial}/scenario`, watched), `/decks/{serial}/snapshot` (`Control`), the runtime routes per deck (`?deck=`); `run` without a scenario (idle) and `--wait` (no deck yet: own decks work, the first deck that appears is taken); the studio starts a daemon on launch when none answers, Brightness in the toolbar, Run on the deck it shows, quits cleanly on SIGTERM (`Leave`); `sdctl decks`, `emu-snapshot --api` over the daemon (wire v2), `emu-model` / `emu-record` / plain `emu-snapshot` stay v1 for `--listen` (why: [daemon.md](../../companions/streamdeck/daemon.md#sdctl-emu--and-the-device-wire)); hidapi split into `sdb-hid` (the studio does not link it). Checked live on the Stream Deck + XL |
| SD4 | Core: `TargetState` + view registration in unreal-qt and the video wall; `target`, `activity`, readout vocabulary, catalogue fields | S1, S2, S4 | open |
| SD5 | Daemon surface client, `action` kind, state-bound faces, contexts on menus, activity; default scenario v2 + its generator | SD3, SD4 | open |
| SD6 | Registry actions for the scenario's REST groups ([migration.md §4](migration.md#4-rest-paths-to-action-ids)), about 40 | S1 | open |
| SD7 | Supervision in unreal-qt, settings page, pairing from the deck and the studio | SD5 | open |
| SD8 | Windows and Linux ports, udev rule, packaging on all three | SD5 | ports done 2026-10-20: the daemon, sdctl, the studio and both test binaries build with zero warnings and pass (1011 + 94 checks) on Linux gcc 14 (the CI image, `docker/linux/build.sh --streamdeck --test`), MSVC 2019 and MinGW gcc 15, and on macOS; hidraw backend + `companions/streamdeck/linux/70-unreal-streamdeck.rules`, the DEB installs the daemon, the studio and the rule. Fixes on the way: a use-after-free on reload (the navigator read the replaced scenario), the studio's Leave lost to a reset on Windows, `shell` / `open` on Windows (cmd.exe, output, timeout, job object). Open: DEB/AppImage/ZIP/DMG staging of the daemon and the studio beyond the DEB install rules, the AppImage rule text in the settings page (SD7), the parent watch on Windows / Linux, a hardware run on Windows and Linux (SD9) |
| SD9 | Conformance suite in CI, hardware acceptance ACC-1 … ACC-7 | SD7, SD8 | open |
| SD10 | Move the plugin and utility entries of `tools/` into `plugins/` and `utilities/` (one mechanical commit) | SD0 | open |

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
| Q-1 | Default for starting the daemon | **Decided 2026-10-19:** when a deck is attached |
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
| control surfaces `architecture.md §6` | "the Qt client sets the selected instance when the focused one changes" → `TargetState` ([targeting.md](targeting.md)) |
| control surfaces `protocol.md` §2, §4, S4 | no own route `/api/v1/surfaces/ws`: the surface protocol is the `surface` topic of the one WebSocket `/api/v1/ws` ([protocol.md §0](protocol.md#0-transport-one-websocket), D7); `SurfaceHub` becomes the topic's handler in the shared hub |
| control surfaces `protocol.md` `instance` op | replaced by `target` ([protocol.md §2](protocol.md#2-target)) |
| control surfaces `requirements.md` "what exists" | the Surface Dial driver and `ttd.jog_*` / `ttd.scrub_*` landed on master (`720fbb53e`, `f3d35d587`) after the design |
| controller manager `debug-and-ttd.md:252-259` | the Stream Deck integrates as a companion: link this design |
