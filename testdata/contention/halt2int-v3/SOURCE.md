# HALT2INT v3 (Mark Woodmass, 2022-01-04)

Measures when the Z80 accepts the frame interrupt after a `HALT`, from the value of `R` the handler sees. The
`HALT` sits at the start or the end of each 16K bank (#4000 / #7FFF are contended on a 48K), and the interrupt
comes at five frame T-states: in the top border, at the first contended T of an early and of a late machine, on
the second picture line and on the last contended run of the picture. The first column does the same for the
floating bus. A late or early machine shows in the header line. License: GPL v2 (`COPYING`, the source headers).

Files from the release zip, unmodified but for the line endings of the `.asm` sources (CRLF in the zip, LF here), zip sha256
`c76c1fef02315017b8ae9d64c6c339f49b20daec7931412416c5556c681ecedf`:

- [zxe.io depot copy](https://zxe.io/depot/software/ZX%20Spectrum/HALT2INT%20v3%20%282022-01-04%29%28Woodmass%2C%20Mark%29%5B%21%5D.zip)
- [the ZX Spectrum Z80 tests wiki page](https://github.com/redcode/Z80/wiki/HALT2INT), with the expected screens
  for early and late 48K timings and photos of real machines

`halt2int.tap` is the 48K program, `halt2int128.tap` the 128K one. The zip's two photos of a real early and a late
48K (2 MB) are not copied; the early one shows the same values as the published expected screen, which
`core/tests/emulator/video/contentionprobe_test.cpp` (`Halt2Int48KEarly`) transcribes.
