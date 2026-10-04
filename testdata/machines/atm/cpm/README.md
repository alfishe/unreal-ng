# ATM Turbo 2+ CP/M disks

CP/M material for the ATM Turbo 2+ machine (`ATM710`).

On the ATM Turbo the CP/M system itself is in the system ROM: the BIOS menu's CP/M entry starts
CCP, BDOS and the BIOS from ROM, and the floppy only holds files (cylinder 0 is reserved and carries
no system). So there is no separate "boot disk"; any ATM CP/M floppy works, and the system
utilities come on their own disk.

## `prince.trd` - Prince of Persia for ATM CP/M

- Added with the ATM CP/M BIOS listing work (commit `bb963ca1d`, 2026-09-17); its download source
  was not recorded.
- **CRC32:** `3D87562E`, **SHA-256:** `d3361437628a2638755ea2abe7e8d0c1d364acffee6923e00aa786e64e71d941`
- **Format:** raw TRD, 655 360 bytes, 80 x 2 x 16 x 256 bytes, ATM CP/M layout (no TR-DOS catalog;
  CP/M directory at cylinder 1 / side 0).
- **Contents:** `PR.COM`, `PRINCE1.OVL`, `PRINCE2.OVL`, `TITLE1.DAT`, `TITLE2.DAT`, `PRINCE.HOF`.
- **How to run:** boot CP/M from the BIOS menu, then `PR` on the drive that holds this disk.

## The ATM CP/M system utilities disk

The ATM CP/M "SYSTEM" utilities disk (STAT, PIP, SUBMIT, ASM, FORMAT, SYSGEN...) for ATM-turbo 1, 2
and 2+ is already in the repository as
[`../../atm450/cpm/sys.trd`](../../atm450/cpm/sys.trd) (source, hashes and boot notes in
[`../../atm450/README.md`](../../atm450/README.md)). It is not duplicated here.
