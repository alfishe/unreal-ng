# Patches for NedoOS upstream

Patches against the NedoOS kernel found while sizing CD-ROM support ([../proposal.md](../proposal.md)). They are for
the NedoOS maintainers (upstream SVN `svn://nedoos.ru/nedoos/nedoos`; `alfishe/NedoOS` on GitHub is a read-only
mirror), not for unreal-ng. Base: the mirror at `a75034922945341a901025a0474b96f3c18afa9c` (2026-10-08).

| Patch | What it fixes | Size |
|---|---|---|
| [0001-fatfsdrv-ide-atapi-no-hang.patch](0001-fatfsdrv-ide-atapi-no-hang.patch) | `src/kernel/fatfsdrv.asm`: (1) `waitDRQ` waits for ever when a command is aborted (an ATAPI unit given ATA READ / WRITE, `OS_READSECTORS` on a CD drive): it now returns with carry on ERR, and `readsectorsIDE` / `writesectorsIDE` return the error `0xFF`; (2) `readidentIDE` takes status `00h` before IDENTIFY for "no device", while an ATAPI unit after a reset may show `00h` (no DRDY): it now reads the reset signature and reports the unit as ATAPI (`1`), as the IDENTIFY path already does | +38 bytes of kernel code (ZX-Evo build); the BDOS table after the 256-byte align does not move |

## Checked

- Applies cleanly to the mirror at `a750349` (`git apply --check`; `patch -p1` and `svn patch` take the same file).
- The kernel assembles with it (sjasmplus from the tree, `build_kernel_evo.bat` settings): `main.asm` and
  `hobeta.asm` without errors; `$ before align` goes from `0x34B5` to `0x34DB`, under the `0x3500` boundary.

## Not checked

- Booting a kernel built with it. A kernel rebuilt here from the unpatched tree does not match `release/sd_boot.$C`
  byte for byte (the packed system code differs: another `mhmt` build than the one that made the release) and does
  not boot in unreal-ng either, while the release kernel does. So the boot check needs the maintainers' (or the
  owner's Windows) toolchain: build `build_kernel_evo.bat` with and without the patch and boot both, ideally with a CD
  drive on the slave channel (unreal-ng: `ZXEvoErs_Test.NedoOsCdplayPlaysAudioTracks` puts one there).
- Real hardware.

## How to send it

The maintainers take changes by mail or on the forum, not by pull request: the letter is
[letter-ru.md](letter-ru.md) (Russian, as the project's discussion is). Attach the `.patch` file. Where to post is the
owner's choice (the NedoOS thread on zx-pk.ru, or the maintainers' mail / Telegram).
