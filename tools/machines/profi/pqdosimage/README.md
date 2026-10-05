# PQ-DOS test disk image

`prune.py` cuts the small hard disk image the `PROFI-PLUS` boot tests use out of the PQ-DOS 2023-09 HDD image
(Karabas Pro, a ready 2 GB image with PQ-DOS 1.45 and DOS Navigator 2.0.16). The image is an MBR disk with one FAT16
partition at sector 2048 (64 sectors per cluster, label PQDOS). Only about 50 clusters are what the boot needs: the root
files (`QDOS.SYS`, `IO.DRV`, `CONFIG.SYS`, `AUTOEXEC.BAT`, `TRDBOOT.BIN`, ..), `DN` and `DOS`. The tool deletes the other
root entries (the games, demos, utilities: their FAT chains are zeroed in both FATs, their clusters cleared) and cuts the
file after the highest cluster kept.

| | Original | Test image |
|:--|--:|--:|
| Size | 2 038 063 104 bytes | 3 050 496 bytes |
| MBR, partition boot sector, FAT layout | as shipped | unchanged |
| Partition size in the table | 1936 MB | 1936 MB (nothing is read past the cut) |

Nothing is moved, so it stays the original's own layout. PQ-DOS boots from it into DOS Navigator, and DN shows the free
space of the real disk.

```
python3 prune.py <pqdos_image> <out.img> [name,name,..]
python3 prune.py ~/zx/profi/pqdos_image ../../../../testdata/machines/profi/pqdos/pqdos-hdd-small.img
```

Source: [Karabas Pro](https://github.com/andykarpov/karabas-pro) `software/profi/pq-dos/`, `pqdos-image-2023.zip`
(the raw `pqdos_image` inside: md5 `1825a9cf09987bea67446afec7158e99`). The test image and the floppy are in
[testdata/machines/profi/pqdos/](../../../../testdata/machines/profi/pqdos/README.md). Needs only Python 3.
`fat16.py` is the small read-only FAT16 reader the tool uses.
