# bios-build

Builds a 256 KB Sprinter BIOS image from the community Sprinter-BIOS sources (zxgit.org/Tolik-Trek)
with sjasmplus, with a fixed build date so a rebuild gives identical bytes. Usage, the source
commits used for the images in `data/rom/sprinter/` and how to track new releases:
[bios-versions.md](../../../../docs/inprogress/2026-09-28-sprinter/bios-versions.md).

```bash
python3 tools/machines/sprinter/bios-build/make-bios.py --help
```
