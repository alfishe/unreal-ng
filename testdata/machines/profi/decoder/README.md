# Profi port decoder PROMs (K556RT4, 256 x 4)

These are the reference tables for the Profi port decode tests. How the tables are read (address inputs, output
bits, the devices each output selects) is described in
[docs/inprogress/2026-10-01-profi-v3-v5/decoder-prom.md](../../../../docs/inprogress/2026-10-01-profi-v3-v5/decoder-prom.md).

| File | Board | Kind | CRC32 | Source |
|:--|:--|:--|:--|:--|
| `556rt4-v3.2.bin` | Profi v3.2, U5 on the controller board | dump | `A4C3436A` | read off a board in 2009 for the MDESK re-trace: [alemorf/retro_computers](https://github.com/alemorf/retro_computers/tree/master/Profi_3_2), `doc/Profi3.zip`, `ROM/Profi_RT4_line.BIN` |
| `556rt4-v4-v5.bin` | Profi 4.0 / 4.01 / 5.0, D10 | transcription | `5657346D` | the table printed in the [v4.01 manual](https://speccy4ever.speccy.org/doc/profi401.pdf) (book p.25, "Дешифратор PROFI-2+") and the [v5.0 album](https://speccy4ever.speccy.org/doc/profi50.pdf) (p.8, "Дешифратор PROFI 3+"). The two books are typeset differently and print the same bytes |

The low nibble of each byte is the PROM output; the high nibble is 0.
