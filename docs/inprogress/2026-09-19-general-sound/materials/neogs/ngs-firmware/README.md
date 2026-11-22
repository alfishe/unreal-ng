# NeoGS firmware sources

The Z80 sources of the NeoGS card firmware from the NedoPC SVN repository `ngs`
(`svn://svn.nedopc.com/ngs`, folder `/z80/`, revision 193, last changed 2026-08-09). The project page is
`http://nedopc.com/gs/ngs_eng.php`. Only the parts the emulator runs were imported. The rest of the repository has
the FPGA and CPLD sources, the flasher, the old GS versions and the tools.

| Folder | What it builds |
|---|---|
| `z80/main_rom/` | The main ROM v1.11, `main_ngs.a80` (`build.bat`: `asw -U -L main_ngs.a80`, then `p2bin main_ngs.p neogs.rom -r $-$ -k`). The loader copies it from flash `#10000` into RAM pages 0-1 and runs it there. `main_full.a80` packs the 512 KB flash image (the loader, then the main ROM at `#10000`) |
| `z80/loader_ngs/` | The flash loader at flash `#00000`: it copies the main ROM, or loads `NEOGS.ROM` from the SD card, and has its own command set after a `#55`/`#AA` handshake |
| `z80/*.a80` | Shared includes: `macros.a80`, `ports_ngs.a80` (the NeoGS ports), `sdcomand.a80` (SD card commands) |

The files are kept as they are in SVN. The comments are in CP866 Russian, and the repository stores them with LF
line endings.

## Building

The sources are for AS by Alfred Arnold (`asw` is its Windows build, `asl` its Unix build;
`http://john.ccac.rwth-aachen.de:8000/as/`). AS builds from its C sources on macOS:
`cp Makefile.def-samples/Makefile.def-arm-osx Makefile.def && make asl p2bin`.

- `z80/main_rom/`: `asl -U -q -g MAP main_ngs.a80` and `p2bin main_ngs.p neogs.rom -r '$-$' -k` give the 32 KB
  main ROM. It equals `data/rom/neogs/full_ngs.rom` at `#10000` byte for byte, and `neogs.rom` in SVN.
- `z80/loader_ngs/`: `asl -U -q -C -x loader_ngs.a80` and `p2bin` give the 2476-byte loader. It equals flash
  `#00000` apart from the 2 bytes of `DATA_VERS` (`macros.a80`), the build date that `asl` takes from the clock. The
  shipped loader was built on 10.02.2026.

`tools/gsfirmware/markup_symbols.py` builds the main ROM this way. It writes the labels to `data/symbols/gs/neogs.map`
and compares every routine with GS v1.05a in `data/symbols/gs/NEOGS-VS-GS.md` (gs-debugger N5).
