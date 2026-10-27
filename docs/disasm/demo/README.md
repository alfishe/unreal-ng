# Demo analyses

Demos taken apart while making them run in unreal-ng: what the code does, what the
emulator has to provide, and what was found or fixed on the way.

| Demo | Machine | What is here |
|---|---|---|
| [The Link](thelink/README.md) (invdemo, 2009, Alone Coder) | Pentagon 1024 + NeoGS | the author's working disk taken apart (ALASM sources, how the demo assembles itself at start, the ZX / NeoGS ZX-DMA protocol); the whole demo converted to sjasmplus with unreal-asm and built from source, byte for byte the author's binaries; a release loader with a machine check and a clean SCL ([sjasm/](thelink/sjasm/README.md)) |
| [Edge](across-the-edge/README.md) | Pentagon | border effect synchronisation, storyboard of the parts |
| [Insult](insult/README.md) | Pentagon 128 | the IM 2 disk loader, and the TR-DOS snapshot bug it exposed |
| [Scroller by Demarche](scroller/README.md) (1996) | Pentagon 128 + Covox | disassembly, memory banking, boot failure analysis |
