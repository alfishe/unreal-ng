# ZX Spectrum Timing Tests 128K v1.0 (Richard and Tim Butler, 2015-03-30)

The 128K edition of the Butler timing suite (the 48K one is `testdata/loaders/sna/Timing_Tests-48k_v1.0.sna`):
tests 1-34 run instruction groups from uncontended and from contended RAM until the frame interrupt and compare
R, the loop count and SP with values measured on a late-timing +2. It runs in 48K mode (the program says so). No
license stated.

`Timing_Tests-128k_v1.0.szx` (SpecEmu snapshot of a 128K), sha1 `df6a466cdf06fb938a342decd81a1a10ad2985d3`, the
same bytes from both sources:

- [zxe.io depot copy](https://zxe.io/depot/software/ZX%20Spectrum/ZX%20Spectrum%20Timing%20Tests%20-%20128K%20v1.0%20%282015-03-30%29%28Butler%2C%20Richard%3B%20Butler%2C%20Tim%29%5B%21%5D.szx)
- [the authors' site, archived](https://web.archive.org/web/20171130113325id_/http://www.zxspectrum4.net:80/downloads/timing_tests/Timing_Tests-128k_v1.0.szx)

Results of real early-timing 128K machines (two Toastracks, Issue 6K and 6U: tests 4, 17, 18, 26 and 33 fail from
contended RAM with the values listed) are on the
[ZX Spectrum tests wiki](https://github.com/redcode/ZXSpectrum/wiki/ZX-Spectrum-Timing-Tests-128K);
`core/tests/emulator/video/contentionprobe_test.cpp` (`Early128KFailures`) holds them.
