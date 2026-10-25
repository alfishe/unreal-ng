# 034 — analysis

Numbers: [measurements-2026-10-23-corpus.md](measurements-2026-10-23-corpus.md).

## Re-pack and rename

As expected: the TR-DOS file set ignores the layout, the interleave and the label, and the tape content ignores the
container. 99.97 % were equal. None of the three misses was a signature defect:

- two TRDs whose catalogues hold entries past the file count in the volume sector, which the SCL writer did not
  export. Fixed in the core and confirmed by the re-run in the measurements;
- one FDI without TR-DOS files.

## Saved to: the first-draft rule fails on the corpus as it is

The Jaccard rule needs n / (n + 1) ≥ 0.8 to survive one added file, so n ≥ 4. To survive one rewritten file it needs
(n - 1) / (n + 1) ≥ 0.8, so n ≥ 9. The corpus is mostly small disks: 53 % hold one file and 17 % two. On those
disks one saved high score already breaks the rule: recall is 19.9 % for an added file and 27.5 % for a rewritten
data file.

The rule's "no code file missing" guard also rejects the common way a TR-DOS game saves: `SAVE "HISCORE" CODE` over
the old file, or sector writes into its own code file. Recall there is 2.2 %.

The catalog rule compares the catalogue entry `(name, type, start, length)` separately from the data. A game that
saves keeps its entries:

- an added file is one more entry;
- a file rewritten in place keeps its entry and changes only its data.

Rule G requires every entry kept, at most 1 file changed and at most max(2, n) entries added. It recalls 99.7 % of
all saves. Its weak point is the one-file disk: there, "at most 1 file changed" accepts any disk that holds one file
of the same name, type, start and length. The corpus has such clusters, built on a common one-file loader layout:
`PEKIN`, `RNROLL`, `SIMPSNCD`, `DXINOX_E`, `LOOPZ_F`.

Rule H adds "at least 1 file unchanged". The clusters go away, and so does one real case: a one-file game that
rewrites its only file. Recall for that case drops to 3 %, and "rewrite a code file" over all sizes to 48.8 %. Over
all saves H recalls 83.9 %, above the 80 % bar. On disks of 2 files and more, every save variant is at least 94.8 %.

## False matches

Every rule has candidate false matches, and most of them are not false. The ground truth (one ZIP = one title)
splits a title over its cracks, translations and fixes, and over the magazines and compilations that carry it. Exact
content already shows this: 30 content signatures are shared by 62 releases, all of them the same files.

H matches 81 disks (0.391 % of 20 701) to another release. By hand review they fall into three groups:

- the same title in another release;
- the title on a magazine or compilation disk with a few more files. The max(2, n) bound lets a 40-file title sit on
  a 47-file magazine disk;
- very few different programs. The example is a lone boot loader (`ACN71 BOOT.$B`) found on test disks that use the
  same loader.

Counting only the different programs, the false-match rate is about 0.01 % of disks. Counting every cross-release
match, it is 0.39 %, above the 0.1 % bar. The first draft (A) has 0.063 % by the same count, but only because it
matches almost nothing on small disks.

Whether "the title on a magazine disk" counts as a false match is a product question. The magazine's menu runs first,
not the game, until the user picks the game. A profile applied to the magazine is wrong for the menu and right for
the game. The max(2, n) bound keeps this to magazines that add little. A stricter bound (at most 2 added) would drop
most of them and still keep every save variant measured, since each of them adds one file.

## Memory signature

As specified (every page, ≥ 90 %), the rule matches 46 % of the pairs of different snapshots. Near the boot state,
RAM is mostly cleared, and cleared pages are equal everywhere.

Leaving out uniform pages and requiring at least 16 common pages removes that: 1 false match in 6992 pairs. But the
five oracle pairs show how far one game's RAM moves between frame 300 and the end of a recording: 67.5-92.6 % of the
pages are equal, and only one of the five reaches 90 %.

Any threshold between 63 % (the best non-match) and 67 % separates these five pairs from the 6992 others, but five
pairs are too few to set it. The memory signature stays best effort, and profiles use only its exact form
(`mem:sha256:`).

## Speed

SHA-256 (digestpp, portable C++) is the cost. It makes two passes per disk (the whole file, then each file's data)
and runs at 80 MB/s under load.

The DiskImage loaders were ten times slower: building every MFM track of an 80 × 2 disk took about 20 ms. The sector
path for TRD and SCL (99 % of the corpus) reads the same bytes the loaders would read, in about 2 ms. It was
cross-checked on 20 908 disks with one difference, which turned out to be a loader defect, now fixed.

One thread does 800-880 files/s under load average 50. The 2000 files/s bar would need an unloaded machine or a
hardware SHA-256. Media are independent, so a scan on every core does 10 000-11 400 files/s.
