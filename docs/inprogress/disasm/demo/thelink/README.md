# How The Link works - a tutorial series

The Link (invdemo, 2009) by Alone Coder - code and music; graphics by Alone Coder, Oimidrol and
Shiru - is a ZX Spectrum demo for a **Pentagon 1024** with a **NeoGS** card. Its trick: the sound
card's 24 MHz Z80 draws every picture, and the Spectrum reads the pictures out of the card's memory
as if it were its own ROM. This series explains, part by part, how it does that.

![The parts, captured in unreal-ng](../../../../disasm/demo/thelink/parts.png)

The code quoted is the annotated sjasmplus version of the author's sources,
[`docs/disasm/demo/thelink/sjasm/src/`](../../../../disasm/demo/thelink/sjasm/src/); it builds into
byte for byte the author's binaries ([sjasm/README.md](../../../../disasm/demo/thelink/sjasm/README.md)).
The reader is assumed to know Z80 assembler and the Spectrum 128.

## Chapters

| # | Chapter | What you learn |
|---|---|---|
| 1 | [The machine](01-the-machine.md) | Pentagon 1024 paging and 16-colour mode; the NeoGS card, its mailbox and ZX-DMA |
| 2 | [The start](02-the-start.md) | `GO`: the card program, uploading 20 pages, the resident loop, the timeline of the parts, music and credits |
| 3 | [The frame](03-the-frame.md) | the handshake every effect repeats each frame: card draws, Spectrum fetches by DMA and blits with `PUSH`; the interrupt-safe stack |
| 4 | [Tunnel](04-tunnel.md) | a lookup-table tunnel in the attributes, code generated on the card |
| 5 | [Rotator](05-rotator.md) | a rotating, scrolling logo: pre-rotated copies and generated line code |
| 6 | [Rotating bars](06-rotating-bars.md) | bars swinging about their corners, rasterised on the card into lists the Spectrum plots |
| 7 | [Multi-bars](07-multi-bars.md) | an 80-frame transition where the card writes a program for the Spectrum every frame |
| 8 | [Hedgehog](08-hedgehog.md) | a lit, textured 3D object in 16 colours: rotation, projection, sorting, the chunky-to-planar transpose |
| 9 | [Girl and mill](09-girl-and-mill.md) | two scrolling 16-colour layers with masks |
| 10 | [Textured face](10-textured-face.md) | the shared texture mapper in depth, and a textured ball |
| 11 | [Building it](11-building-it.md) | from the author's ALASM disk to a cross-assembled project and a release disk |

Read 1-3 first; the effect chapters (4-10) each stand alone after that.

## The timeline

| Part | Frames (50 per second) | Chapter |
|---|---|---|
| intro picture + music | 0-895 | [2](02-the-start.md) |
| tunnel | 896-1778 | [4](04-tunnel.md) |
| rotator | 1792-2674 | [5](05-rotator.md) |
| rotating bars | 2688-3248 | [6](06-rotating-bars.md) |
| multi-bars | 3476-3556 | [7](07-multi-bars.md) |
| hedgehog | 3584-4466 | [8](08-hedgehog.md) |
| girl and mill | 4480-4914 | [9](09-girl-and-mill.md) |
| textured face | 4928-5824 | [10](10-textured-face.md) |
| credits | after | [2](02-the-start.md) |

## Related

- [docs/disasm/demo/thelink/README.md](../../../../disasm/demo/thelink/README.md) - the analysis of
  the author's disk: files, start, page plan, what the emulator must provide, why it "hangs" on a
  classic GS.
- [sjasm/README.md](../../../../disasm/demo/thelink/sjasm/README.md) - the project and how the
  sources were converted.
- [sjasm/loader/README.md](../../../../disasm/demo/thelink/sjasm/loader/README.md) - the release
  loader, the memory image at `GO`, every page.
