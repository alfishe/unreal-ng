# CHD (MAME hard-disk images) as a shared media format

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Branch** | `media-chd` |
| **Status** | Implemented; follow-ups in [TODO.md](TODO.md) |
| **Part of** | PLAN #58, the unified media manager ([../2026-09-28-storage-manager/](../2026-09-28-storage-manager/TODO.md)) |

MAME stores every hard disk and SD card as a CHD file, and its ZX Spectrum family drivers use it
for the ATM IDE, the Nemo IDE (Pentagon, ZX-Evo), SMUC (Scorpion), the NeoGS SD card and the
Sprinter IDE. unreal-ng now reads and writes CHD as one more block format of the media manager:
any IDE unit or SD card slot of any machine takes a `.chd`, and the slots and machines do not
know about it.

| Document | Content |
|---|---|
| [design.md](design.md) | Decisions (own code vs vendored, codecs), layers, save / export, surfaces, tests, measurements |
| [TODO.md](TODO.md) | What is done, the follow-ups |
| [docs/file-formats/disk-images/chd.md](../../file-formats/disk-images/chd.md) | The format and what is supported (permanent reference) |
| [.recipe/media/sprinter-hdd.md](../../../.recipe/media/sprinter-hdd.md) | DSS 1.71 straight from MAME's `sp_hdd_sys.chd` |
