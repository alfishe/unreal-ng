# Symbol bundles

Label files for known ROMs and system variables. The emulator ships this folder as `symbols/` next to `rom/` in every
build and package. `manifest.json` lists the bundles. Each bundle names a file and the SHA-256 of the 16 KB ROM page it
belongs to.

At the start of each instance, and after a ROM reload, the emulator hashes the loaded ROM pages. Every bundle whose
page is there becomes a symbol set `bundle:<id>` (origin `bundle`, priority 50, below every loaded file).
`UNREAL_SYMBOL_BUNDLES=0` turns this off.

| Field | Meaning |
|---|---|
| `id`, `title`, `file`, `note` | the set id (`bundle:<id>`), its title, the label file (relative to this folder), where the digest comes from |
| `match.page_sha256` | the lower-case hex SHA-256 of the 16 KB ROM page (a list: any of them) |
| `match.page` | compare only this ROM page number |
| `space` | `rom` = the file's addresses are offsets in the page that matched (one set per matching page); a space spelling (`cpu:main`) = records without a page go there; absent = the records' own pages (`ROM8:0000`) |
| `except` | names left out (the 48K ROM's `spare` label is code in the 128K's ROM 1) |

The other files are not bundles: `128k_rom.map` (the other map of 128K ROM 0; the bundle takes `128k_rom_relabeled.map`, "Verified & Relabeled"), `sos.l`, `next/` (no NextZXOS
ROM ships to hash) and the Sprinter maps of code that runs in RAM (`bios304-setup.map`, `bios304-p0-setupstub.map`,
`spectrum-exe-pp.map`). Load them yourself (`symbols import`).

Format of the manifest, matching, and how it is applied: `core/src/3rdparty/unreal-asm/include/unrealasm/symbols/bundles.h`,
`LabelManager::ApplyBundles`, `.recipe/analysis/symbols-import-export.md` (section Bundles).
