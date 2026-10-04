# ZX Spectrum +3 CP/M disk

Collected 2026-10-03 for the `PLUS3` machine.

## `zx-cpm-2.2.dsk` - ZX CP/M 2.2 for the +3 (M. Williams, 2009)

- **Source:** Spectrum Computing / ZXDB entry 23486, "ZX CP/M 2.2", availability "Available":
  <https://spectrumcomputing.co.uk/entry/23486/ZX-Spectrum/ZX_CPM_22>, file
  <https://spectrumcomputing.co.uk/pub/sinclair/utils/z/ZXCP-M2.2.dsk.zip> (49 037 bytes, holds
  `ZX CPM v2.2.dsk` and `ZX CPM v2.2.txt`, both dated 2009-05-02). Fetched 2026-10-03.
- **CRC32:** `26FC1FC5`, **SHA-256:** `78b5639a212514c0d859288db65ebfd2e9258a254f05717d1f1752d0cfee3309`
- **Format:** Extended CPC DSK (written by Spectaculator), 194 816 bytes, 40 tracks, 1 side, 9 sectors
  of 512 bytes (IDs 1-9): the standard +3 180 KB format (+3DOS directory on track 1). Track 0 holds no
  boot sector (all `#E5`).
- **What it is:** a free CP/M 2.2 port for the +3 (beta). The CCP and BDOS are Digital Research's
  open-sourced code; the BIOS is new. The system lives in two +3DOS files, `CPM.SYS` (BDOS + BIOS) and
  `CCP.SYS`, loaded by the BASIC program `DISK`. Memory map: CCP `#C400`, BDOS `#CC00`, BIOS `#DA00`.
  The 80-column console is shown as two overlapping 64-column halves (Symbol Shift + Q / W).
- **How to boot:** on the +3 boot menu choose **Loader** (or `LOAD "DISK"` in +3 BASIC); `DISK`
  loads `CPM.SYS` and `CCP.SYS` and starts CP/M at `A>`. Not yet booted on the emulator.
- **Contents:** `DISK`, `CPM.SYS`, `CCP.SYS`, and the Digital Research tools `ASM`, `DUMP`, `ED`,
  `GENHEX`, `GENMOD`, `LOAD`, `MAC`, `OBJCPM`, `PIP`, `SDIR`, `STAT`, `SUBMIT`, `XSUB`.
- **Licence (from the text file):** free to copy and use, not to be resold; the DRI parts are under
  Caldera's open-source licence.

## Not placed: Amstrad / Locomotive "Spectrum CP/M Plus"

The canonical +3 CP/M is Locomotive Software's CP/M Plus (with Mallard BASIC), sold by Amstrad. ZXDB
lists it as entries 9420 ("Spectrum CP/M Plus") and 14024 ("+ Mallard BASIC") with availability
"Distribution denied", and offers no download, so it is not in the repository.
