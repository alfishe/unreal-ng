# TODO: mouse manager

Design: [design.md](design.md). Branch `mouse-manager`.

| Phase | Status |
|:--|:--|
| M1 core `MouseManager` + device sinks | implemented (committed on the branch): Kempston and Sprinter serial mouse as sinks, button sources, TTD apply through the manager, ZX-Poly members |
| M2 Qt capture controller, toolbar indicator / gate | implemented; release key = physical Ctrl+Esc on every platform, caught by an application-wide filter; `unreal-qt-tests` (real Qt key events, part of `test-parallel`) |
| M3 ZX-Evo AVR PS/2 mouse (ATM3, TS-Conf) | implemented: `EvoAvrMouse` (registers, found / none values, wheel nibble, keypad resolution in RTC cell #FD), TTD blob 38 `EvoMouse`; corpus `sprites.ttd`, the ATM3 core golden and the ATM3 TTD bench gate rows re-recorded; checked live on TS-Conf |
| M4 ATM450 / ATM710, AY mouse | ATM450 / ATM710: an external ZX-bus Kempston card (the boards have none: TURBO 2+ manual "Kempston joystick and mouse - Not supported"; NedoOS uses the card), UnrealSpeccy decode, not shadow-gated. AY mouse: not needed by any machine yet |
| M5 automation docs, `.recipe/input/mouse.md` | recipe written (MCP / WebAPI / CLI / Lua / Python, per-machine table, host capture); automation status `ports` come from the machine's own mouse (`PortDecoder::PeekMouseRegister`) |
