# ATM Turbo 2+ keyboard controller firmware

The i8031 / AT89S52 on ATM Turbo 2+ boards (v7.xx) answers every `IN #FE`,
reads the PC keyboard, keeps a clock and drives the RS-232 port. The emulator
runs these images on its MCS-51 core (`[ATM] Kbc=`). Design and facts:
[docs/inprogress/2026-10-01-atm2-keyboard-controller](../../../docs/inprogress/2026-10-01-atm2-keyboard-controller/README.md).

| File | Version | Crystal | Bytes at #2C | SHA-1 | From |
|---|---|---|---|---|---|
| `at22-7mhz.bin` | 2.2 (Kamil Karimov, 2005-03-27) | 7.0000 MHz | 2,2,0,7 | ea92348dad96e604574ea2677660a53eb471c95b | [atm_at22.zip](http://atmturbo.nedopc.com/download/shems/roms/atm_at22.zip) `at22_f07.rom` |
| `at22-11mhz.bin` | 2.2 | 11.0592 MHz | 2,2,1,1 | d0dfeaf2f14bf43aeb3eafff4a4dc411300cb12b | same, `at22_f11.rom` |
| `at22-12mhz.bin` | 2.2 | 12.0000 MHz | 2,2,1,2 | 32e23e66da050073a04faec8a1c391c55379aa9f | same, `at22_f12.rom` |
| `at31-7mhz.bin` | 3.1 (2006-10-26, RS-232 added; needs 256-byte RAM) | 7.0000 MHz | 3,1,0,7 | e381c91be7cba4b8ba2ba628928983666d9dba46 | svn atmturbo [ver_3_1_caro/at31.zip](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/source/keyb_rom_805x/ver_3_1_caro/) `AT31_07.BIN` |
| `at31-11mhz.bin` | 3.1 | 11.0592 MHz | 3,1,1,1 | a348a5428d0f35fb4197d5d7aa312f59d632c317 | same, `AT31_11.BIN` |
| `at32m-7mhz.bin` | 3.2m2 (2019-12-07) | 7.0000 MHz | 3,2,0,7 | 9be29fb604964883dc786f4c4f48e60328552332 | svn atmturbo [ver_3_2_caro/V32.zip](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/source/keyb_rom_805x/ver_3_2_caro/) `AT32m07n.BIN` |
| `at32m-11mhz.bin` | 3.2m2 | 11.0592 MHz | 3,2,1,1 | 10ef47c555854fb7c534fbfb33e00367b8b8e735 | same, `AT32m11n.BIN` |
| `at40.bin` | 4.0 (LVD, Kulicheg, 2021-10-29; AT89S52) | 11.0592 MHz | 4,0,1,1 | 073cc5acd51771503a32dcc1d0150d6daebd4839 | [atm_at40.zip](http://atmturbo.nedopc.com/download/shems/roms/atm_at40.zip) `at40.bin` |
| `at41.bin` | 4.1 (Maxagor, 2023-03-05; AT89S52) | 11.0592 MHz | 4,1,1,1 | 6f63f7aa05ba53ca576712544f5714d23bf4a679 | [atm_at41.zip](http://atmturbo.nedopc.com/download/shems/roms/atm_at41.zip) `at41.bin` |

The archives carry the assembly sources too; the svn repository
`svn://svn.nedopc.com/atmturbo` (`source/keyb_rom_805x/`) holds 2.2, 3.1 and
3.2m. Not here: MicroART 1.0x (MAME `rfat710.rom` / `rf2ve3.rom`, no COM port).
