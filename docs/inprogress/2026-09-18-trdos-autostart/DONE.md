# DONE — TR-DOS disk autostart

Implemented in `0f9e3b70` (`diskautostart.cpp`, 18 tests). Design: [design.md](design.md).

Follow-up 2026-10-11: after the reset the cold start's first arrival at `$02EF` lets the model adjust its paging
(`PortDecoder::FinishAutostartColdStart`); the ATM710 lays its windows out the "SPECTRUM 128" way, the ATM450
clears `#FDFD`.
[2026-10-11-atm-autostart-128k-paging](../2026-10-11-atm-autostart-128k-paging/README.md).
