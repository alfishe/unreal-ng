# DONE: automation mouse on the machine's own mouse

Design: [design.md](design.md). Branch `mouse-api-routing` (from master `3921de12a`).

| Item | Status |
|:--|:--|
| Device inventory and `IMouseSink` describe / wired / unread / step limit; `MouseManager` device listing | done: Kempston (`kempston`), Sprinter board mouse (`sprinter`), ZX-Evo / TS-Conf AVR PS/2 (`evo-ps2`) |
| `/mouse/*` and MCP / CLI / Lua / Python on the machine's mouse: `device`, `devices`, `mouse_fitted`, `queue` in status (additive JSON), `?device=` / `device` argument, `glide`, 409 `no_mouse` | done; OpenAPI updated (`verify_openapi_coverage.py`: `/mouse/glide` covered) |
| Glide with input queued behind it (automation-interfaces Q5) | done, journaled per step |
| Sprinter in the GUI: capture only while polled (SIO B or `#58`) | done (`SprinterInput::IsMouseInUse`); the host path itself was already on master |
| Tests | `DebugMouseManagerMachines_Test` (19 models x 2), TTD exact replay (48K, Sprinter), serial packet / glide / in-use tests, FN 1.15 drive icon "D" click (`UNREAL_SPRINTER_HDD` + the pack's data disk on the slave) |
| Live check (own instance, WebAPI on a private port) | Pentagon: glide 300 / -40 + queued click -> ports 75 / 45; `kempstonmouse` off -> 409 `no_mouse`. Sprinter (BIOS 3.07 BETA 1, MAME pack `sp_hdd_sys.chd` + `sp_hdd_media.chd`): `in_use` false during the BIOS, true under DSS 1.71 (receiver 1 215.3 baud, in tune); home + glide to (98, 35) + click: FN's left panel shows `D:\*.*`. CLI `mouse status` / `devices`, Lua `mouse_status().device` checked live; Python compiled with `ENABLE_PYTHON_AUTOMATION=ON` (off in the default build, not run live) |

Permanent documentation: [command-interface.md §11](../../emulator/design/control-interfaces/command-interface.md#11-mouse-input-injection),
the WebAPI / CLI / Lua / Python interface docs, the MCP README, [.recipe/input/mouse.md](../../../.recipe/input/mouse.md).

Known, not from this work: `tools/verification/webapi/src/test_api_mouse.py` cases that inject on a **paused**
emulator and read the state back at once fail (8 of 55): live input on a paused machine is applied at its next
executed instruction (TTD live-input gateway), as the recipe says; the new device / glide cases pass.
