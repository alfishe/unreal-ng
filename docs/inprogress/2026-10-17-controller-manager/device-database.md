# Device database and controller images

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Design, for review |
| **Decisions** | D5 (photos in one house style, hotspot maps), D12 (VID/PID identity) in [README.md](README.md#decisions-2026-10-17) |
| **Research** | [research-host-apis.md §5](research-host-apis.md#5-other-controllers), [research-other-emulators.md §4 I-4, I-11](research-other-emulators.md#4-ideas-to-take) |

## Contents

- [1. What the database answers](#1-what-the-database-answers)
- [2. devices.json](#2-devicesjson)
- [3. Lookup order](#3-lookup-order)
- [4. User overrides](#4-user-overrides)
- [5. Licences of the sources](#5-licences-of-the-sources)
- [6. Images: the house style](#6-images-the-house-style)
- [7. Provenance record](#7-provenance-record)
- [8. Hotspot maps](#8-hotspot-maps)
- [9. User photo and the hotspot editor](#9-user-photo-and-the-hotspot-editor)
- [10. Retro side images](#10-retro-side-images)
- [11. Seed families](#11-seed-families)
- [12. Open points](#12-open-points)

## 1. What the database answers

The input is a host device's identity: bus, VID, PID, version, name, SDL GUID and backend
([architecture.md §5](architecture.md#5-device-identity-and-merging)). The database returns:

| Answer | Used by |
|:--|:--|
| **family** (`xbox-elite-2`, `dualsense`, `8bitdo-sn30-pro`, `generic-db9-adapter`) | display name, default play and debug profiles |
| **layout**: how the device's raw controls map onto the canonical vocabulary ([architecture.md §6](architecture.md#6-the-canonical-control-vocabulary)) | backends that see raw buttons and axes, i.e. our HID drivers, Raw Input, evdev |
| **driver** with its quirks (report offsets, init commands, rest values) | the raw HID drivers |
| **image set** and **glyph set** (labels per control: "A", "Cross", "P1") | the dialog, the on-screen notices |
| **filter** (`ignore: synthetic`) | the backend merge: the macOS `AppleGCSyntheticDevice` 045E:028E, version 0 |

## 2. devices.json

Shipped as `data/controllers/devices.json`. Overrides from the user ([§4](#4-user-overrides)) are merged on top.

```json
{
  "schema": 1,
  "families": [
    {
      "id": "xbox-elite-2",
      "name": "Xbox Elite Wireless Controller Series 2",
      "vendor": "Microsoft",
      "match": [
        { "bus": "usb", "vid": "045e", "pid": ["0b00"] },
        { "bus": "bt",  "vid": "045e", "pid": ["0b05", "0b22"] }
      ],
      "layout": "xbox",
      "driver": {
        "id": "xbox-gip",
        "quirks": {
          "paddles": [
            { "firmware": "<5.00",           "report": 34, "byte": 14, "profileByte": 15 },
            { "firmware": ">=5.00 <5.17",    "report": 46, "byte": 18, "profileByte": 19 },
            { "firmware": ">=5.17",          "report": 47, "byte": 14, "profileByte": 20 }
          ],
          "paddleBits": { "paddle.p1": 1, "paddle.p2": 2, "paddle.p3": 4, "paddle.p4": 8 },
          "paddlesHiddenWhenProfileMapsThem": true,
          "ble": { "report": 1, "paddleByte": 19, "profileByte": 17, "mirrorOnProfile": "learned (D19)" },
          "unmappedPaddleReport": { "firmware": ">=5.13", "enable": "gip:0x4d", "size": 17 },
          "triggerSaturationCalibrate": true
        }
      },
      "controls": ["south", "east", "west", "north", "dpad.*", "lb", "rb", "lt", "rt",
                   "lstick.*", "rstick.*", "view", "menu", "guide", "profile",
                   "paddle.p1", "paddle.p2", "paddle.p3", "paddle.p4"],
      "outputs": ["rumble", "trigger-rumble"],
      "images": "xbox-elite-2",
      "glyphs": "xbox",
      "profiles": { "play": "builtin/play/xbox-default", "debug": "builtin/debug/elite-paddles" }
    },
    {
      "id": "macos-synthetic-xbox",
      "match": [ { "bus": "usb", "vid": "045e", "pid": ["028e"], "version": "0000",
                   "backend": "iohid", "ioClass": "AppleGCResource" } ],
      "ignore": "synthetic"
    },
    {
      "id": "8bitdo-sn30-pro",
      "name": "8BitDo SN30 Pro",
      "vendor": "8BitDo",
      "match": [ { "vid": "2dc8", "pid": ["6001", "6101"] } ],
      "modes": { "6001": "d-input", "6101": "bluetooth" },
      "layout": "nintendo-positional",
      "images": "8bitdo-sn30-pro",
      "glyphs": "nintendo"
    }
  ],
  "layouts": {
    "xbox": { "south": "A", "east": "B", "west": "X", "north": "Y", "view": "View", "menu": "Menu" }
  }
}
```

Rules:

- **One family may have several PIDs.** 8BitDo changes the PID with the mode switch; the Elite has USB, Bluetooth
  classic and BLE ids. A `modes` table names each PID, so the dialog can say "in D-input mode".
- **A version range** (`"firmware": ">=5.17"`) selects driver quirks. A pad whose firmware is outside every known
  range gets the newest layout and a notice: "unknown firmware 5.21: paddles may be wrong".
- **`layout` maps raw indices to canonical names** for the backends that need it (`button.<n>`, `axis.<n>`,
  `hat.<n>`); for backends that already name controls (GameController, GameInput, evdev key codes) it maps their names.
- **`outputs` lists the actuators**: `rumble`, `trigger-rumble`, `hd-haptics`, `adaptive-triggers`, `light`
  ([haptics.md §2](haptics.md#2-actuators)); the backend confirms them at run time.
- **`hid` corrects the layout for the HID paths** (IOKit HID, Raw Input, hidraw), per family or per `match` entry
  (a match's map replaces the family's, for a pad whose USB and Bluetooth reports differ):
  `buttons` (button usage n from 1 to a control, `""` keeps `button.<n-1>`), `rxRyTriggers`, `dpadAxes` (the cross on
  X / Y), `hat` (the control the first hat is: `lstick` on a single Joy-Con), `consumer` (`{"0223": "guide"}`) and
  `bits` (`{"report": 1, "byte": 9, "bit": 4, "control": "misc.2"}`: a vendor bit outside the descriptor, the byte
  counted after the report id). The maps of the shipped families are read off SDL_GameControllerDB's Mac OS X lines
  ([POC 033 inventory §4](../../../tools/poc/033-vector-controller-pictures/inventory.md#4-how-the-button-maps-were-read)).
- **`controls` lists what the device has.** The dialog draws only those controls, and a profile binding to a control
  the device lacks is shown greyed out ([research-other-emulators.md X-8](research-other-emulators.md#5-pitfalls)).

## 3. Lookup order

| Step | Match | Score | Example |
|:--|:--|:--|:--|
| 1 | Exact bus + VID + PID + version within a firmware range | 100 | Elite 2 USB firmware 5.17 |
| 2 | VID + PID (any version) | 80 | Elite 2 with unknown firmware |
| 3 | SDL GUID in the bundled `gamecontrollerdb.txt` (exact, then ignoring version, then ignoring platform: as vAmiga) | 60 / 55 / 50 | a known generic pad |
| 4 | RetroArch autoconfig `.cfg` imported by the user (VID/PID +30, exact name +20, as RetroArch scores) | 30-50 | a pad RetroArch knows |
| 5 | Name heuristics ("xbox", "dualsense", "8bitdo", "retro", "db9", "joystick") → family template | 20 | an unbranded "USB Gamepad" |
| 6 | Unknown: raw controls, a generic image, the mapping wizard ([ui-ux.md](ui-ux.md)) | 0 | a DB9 adapter with no data |

- The highest score wins. A tie goes to the more specific entry, and a user override beats a built-in entry at the
  same score.
- The chosen step and score are part of the device report (`GET /controllers/host`, the Advanced view), so a wrong
  match can be explained.
- The wizard's result is saved as a user entry in the gamecontrollerdb line format plus our family fields. It can then
  be shared as a pull request to `data/controllers/`.
- A `gc:` device (a pad only GameController sees, [architecture.md §5](architecture.md#5-device-identity-and-merging))
  has no VID / PID: its family comes from step 5 on GameController's vendor name ("DualSense Wireless Controller" ->
  the PlayStation family, "Xbox Wireless Controller" -> the Xbox one), else step 6.
- The PlayStation pads (DualShock 4 054C:05C4 / 09CC / 0BA0, DualSense 054C:0CE6, DualSense Edge 054C:0DF2) and the
  Switch Pro controller (057E:2009) are read by their own drivers on macOS (`SonyPad`, `SwitchProPad`, by VID / PID,
  [haptics.md §9](haptics.md#9-backends)), whatever entry matched them; the entry gives the name, family, layout and
  images as for any pad. So are the Joy-Cons (057E:2006, 2007; the `joy-con` entry, the OS's product name "Joy-Con
  (L)" / "(R)"), alone or a left and a right one as one pad "Joy-Con (L/R)". The charging grip (200E) shows each Joy-Con in it
  as an interface of its own: the side of each comes from the device info (below the USB handshake, subcommand 0x02),
  then it is a Joy-Con as any and pairs the same way; an empty slot (no answer) stays hidden.

**As built (2026-10-23): steps 3 and 4 as an import.** No gamecontrollerdb.txt is bundled and no RetroArch profile is
read at match time; the user imports a mapping for one pad instead (`database_import`, `ImportDevice` in
`core/controllers/src/deviceimport.*`), and it becomes a devices.user.json entry that steps 1-2 then match:
- a gamecontrollerdb.txt line: the GUID's little-endian words give bus (word 0: 3 = `usb`, 5 = `bt`), VID (word 2) and
  PID (word 4); a GUID whose words 3 and 5 are not 0 carries no USB ids and is refused. SDL names map to the
  vocabulary (`a` south, `b` east, `x` west, `y` north, `back` view, `start` menu, `guide`, shoulders `lb` / `rb`,
  triggers `lt` / `rt`, stick clicks, `dp*` the D-pad, `misc1` share, `paddle1-4`, `touchpad`), axes add `lstick.*` /
  `rstick.*`;
- a RetroArch autoconfig .cfg: `input_vendor_id` / `input_product_id` in decimal, the RetroPad names (`b` south, `a`
  east, `y` west, `x` north, `l` / `r` / `l2` / `r2` / `l3` / `r3`, `select` view, `start` menu, `menu_toggle` guide,
  the D-pad).
The entry is `{"id": "user-<vid><pid>", "name", "layout": "generic", "match": [{vid, pid, bus}], "map", "controls",
"source"}`; `map` turns the raw controls into the canonical ones (`b3` → `button.3`), on IOHID and Raw Input alike
(a HID pad's own button names are the family's `hid.buttons`, read by `HidLayoutFor(match)`). The user's family wins
over a built-in one of the same VID:PID. An existing user entry of the same id keeps the members the import does not
set (a photo's `images`). What the import cannot take over is listed
(`skipped`: `platform`, an axis bound to a button). The Qt dialog's **Import mapping…** picks the line of a whole
gamecontrollerdb.txt by the selected device's VID:PID.

## 4. User overrides

`<user config>/unreal-ng/controllers/devices.user.json` has the same schema:

- It adds families for devices we do not know.
- It renames a device ("Left Elite", "Right Elite").
- It replaces an image set with a user photo ([§9](#9-user-photo-and-the-hotspot-editor)).
- It changes a quirk, for example forcing the raw HID path for a pad.

`gamecontrollerdb.user.txt` in the same folder is read after the bundled database.

**As built (2026-10-23).** `DeviceDatabase::Active()` is the database every reader uses (the backends, the router's
service, the profile store, the Qt dialog): the compiled-in families with devices.user.json merged over them
(`DeviceDatabase::SetUser`), an immutable snapshot swapped under a mutex, so a reader holding the old one keeps it.
The Qt client reads the file at start, before the backends match a device. An entry whose `id` names a built-in
family is merged field by field (only the fields it names change; `controls` and `match` replace the list; `match`
may be left out); an entry with a new id is a family of its own and needs `match`. A file that does not parse or
does not validate leaves the active database as it was and is reported. The host verbs `database`, `database_save`
and `database_delete` ([surfaces.md §2](surfaces.md#2-verbs)) read and edit the file on every plane; the hotspot
editor writes through them; `database_import` adds an entry from a gamecontrollerdb line or a RetroArch autoconfig
(§3). Not built: `gamecontrollerdb.user.txt` read as a file (an import of its line instead).

A user family may carry a `map` (canonical control -> raw control: `button.2`, `lstick.y-` for one half of an axis,
`hat.1.up`), and a connected device that matches it by VID + PID takes the map at once (`devicemapping.h`,
`HostInputService::RefreshUserMaps`): each raw control also reports as its canonical one. The learn wizard (`learn
finish`) and an import (`database_import`: a `gamecontrollerdb.txt` line or a RetroArch autoconfig file, RetroPad B =
`south`) write such a family, `user-<vid><pid>`, with `source` and its `gamecontrollerdb` line (bN, ±aN, hN.M; SDL's
a0..a3 are the HID X Y Z Rz, our sticks), ready to export. `database` also looks a device up by its SDL GUID
(`guid=`) or a connected device (`device=`).

## 5. Licences of the sources

| Source | Licence | Use |
|:--|:--|:--|
| usb.ids (<http://www.linux-usb.org/usb.ids>, 2026.06.26) | GPL-2.0-or-later **or** BSD-3-Clause | Vendor and product names, under the BSD option. Weak for retro devices. |
| SDL `controller_list.h` / `usb_ids.h` | zlib (Valve copyright) | Seed of the families table and controller types |
| SDL HIDAPI driver sources | zlib | Reference for report layouts, offsets, init commands |
| SDL_GameControllerDB | zlib | Generic pad mappings (step 3) |
| RetroArch autoconfig profiles | GPL-3.0 (joypad-autoconfig repository) | **Not shipped.** The user imports them; only the user's own copies are read. |
| Our measurements (`probes/`) | ours | Firmware-specific quirks |

## 6. Images: the house style

Q-1 is answered (owner, 2026-10-23): the pictures are vector art. Each device is drawn from a data model in millimetres
by a generator, not traced: [POC 033](../../../tools/poc/033-vector-controller-pictures/README.md) writes
`front.svg` (SVG Tiny 1.2 for QSvgRenderer, named layers, one `control-<name>` group per control) with its
`front.hotspots.json` from the same geometry and a `provenance.json`. Photos (the sources below) are references for
sizes and layout only, nothing is copied; platform logos are left out (D5). A user may still add a photo with its own
hotspot map: the format is the same for SVG and raster.

**Sources, in order of preference:**

1. Manufacturer press kits and product pages whose terms allow editorial or product-identification use. The terms are
   recorded per image ([§7](#7-provenance-record)).
2. Openly licensed photos: Wikimedia Commons CC0 / CC BY / CC BY-SA, with attribution kept in the provenance record and
   in the About box.
3. Our own photographs of devices we own (the Elite 2, retro joysticks, DB9 adapters).

**Style guide:**

| Item | Rule |
|:--|:--|
| Views | `front` (face of the pad, sticks up), `back` (paddles, triggers), `top` (shoulders, ports) when the device has controls there; joysticks: `three-quarter` plus `top` |
| Angle | Orthographic or near-orthographic, the device centred, horizontal axis level |
| Canvas | 2048 × 1536 px master, 4:3, the device inside the central 1600 × 1100 px |
| Callout safe areas | 224 px left and right free of the device, for callout labels; 96 px top and bottom |
| Background | Transparent (PNG / WebP with alpha); no shadows baked in, the dialog draws a soft shadow |
| Colour treatment | Desaturate to a neutral body (85 % desaturation); keep the device's own button colours at 60 % saturation; normalize exposure so the body sits at L* 35-45 (dark) or 80-88 (light); the dialog tints the active control |
| Cleanup | Remove reflections of the studio, dust, cables (or straighten the cable out of frame), stickers |
| Branding | **No platform logos.** Retouch out the Xbox, PlayStation and Nintendo logos and the guide-button logo; the guide button gets a neutral ring. Trademarks are named in text only, to identify the device. |
| Output sizes | Master 2048 × 1536; derived 1024 × 768 and 512 × 384 WebP; a 128 × 96 thumbnail for the device list |
| Naming | `data/controllers/images/<family>/<view>.webp`, `<view>.hotspots.json`, `provenance.json` |

**Fallback images** per family template, used when a device has no image of its own: `generic-xbox-like`,
`generic-playstation-like`, `generic-nintendo-like`, `generic-snes-like`, `generic-arcade-stick`, `generic-db9-stick`,
`generic-keyboard`, `generic-mouse`, `generic-unknown` (a pad outline with raw button numbers).

## 7. Provenance record

`data/controllers/images/<family>/provenance.json`:

```json
{
  "family": "xbox-elite-2",
  "images": [
    {
      "view": "front",
      "source": "https://…",
      "author": "…",
      "licence": "CC BY-SA 4.0",
      "terms": "attribution in the About box",
      "retrieved": "2026-10-17",
      "retouch": "logos removed, desaturated 85 %, background removed, cable removed",
      "retouchedBy": "…"
    }
  ]
}
```

- A pull request that adds an image without a provenance record fails a check in
  `tools/verification/controllers/check_images.py`, which is part of the plan ([testing.md](testing.md)).
- The About box lists every CC BY / BY-SA attribution from the records.

## 8. Hotspot maps

`<view>.hotspots.json`. Coordinates are in **design space**: the 2048 × 1536 master canvas. The dialog scales them with
a `QTransform`, so derived sizes and user photos of any size use the same map.

```json
{
  "schema": 1,
  "family": "xbox-elite-2",
  "view": "back",
  "size": [2048, 1536],
  "controls": [
    { "control": "paddle.p1", "label": "P1",
      "shape": { "ellipse": [1240, 980, 70, 120] },
      "anchor": [1240, 980], "callout": "right", "order": 1 },
    { "control": "lt", "label": "LT",
      "shape": { "polygon": [[620, 260], [760, 240], [790, 380], [640, 400]] },
      "anchor": [700, 320], "callout": "left", "order": 1,
      "analog": { "kind": "trigger", "fill": "down" } },
    { "control": "lstick.x", "group": "lstick", "label": "LS",
      "shape": { "circle": [640, 720, 90] },
      "anchor": [640, 720], "callout": "left",
      "analog": { "kind": "stick", "pair": "lstick.y", "radius": 60 } }
  ]
}
```

| Field | Meaning |
|:--|:--|
| `control` | A name from the canonical vocabulary ([architecture.md §6](architecture.md#6-the-canonical-control-vocabulary)) |
| `shape` | Hit area and highlight: `circle [cx, cy, r]`, `ellipse [cx, cy, rx, ry]`, `rect [x, y, w, h]`, `polygon [[x, y] …]` |
| `anchor` | Where the leader line of the callout starts |
| `callout` | `left` / `right` / `top` / `bottom`: which label column takes it. Labels in a column are sorted by `anchor.y` so leader lines do not cross |
| `order` | Tie-break within a column |
| `analog` | Live drawing: a stick dot inside `radius`, a trigger fill, a touchpad finger |
| `group` | Draw several controls as one callout (a stick's x / y / click; the four D-pad directions) |

The same map serves three things:
- hit testing (click a control to edit its binding);
- live highlight from control events;
- the callout layout, which shows the binding of the current layer ([ui-ux.md](ui-ux.md)).

## 9. User photo and the hotspot editor

For a device with no image, or when the user prefers a photo of their own pad:

1. **Pick the image.** Device page → **Picture → Use my photo…**: a file, or a paste from the clipboard.
2. **Crop and straighten.** The editor offers crop and rotate, and fits the image to the 4:3 design canvas (letterboxed;
   the image is not scaled destructively).
3. **Place the hotspots.** The editor lists the device's controls (from the database `controls`, or from the raw
   controls of an unknown device).
   - **Press-to-place:** press the control on the pad, and the list selects it.
   - Click the spot on the photo, then drag to size a circle or draw a polygon.
4. **Choose the callout side.** It is proposed from the anchor's x and can be changed.
5. **Save.** The image and the map are copied to `<user config>/unreal-ng/controllers/images/<device id>/` and recorded
   in `devices.user.json`.
6. **Share.** "Share…" exports a folder (image, map, a provenance stub to fill in) ready for a pull request.

The editor is the same widget the project uses to make built-in maps, so built-in and user maps cannot drift.

**As built (2026-10-23).** `HotspotEditor` (unreal-qt `controllers/hotspoteditor.*`; ui-ux.md §3.6): per view (front,
back, top) a file (EXIF rotation applied) or a paste, turned by quarter turns, straightened by up to ±15° and cropped
(applied to the photo as picked, not stacked); the result is kept as it is (scaled down only past 4096 × 3072) and
letterboxed into the design canvas, by the editor and by `ControllerPicture` alike. Every shape of §8: circle,
ellipse, rectangle, polygon (drawn corner by corner; drag-to-size is a Size and a Height box instead). The set is
saved as `images/<device id>/<view>.png` + `<view>.hotspots.json` for each view with a photo and a spot (the front
is required), where the device id is the database entry's (`xbox-elite-2`, `user-2dc8abcd`), the entry as
`{"id": <family>, "images": <device id>}`; a device the database does not know gets a family `user-<vid><pid>`
with its name, its USB ids as `match` and its controls. `ControllerPicture` looks in the user's `images/` before the
shipped ones, draws `<view>.png` / `.jpg` as well as `<view>.svg`, and lists a set's views (`Views`). Share… writes
the same set into a chosen folder with `provenance.json` (§7: view, `"source": "own photograph"`, today's date, the
author, licence and retouch fields empty) and `entry.json`.

## 10. Retro side images

The right pane of the dialog shows the retro device plugged into each connector, in the same style and with the same
hotspot maps. Here `control` names retro controls ([retro-devices.md](retro-devices.md)), not host controls.

| Retro device | Image subject | Controls with hotspots |
|:--|:--|:--|
| `kempston-joystick` | Quickshot II style stick, or Competition Pro (choice per profile: "Kempston + Competition Pro") | `up` `down` `left` `right` `fire`, `fire2` (pin 9) when used |
| `sinclair-joystick` | Sinclair SJS-1 | `up` `down` `left` `right` `fire`; the label shows the keys (6 7 8 9 0 or 1 2 3 4 5) |
| `cursor-joystick` | Protek / AGF interface with a stick | `up` `down` `left` `right` `fire`, labelled 7 6 5 8 0 |
| `md6-pad` / `md3-pad` | Sega Mega Drive 6-button / 3-button pad | `up` `down` `left` `right` `a` `b` `c` `x` `y` `z` `start` `mode` |
| `kempston-mouse` | two-button ball mouse | `dx` `dy` `left` `right` `middle` `wheel` |
| `amx-mouse` | AMX mouse (three buttons) | `dx` `dy` `left` `middle` `right` |
| `light-gun` | Magnum Light Phaser | `trigger`, `pointer` |
| `keyboard` | per machine: 48K rubber keys, 128 / +2, Pentagon, Next membrane, PC keyboard for the Evo / Sprinter / ATM | every ZX key; a chord shows its keys |

The keyboard images double as the target picker for "bind to a ZX key": a click on a key of the picture chooses it.

## 11. Seed families

The shipped `devices.json` has 74 families: the first-party pads of Microsoft, Sony, Nintendo, Valve, Google, Amazon
and Logitech, 8BitDo per model, the retro adapters, and the licensed / compatible families generated from SDL3's
`controller_list.h` (Xbox 360 / One compatible, PlayStation compatible, Switch wired and Switch Pro compatible, arcade
sticks and fight pads per protocol). The full list with the ids per bus, the path on macOS, Windows and Linux and the
picture of each is the [POC 033 inventory](../../../tools/poc/033-vector-controller-pictures/inventory.md); the
generator script of the compatible families is kept with the all-pads task's scratch files, the families themselves
are plain data.

Image status: every family has an own POC 033 vector set or a look-alike's (Xbox One S on Xbox One, Elite 1 on
Elite 2, Ultimate 2C on Ultimate 2, Switch 2 Pro on Switch Pro, the compatible families on the generic pads, the
retro adapters on the NSO pads). THEJOYSTICK (row 23 of the first seed) is still unverified (Q-8).

The Stream Deck (0FD9) is not a host device here. It is a companion client through the WebAPI
([debug-and-ttd.md](debug-and-ttd.md), [research-host-apis.md §6](research-host-apis.md#6-stream-deck)).

## 12. Open points

| # | Question |
|:--|:--|
| Q-1 | Answered (owner, 2026-10-23): vector pictures drawn by a generator from per-device data, POC 033 ([§6](#6-images-the-house-style)). |
| Q-8 | Retro Games Ltd THEJOYSTICK / THESPECTRUM pad VID/PIDs: read them from ioreg when a device is available. |
| Q-9 | Manufacturer press-kit terms. **Answered** (D5): allowed with the source and terms recorded per image, platform logos removed in the retouch; where a vendor forbids it, CC images or our own photos. |
