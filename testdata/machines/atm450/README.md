# ATM Turbo 2 v4.50 (ATM450) test fixtures

Material for the unreal-ng ATM450 machine (design:
[docs/inprogress/2026-10-01-atm450/](../../../docs/inprogress/2026-10-01-atm450/)), collected
2026-10-01.

| Path | Content |
|:--|:--|
| `cpm/sys.trd` | the ATM CP/M "SYSTEM" utilities disk (STAT, PIP, SUBMIT, ASM, FORMAT, SYSGEN...) |

## `cpm/sys.trd` - ATM CP/M system utilities disk

- **Source:** the NedoPC ATM-turbo site, CP/M system software section:
  <http://atmturbo.nedopc.com/download/cpm/system/sys/sys.htm> (description),
  <http://atmturbo.nedopc.com/download/cpm/system/sys/sys.zip> (312 478 bytes; holds `SYS.TRD`
  and `SYS.INF`, both dated 2001-12-08). Fetched 2026-10-01. No licence is stated.
- **Target:** ATM-turbo 1, 2 and 2+ (`SYS.INF`: "a pack of standard external CP/M commands ... for
  absolutely all CP/M-compatible computers").
- **Format:** raw TRD, 655 360 bytes (80 cylinders x 2 sides x 16 sectors x 256 bytes, logical
  track = cylinder * 2 + side), the ATM CP/M floppy layout: cylinder 0 is reserved and carries no
  system (CCP and BDOS come from the ATM system ROM), the CP/M directory starts at cylinder 1,
  sector 1 (2 KB blocks, 2 directory blocks).
- **CRC32:** `78629828`
- **SHA-256:** `4e9a2d30090aa459467a964cea65db5d94cc12f58d3e2e1390fd02311a85bac1`

Used by `ATM450Boot_Test.CpmBootsFromSystemDiskAndListsIt`
([atm450_boot_test.cpp](../../../core/tests/emulator/machines/atm450/atm450_boot_test.cpp)):
the boot menu's CP/M entry signs on (`CP/M V2.2`, `BIOS V1.03` by XVR), runs the ROM's default
autostart `B:XC /R` (XC is not on this disk: `B:XC?`), and `DIR B:` lists this catalog - B: is the
floppy, A: the BIOS's electronic disk.
