# minimp3

Single-header MP3 decoder by lieff, https://github.com/lieff/minimp3, CC0 1.0
(see `LICENSE`). Vendored unmodified from the copy carried in zxtune
(`3rdparty/minimp3`, zxtune commit f619784, 2026-09-08).

Used by the NeoGS MP3 decoder model (`core/src/emulator/sound/chips/neogs/vs10xx.cpp`,
neogs-tdd.md §5.6), which feeds it exactly one frame per call.
