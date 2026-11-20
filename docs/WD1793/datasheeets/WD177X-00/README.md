# WD177X-00 data sheet

Western Digital WD177X-00 Floppy Disk Formatter/Controller (WD1770, WD1772, WD1773), scanned pages `WD177X-00_page01.gif`
... `page23.gif`, and David Gahris' typed programming notes for the 1770-00 / 1772-00 (1995).

Source: the Commodore chip data archive, <https://bio.nic.funet.fi/pub/cbm/documents/chipdata/wd177x/>
(`177x01.gif` ... `177x23.gif`, `1772.txt`), fetched 2026-11-15.

Used by the MGT +D design (`docs/inprogress/2026-11-15-mgt-plus-d/`): step rates of the 1772-00 (6 / 12 / 2 / 3 ms),
settling 15 ms on the 1772 (30 ms on the 1770), motor on after a command, 6-revolution spin-up unless `h`, motor off
after 9 idle revolutions, status bit 7 = motor on, bit 5 = spin-up done (Type I) / deleted data mark (Type II / III),
precompensation flag `P`, Force Interrupt I2 / I3 only.
