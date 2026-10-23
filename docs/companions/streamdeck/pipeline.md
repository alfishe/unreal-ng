# Stream Deck scenario pipeline (deckflow)

Scenarios change often. The pipeline keeps two **equal sources** of one scenario in step: a compact text spec that a
human or an agent edits, and the Stream Deck Studio project that the studio edits. An edit on either side is carried
to the other; the same thing changed on both sides is a conflict listed for a human, and nothing is written. From
the two it builds a meta description (key grids, use-case coverage) and plays the result on a deck that follows every
edit within a second. Keys name glyphs; the icon pack that draws them is chosen at run time (section 4).

Tool: [`companions/streamdeck/tools/deckflow.py`](../../../companions/streamdeck/tools/deckflow.py) (Python 3 standard
library; Pillow for the shots). It builds on the scenario generator
[`make_deck_scenario.py`](../../../companions/streamdeck/tools/make_deck_scenario.py) (steps, slots, the checks) and
reads the icon packs of [icons/README.md](icons/README.md) (`data/streamdeck/icons/packs/<pack>/`).

## Contents

- [1. Files](#1-files)
- [2. The loop](#2-the-loop)
- [3. The spec](#3-the-spec)
- [4. What the project holds](#4-what-the-project-holds)
- [5. Sync and conflicts](#5-sync-and-conflicts)
- [6. Checks](#6-checks)
- [7. Running on a deck](#7-running-on-a-deck)
- [8. Commands](#8-commands)
- [9. Tests](#9-tests)
- [10. Limits](#10-limits)

## 1. Files

| Path | What | Edited by |
|:--|:--|:--|
| `data/streamdeck/scenarios/src/<name>.spec.yaml` | The spec of a scenario | you, an agent; sync |
| `data/streamdeck/scenarios/src/shared/<menu>.spec.yaml` | A shared menu: one spec, opened by several scenarios | you, an agent; sync |
| `data/streamdeck/scenarios/<name>.deck.yaml` | The Studio project (scenario format v2) | Stream Deck Studio; sync |
| `data/streamdeck/scenarios/shared/<menu>.deck.yaml` | The project of a shared menu: the file the scenarios include | Stream Deck Studio; sync |
| `data/streamdeck/scenarios/.sync/<name>.base.json`, `.sync/shared/<menu>.base.json` | What both sides held at the last sync (the 3-way base), with the derived values (glyph, state) the project was built with, so that a glyph bound to an action later is not read as a studio edit; commit it with the spec and the project | generated |
| `docs/inprogress/2026-10-19-streamdeck-integration/scenarios/<name>.md` | The meta description: per menu the key grid with the glyphs (the scenario pack's pictures, `data/streamdeck/icons/packs/<pack>/src/<glyph>.png`), the keys, dials, strip; use-case coverage, actions without a glyph, unused glyphs | generated |
| `scratch/deckflow/` | Logs of the daemon and studio deckflow started, the contact sheet of the shots | generated (git-ignored) |

Nothing is rendered: a key's face names its glyph (`face: {glyph: run.pause}`) and the daemon draws it from the pack
the deck uses, at the deck's key size (section 4). The same project shows in every pack.

The example: [`src/sample.spec.yaml`](../../../data/streamdeck/scenarios/src/sample.spec.yaml) (a home page with the
C1, C2, C6 keys, a local menu) and the shared menu
[`src/shared/speed.spec.yaml`](../../../data/streamdeck/scenarios/src/shared/speed.spec.yaml); its description:
[sample.md](../../inprogress/2026-10-19-streamdeck-integration/scenarios/sample.md). It proves the loop; it is no
product layout.

## 2. The loop

For a human:

```bash
T=companions/streamdeck/tools/deckflow.py
python3 $T watch sample          # build, start an own Stream Deck Studio deck + daemon, play, follow every edit
#   edit src/sample.spec.yaml in an editor: the deck shows it within ~0.5 s
#   or open data/streamdeck/scenarios/sample.deck.yaml in the studio's editor, move / retitle / add keys, Save:
#   the spec takes the change (comments and layout kept), the deck follows
python3 $T shots sample --open   # every menu in one PNG, opened in Preview
python3 $T stop                  # end the studio and daemon deckflow started
```

For an agent (no window, no real deck, no emulator needed):

```bash
python3 $T check sample                       # errors with file:line; exit 1 on errors
python3 $T sync sample                        # after every edit of a spec or a project; exit 1 on a conflict
python3 $T run sample --offscreen             # own daemon (dry run) + offscreen studio, the deck plays it
python3 $T shots sample                       # scratch/deckflow/sample-shots.png: look at it before showing it
```

Then read the meta description, look at the sheet (`shots sample --all-packs`: every menu in every pack), and commit
the spec, the project(s) and `.sync/` together (only when asked to commit).

## 3. The spec

YAML, a small subset ([`deckyaml.py`](../../../companions/streamdeck/tools/deckyaml.py)): block mappings, flow
collections (`[0, 1]`, `{k: v}`, continued on deeper lines), plain and quoted scalars, comments. One key = one line.

```yaml
scenario: sample                 # or `shared: speed` in src/shared/speed.spec.yaml
model: plusxl                    # the deck model: grid, key size (default plusxl)
pack: magicwb                    # the scenario's icon pack: settings.pack (optional; default magicwb)
settings: {brightness: 60, activity: {ignore: [ttd.seek_by]}}   # the scenario's settings, as in the v2 format

menu home:                       # the first menu is the root
  title: Home
  context: play
  strip: {content: title, status: ttd, target: true}
  dials:
    0: {label: TTD, uc: U2.1, turn: {action: ttd.seek_by, value: {d: $ticks, rate: $rate, unit: seconds}},
        press: {action: run.pause_toggle}}
  keys:
    pause:  {at: [0, 0], uc: C1.1, action: run.pause_toggle}
    reset:  {at: [0, 1], uc: C1.2, action: mach.reset}
    x1:     {at: [1, 2], uc: C2.5, action: set.speed, value: 1, label: X1}
    speed:  {at: [1, 3], open: speed}       # a shared menu: src/shared/speed.spec.yaml
    save:   {at: [2, 2], open: save}        # a local menu: the section below

menu save:
  title: Save state
  uc: C6
  glyph: snapshot.save
  keys:
    file:   {at: [0, 4], uc: C6.2, action: snapshot.save, value: {path: $user/snapshots/deck.szx}}
    gohome: {at: [3, 8], nav: home}
    cpu:    {at: [3, 0], uc: C10.2, label: CPU, show: {readout: host.stats.cpu_percent, format: "{:.0f}%", warn: 100}}
    fps:    {at: [3, 1], uc: C10.3, label: FPS, show: {readout: emulator.stats, format: "{fps:.1f}"}}
```

A value key's `show` is the project's `face.value` ([scenarios.md §4.2](../../inprogress/2026-10-19-streamdeck-integration/scenarios.md#42-value-faces)):
`readout` a readout path (a whole readout or a field, dotted: `emulator.stats.frame_time_us.avg`), `format` a
template (`{:.0f}%`, `{fps:.1f} FPS`, `{rss_bytes:b}` bytes, `{frames:k}` SI, `{slots:#}` a count,
`{recording?REC|IDLE}`, `\n` a second line; absent = `{}`), `warn` / `alert` numbers that tint the text amber / red
(`warn` above `alert`: the low values are bad). A template holds `{` and `:`: quote it.

Key fields (`id: {...}` in a menu's `keys`; ids unique in the file):

| Field | What | Default |
|:--|:--|:--|
| `at` | `[row, col]` | the next free key, row by row (the back key kept free) |
| `uc` | use-case id(s) of [use-cases.md](../../inprogress/2026-10-19-streamdeck-integration/use-cases.md): `C2.1` or `[C2.1, C2.5]` | |
| `action`, `value`, `hold`, `params` | a catalogue action ([`data/streamdeck/catalogue.json`](../../../data/streamdeck/catalogue.json)) and its value | `hold`: the catalogue's `held` (unless the action has a `hold?` param) |
| `steps` | several steps in order: `[{action: ..., value: ...}, {nav: home}, {kind: delay, ms: 200}]` | |
| `nav` | `back`, `home` or a node id | |
| `do` | any other step of the v2 format, as it is (`{kind: toggle, on: ..., off: ...}`) | |
| `open` | a menu: a `menu ID:` section of this spec (the key's id must be that id; only `at` beside it), or a shared menu `src/shared/ID.spec.yaml` (an include; `title`, `glyph`, `label`, `uc`, `tags` override) | |
| `then` | `stay`, `back`, `home` after a successful action | `stay` |
| `title` | the node's title (breadcrumbs, the studio's tree) | the catalogue label of the action |
| `label` | text drawn under the picture (a slot, a speed, a drive) | none |
| `glyph`, `glyph_on` | glyph ids of [glyphs.json](icons/glyphs.md); `glyph: none` = no picture (the key shows its title) | the glyphs bound to the action in glyphs.json (also per param: `snapshot.quick` save / load) |
| `state` | a readout path; the face is `glyph_on` while it is true | the action's (`run.state.paused`, `toggle.<id>`, `toggle.set.toggle.<name>`) |
| `show` | a value key: `{readout: <path>, format: <template>, warn: n, alert: n}`; the key shows the readout's value as big text, its `label` (else the title) as the caption, its glyph shrunk above; a key with `show` needs no action (a press does nothing) | |
| `face`, `face_on` | more face fields (`bg`, `fg`, `fit`, ...), merged over the generated face | |
| `tags`, `activity`, `extra` | more tags; `activity: false`; any other node field, as it is | |

Menu fields (`menu ID:`): `title` (default the id), `uc`, `glyph` (default `nav.folder`), `label`, `context`, `strip`,
`back` (`[row, col]` or `false`; submenus and shared menus default `[0, 0]`), `navigation` (more `nav` fields:
`auto_exit`, `breadcrumbs`), `dials` (`index: {label, uc, turn | left / right, press}`), `face`, `tags`, `extra`,
`keys`.

A value that equals its default is the default: `glyph: run.pause` on a `run.pause_toggle` key reads as "the action's
glyph" and follows the action when it changes.

## 4. What the project holds

The project is an ordinary scenario v2 document (`unreal-streamdeck scenario check` passes on it, the studio opens it)
in the YAML style the studio writes (fkYAML, keys sorted): a Save in the studio changes only the lines of the edit.
Every node carries its meta in `tags`, so the project alone holds everything the spec says:

| Tag | On | Means |
|:--|:--|:--|
| `uc:C2.1` | any node | the use case |
| `open:speed` | an include | the shared menu |
| `shared` | the root | the document is a shared menu |
| `dial.0.uc:U2.1` | a menu | a dial's use case |

The glyphs are face fields, not tags: `face: {glyph: run.pause, label: S1}`, `face_on: {glyph: run.play}` (a key's
glyph and its `glyph_on`; a menu's and an include's glyph in their `face`). A face with a glyph and a label shows the
glyph shrunk and the label under it. A value face (`show`) is `face: {label: CPU, value: {readout: ..., format: ...}}`.

The pack is a run-time choice, not part of the faces: the spec's `pack` is the project's `settings.pack` (the
scenario's default), a deck's own pack setting in the daemon (`PUT /api/v1/decks/{serial}/pack`, the studio's pack
picker, [daemon.md](daemon.md)) wins over it, and without either the deck shows `magicwb`. A shared menu names no pack:
it shows in the pack of the scenario (the deck) that opens it. Switching the pack redraws the keys at once.

Keys are written by slot. A shared menu is an `include` node (`file: shared/<menu>.deck.yaml`); its keys are edited in
its own project (open `shared/<menu>.deck.yaml` in the studio).

In the studio:
- move, swap, retitle, rebind (the action step), delete, add keys and menus: all come back into the spec;
- a new key from the palette takes its action's glyph at the next sync;
- another glyph: the inspector's glyph field (face or face on); the spec gets `glyph:` (a glyph equal to the action's
  stands for the default);
- the pack: `settings.pack` of the document (the spec's `pack`); the pack picker of the preview only changes the
  preview;
- a project an older deckflow wrote (rendered faces `$data/streamdeck/scenarios/faces/...`, `glyph:` / `pack:` tags)
  is still read: the next sync writes it with glyph faces;
- a renamed id is a deleted node and a new one.

## 5. Sync and conflicts

`sync` reads three models per document: the spec's, the project's and the base (the last sync). Per node and field:

| Spec vs base | Project vs base | Result |
|:--|:--|:--|
| same | same | nothing to do |
| changed | same | the project gets it |
| same | changed | the spec gets it: the line of that key is rewritten (its field order and comment kept), a key moved to another menu goes to the end of that menu's keys, a new menu is a new section at the end of the file |
| changed | changed, to the same | taken |
| changed | changed, differently | **conflict** |

Also conflicts: a node deleted on one side and changed on the other, a key both sides put on the same slot, a key
whose menu the other side deleted. On a conflict nothing is written (spec, project, base stay as they are) and every
one is listed with the spec's line:

```
data/streamdeck/scenarios/src/sample.spec.yaml:17: error: conflict: reset at: spec [0, 3], project
  (data/streamdeck/scenarios/sample.deck.yaml) [0, 4], last sync [0, 1]
```

To resolve: make the two sides agree on that field (edit the spec to the project's value or the other way round, or
undo one of the edits), then `sync` again. `sync --prefer spec` / `--prefer project` takes one side for every conflict.
`build` (spec -> project only) refuses while the project holds edits not synced yet; `build --force` lets the spec
win. A project without a spec writes the spec (a scenario started in the studio); a spec without a project builds it.

After a sync the spec is read again and must give exactly the merged model; otherwise nothing is written and the
tool reports it (a bug of the patcher, not of the user).

## 6. Checks

`check` (and every sync) stops on errors, with the spec's file and line:

| Check | |
|:--|:--|
| Syntax | the YAML subset; unknown fields of the document, a menu, a key, a dial |
| Actions | ids in the catalogue (the same group's ids suggested); params against the catalogue's `params` text: unknown names and values outside a listed choice (`op (save\|load)`) are errors, a missing required param a warning; text / delta actions used right |
| Glyphs | ids in `data/streamdeck/icons/glyphs.json`; every glyph a key shows (its own or its action's) drawn by **every** pack in its own `src/` (a fallback does not count): an error names the packs without it and says how to add it (`tools/icons/streamdeck_glyphs.py`, a drawing in `tools/icons/streamdeck_packs/magicwb.py`, `tools/icons/regenerate_streamdeck_icons.py`); an action without a glyph is a warning (the key shows its title) |
| Pack | `pack` names a pack of `data/streamdeck/icons/packs`; `settings.pack` in the spec is an error (the top-level `pack` is it) |
| Use cases | ids (`C1`, `C1.1`, `C7.2a`) in `docs/inprogress/2026-10-19-streamdeck-integration/use-cases.md` |
| Readouts | `state` and `show.readout` against the readout vocabulary of `core/src/emulator/actions/readouts.h` (+ `toggle.<action id>`); `show`'s fields, its template (braces, specs, `?yes\|no`) and its thresholds (numbers) |
| Slots | inside the model's grid, one node per key, not the back key; the number of dials |
| Menus and includes | every local menu opened by exactly one key, `open` names a local menu or a shared spec |
| Release rules | no `shell`, `open`, `keys`, `webapi` steps (ACC-5); `$data` paths exist (the generator's checks) |
| The companion | `unreal-streamdeck scenario check <project> --catalogue data/streamdeck/catalogue.json` on the result (skipped with a note when the binary is not built; `--no-cli` skips it) |

## 7. Running on a deck

`run` and `watch` never touch a deck or a daemon they did not start, unless told to:

| Default | |
|:--|:--|
| Daemon | `unreal-streamdeck run --wait --viewers 37150 --api 37153 --no-lock --device deckflow-no-usb-deck` (no USB deck matches that serial), a dry run (`--dry-run --no-surface`: actions are logged, nothing reaches an emulator) unless `--host URL` names an unreal-ng WebAPI |
| Studio | `unreal-streamdeck-studio --own <model> --daemon 127.0.0.1:37150 --daemon-api 37153 --api-port 37152 --no-daemon-start` (`--offscreen` without a window) |
| Deck | the studio's own deck; the project is loaded with `PUT /api/v1/decks/{serial}/scenario {file, watch: true}`: the daemon reloads it on every write, the menu stack kept |
| `--api PORT` | a daemon that runs already (its own decks); `--deck SERIAL` a real deck, only when named |
| `--host URL` | the actions go to that unreal-ng (start it muted) |

`watch` polls the specs and projects of the scenario and its shared menus every 250 ms and syncs on a change; a spec
edit reached the deck in about 0.4 s, a studio Save came back into the spec in about 0.5 s. When the studio's editor
shows a project the sync rewrote and has no unsaved edits, it is reopened; with unsaved edits the tool says so (Save:
the next sync takes them).

`shots` navigates every menu (the included ones too) through the studio's runtime proxy (`POST
/api/v1/runtime/navigate?deck=`), takes `GET /api/v1/screenshot?area=deck` for each and puts them on one sheet,
`scratch/deckflow/<name>-shots.png` (`--open`: Preview). The deck shows the scenario's pack for the shots (its own
pack setting cleared); `--pack P` sets the deck's pack to P (`PUT /api/v1/decks/{serial}/pack`); `--all-packs` takes
every pack the daemon lists (`GET /api/v1/packs`), a row per pack, into `scratch/deckflow/<name>-packs.png`. After a
switch it waits until the deck's picture changed (at most 1.5 s); the deck's own setting is given back at the end.

## 8. Commands

| Command | What | Exit |
|:--|:--|:--|
| `check NAME` | the spec(s), the project(s), conflicts pending, the companion's check; nothing written | 1 on errors |
| `build NAME [--force]` | spec -> project, meta description | 1 on errors |
| `sync NAME [--prefer spec\|project]` | 3-way sync, then build | 1 on errors or conflicts |
| `run NAME [--deck S] [--api P] [--studio P] [--viewers P] [--host URL] [--offscreen]` | sync, start the own deck when needed, play | |
| `watch NAME [run options]` | run, then sync on every change | Ctrl+C |
| `shots NAME [--pack P \| --all-packs] [--open]` | the contact sheet: in the scenario's pack, in pack P, or a row per pack | |
| `stop [--api P]` | end what `run` started on those ports | |
| `list` | every spec: in sync, project edits to sync, never synced | |

`NAME` is the scenario name (`sample`), `shared/<menu>` or a spec path.

## 9. Tests

```bash
python3 companions/streamdeck/tools/tests/test_deckflow.py   # also in CI beside the generator's tests
tools/build/build.sh streamdeck-tests && cmake-build-agent-release/bin/streamdeck-tests
```

`test_deckflow.py` (fixture specs in `tools/tests/deckflow/src/`): the YAML subset (the studio's style byte for byte,
errors with lines), the spec's mistakes with their lines, deterministic builds, glyph faces (no picture written, the
pack as `settings.pack` both ways, a glyph missing from one pack, an older project's rendered faces still read), the
checked-in sample equals
the pipeline's output, the companion's check; sync: a spec-only change, project-only changes (a swap the way the
studio's move does it, retitle, rebind, new key and menu, delete, a key into another menu, another glyph, a
rebind that changes the default glyph), conflicts (same field, delete against edit, the same slot), both sides on
different fields merged, a shared menu edited in its project, the round trip spec -> project -> spec, and a project
as Stream Deck Studio saved it after a move (`tests/deckflow/fx.studio-saved.deck.yaml`, captured through the
studio's editor routes). `streamdeck-tests` loads the sample with its include, parses it without issues, checks it
against the shipped catalogue and that a label is drawn under a picture.

## 10. Limits

- The YAML subset has no anchors, block scalars or multi-line plain scalars.
- Dials, strips and settings merge as a whole per menu (both sides changing the dials of one menu is a conflict).
- An item's order in a menu is not kept (keys are by slot).
- A shared menu has its own model (its keys are laid out for it); its pack is the deck's.
- The studio does not reload a file changed on disk by itself: `watch` reopens it when it has no unsaved edits.
