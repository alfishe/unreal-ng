# ROM images

The files in this directory (and `data/testrom/`) are firmware images of ZX Spectrum machines and clones.
They are **not** part of the unreal-ng source code and are **not** licensed under the GPL. They are included so
that the emulator works out of the box, on the following basis:

* **Sinclair / Amstrad ROMs** (`48.rom`, `128.rom`, `128_low.rom`, `plus2.rom`, `plus2a.rom`, `plus3.rom`,
  `plus341.rom`, `1982.rom`, `sos.rom`, `48for128.rom`, `service.rom`, `if1.rom`, and the 48K ROM font bitmap in
  `core/src/debugger/analyzers/rom-print/zxspectrumfont.h`): Amstrad plc, the copyright holder, has given
  permission (Cliff Lawson, Amstrad, comp.sys.sinclair, 1999) for the Spectrum ROMs to be distributed with emulators
  free of charge, provided the copyright is acknowledged and no fee is charged for the ROMs themselves.
  Amstrad copyright is hereby acknowledged.
* **TR-DOS** (`trdos.rom`, `trdos503.rom`, `trdos504t.rom`, `trd504tm.rom`, `dos.rom`, `dos6_10e.rom`, `128tr!.rom`):
  Technology Research Ltd (no longer trading). Distributed with emulators by long-standing custom; no formal permission exists.
* **Pentagon, Scorpion, KAY, ATM, Profi, ZX Evolution / TS-Conf, General Sound and other clone firmware**
  (`pentagon*.rom`, `glukpen*.rom`, `scorp*.rom`, `kay1024*.rom`, `atm*.rom`, `glukatm.rom`, `profi.rom`,
  `zxevo.rom`, `ts-bios*.rom`, `gs*.rom`, `bootGS.rom`, `lsy256.rom`, `qu7v42.rom`, `qc_3_05.rom`, `madrom.rom`,
  `zxi1.rom`, `2006.rom`, `xbios135.rom`, `sgen.rom`, `gd.rom`, `gmx.rom`, `1993.rom`, `ZXM-Phoenix_bios.bin`,
  `tk90.rom`, `tk95.rom`): property of the respective clone manufacturers and authors, distributed freely in the
  ZX Spectrum community. ZX Evolution firmware sources are published by TSLabs.
* **Open firmware**: `gdos-pd.rom` (public domain), `opense.rom` (OpenSE BASIC, GPL),
  `data/testrom/zx-diagnostics.rom` (Brendan Alford, GPL-3.0).
* `data/symbols/*.map` label tables are derived from published disassemblies and are provided for debugging only.

If you are a rights holder and object to a file being distributed here, open an issue or contact the maintainer
and it will be removed.
