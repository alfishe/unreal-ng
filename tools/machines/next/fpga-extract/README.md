# fpga-extract - generated pieces taken from the Next's own sources

| Script | Makes | From |
|:--|:--|:--|
| `extract-bootrom.py <bootrom.vhd> <out.rom>` | the 8K boot ROM image (`rom/next/nextboot.rom`), prints its MD5 | the FPGA sources `cores/zxnext/src/rom/bootrom.vhd` |
| `gen-nextreg-table.py <registers.txt> <out.inc>` | `core/src/emulator/io/z80n/nextregtable.inc`, the register table (number, name, access, reset) | the distribution's `registers.txt` v2.4 |

Run again when the distribution or the core version changes; the table's test (`NextSkeleton_Test.ReportsDescribeTheMachine`
counts the rows) and the register defaults tests tell what moved.
