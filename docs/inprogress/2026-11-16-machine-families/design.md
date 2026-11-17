# Machine families: a grouped machine picker

Status: in progress (branch `machine-families`).

## 1. Problem

The machine lists are flat: 21 models (some with two to four RAM sizes), 6 machine variants and 3 ZX-Poly
configurations. The Qt Machine menu shows them one after another; on the handheld (Steam Deck) the Model picker is a
column of ~35 rows. Each surface also walks the three tables itself (`Config::mem_model`, `MachineVariants`,
`ZXPolyGroup::Configurations`), so a client that wants a grouped menu (the handheld, the coming Android client) has to
rebuild the grouping on its own.

## 2. Model

Three levels: **family -> machine -> RAM size**.

- A **family** is a line of boards that users name together (Sinclair, Pentagon, ATM Turbo, ZX-Evo...).
- A **machine** is anything a create request names: a model short name (`ATM710`), a machine variant (`TSL-VDAC2`)
  or a ZX-Poly configuration (`ZXPOLY-128K`). Names do not change; only their presentation is grouped.
- **RAM sizes**: the model's sizes; a variant or a ZX-Poly configuration has one.

A level with a single entry is skipped by the menus: a family with one machine (Sprinter) is the machine itself, a
machine with one RAM size has no RAM level.

| Family | Machines |
|---|---|
| Sinclair | 48K, 48K + Beta 128, 128K, +2, +2A, +3 |
| Pentagon | Pentagon (128 / 512 / 1024) |
| ATM Turbo | v4.50 (512 / 1024), 2+ v7.10 (128 / 256 / 512 / 1024) |
| ZX-Evo | BaseConf, TS-Conf, TS-Conf + VDAC2 |
| Profi | v3, v5, Profi+ |
| Scorpion | Scorpion, + PROF ROM, GMX |
| ZX Spectrum Next | Next (1 / 2 MB), KS1, KS3, DDMA |
| ZX-Poly | 48K, 128K, Pentagon |
| Other clones | KAY, Quorum, Orel BK-08, Phoenix (not creatable yet: hidden) |
| Sprinter | Sprinter |

## 3. One catalog in the core

`core/src/emulator/machinecatalog.h`: `MachineCatalog::Families()` builds the tree once from the three tables and a
family table (family id, title, member names in menu order). Every surface reads it:

- Qt Machine menu: a submenu per family (the RAM sizes as entries of the machine's submenu).
- Handheld Model picker: drill-down, as the file browser (`..` back).
- WebAPI `GET /api/v1/emulator/machines`: the tree as JSON, the same for every client (Deck, Android, MCP); the flat
  `GET /emulator/models` stays and gains a `family` field per entry. MCP and CLI report the family next to the model.
- `MachineCatalog::Locate(name)` finds a machine's family; the running machine's name comes from `MachineIdentity`
  (variant, ZX-Poly configuration or model), so menus mark the current entry at every level.

A unit test keeps the catalog complete: every model, variant and ZX-Poly configuration is in exactly one family. A name
the family table misses still shows up (under Other clones), so a new model is never lost from the menus.

## 4. Clients

The tree is the contract for remote clients (the Android client): they build their picker from
`GET /emulator/machines` and start a machine with `POST /emulator/start {model, ram_kb}` (or the `mach.start` action),
never from their own copy of the list.
