# Keyboard 3D models: the proposed pipeline

A proposal for the agent who builds it; the owner decides the open questions of
[requirements.md](requirements.md) §9 first.

## 1. Tool

**Blender (bpy), headless**: `blender -b --factory-startup -P build.py -- <args>`. Blender 5.2.2 LTS is installed on
the dev Mac (`/opt/homebrew/bin/blender`).

Why Blender:
- one tool for the meshes, the PBR materials, UVs and texture baking, the preview renders (Cycles) and every export
  (glTF / GLB, USD / USDZ, FBX, OBJ, STL);
- reproducible from a script with `--factory-startup` and a pinned version;
- the importers of UE, three.js and Quick Look are tested against its exporters.

Considered and not chosen as the main tool:
- **CadQuery / build123d** (OpenCascade B-rep): exact fillets and drafts, STEP export; no materials, UVs, baking or
  render, so it would need Blender anyway. A cap family can still be prototyped in it and imported as a mesh if
  Blender's modelling of a sculpted profile proves hard.
- **trimesh / pygltflib** alone: glTF only, no materials beyond factors, no render.

## 2. Layout

```
tools/keyboard3d/                      (or the place the owner picks, open question 5)
  build.py                             entry: ids, formats, LODs; runs inside Blender
  data.py                              loads the POC 031 module (layout(), KEYBOARD) and the 3D fields
  caps/<family>.py                     one parametric cap generator per family (data-model.md §3)
  case/<kind>.py                       one builder per part kind (data-model.md §2)
  legends.py                           legend text and glyphs → curves → meshes or texture
  materials.py                         PBR materials from the 2D colours + the 3D material fields
  export.py                            GLB, USDZ, FBX, OBJ, STL with the scene conventions; extras on the key nodes
  check.py                             the checks of §6
  render.py                            the preview sheet per keyboard
scratch/<date>-keyboard3d/             build output (git-ignored), unless the owner wants the files committed
```

The POC 031 modules are plain Python with no third-party imports in `keyboards/` and `model.py`, so Blender's
Python can import them directly (`sys.path` to the POC folder). `svgkit.py` needs fontTools: install it into
Blender's Python or pre-export the legend paths to JSON from the system Python.

## 3. Building a keyboard

1. **Load** `KEYBOARD`, `layout()` and the 3D fields; convert mm to m at export only (model in mm, scale the root).
2. **Case**: extrude the `body` footprint (rounded rectangle) to the height profile; cut `well` pockets, `groove`s,
   `teeth`, `grille` / `vents` slots with booleans (or build the shell from the profile and add the cuts as
   separate geometry when booleans are slow; 600+ Quorum slots argue for an array of instanced cutters, applied
   once); add `hump`, `boss`, `steps`, `plate`, `fins`, `insert`, `tab`, `led`; fillet with a bevel modifier
   (segments 2–3 at LOD0).
3. **Caps**: for each key, the family builds the cap in the key's rectangle (or the L polygon): the skirt as a loft
   from the base outline to the top face outline, the top face with its dish, fillets. One mesh per cap; identical
   caps share mesh data (instancing) and differ in their legends.
4. **Legends**: each legend → its SVG path (`svgkit.text_path`, `glyph_shape`) → Blender curve → mesh, placed on
   the face (projected onto the dish or the slope), 0.02 mm proud, its own ink material; or baked into the cap's
   base-colour texture for the LODs (open question 2).
5. **Decals**: the case print (`logo` without relief, `stripe`, `ring`, `fddpanel`) as thin meshes 0.02 mm proud or
   one baked texture over the case's top UVs.
6. **Nodes and extras**: `kbd_<id>` / `case`, `keys/key_<id>` with the pivot at the top face centre, the legends
   parented to their key, the collision box `UCX_key_<id>`; the custom properties of FR9 from the key's data.
7. **LODs**: decimate the case and caps (LOD1 ≈ 50 %, LOD2 ≈ 15 %); LOD1+ legends from the baked texture.
8. **Bake**: ambient occlusion and the normal map of the moulded relief into the ORM / normal textures, one set per
   keyboard (Quest).
9. **Export** every requested format; **render** the preview sheet.

## 4. Materials

Metal-rough PBR only (NFR2). Starting values, to be checked against the photos (the owner rejects shine):

| Material | Roughness | Metallic | Notes |
|---|---|---|---|
| matte case plastic (ABS) | 0.65–0.75 | 0 | a fine stipple as a normal map (the 48K, the Spectrum+) |
| satin cap plastic | 0.5–0.6 | 0 | the Amstrad, Soviet caps |
| rubber mat (48K) | 0.85 | 0 | subsurface off for real-time; base colour blue-grey |
| aluminium plate (48K) / fins (128) | 0.35–0.45 | 1 | anodised black fins: base colour near black |
| clear window (Datacorder) | 0.05 | 0 | transmission 0.9, IOR 1.49; smoked tint |
| printed ink | as the surface under it | 0 | |
| LED | 0.3 | 0 | emissive off by default, a consumer turns it on |

Base colours are the 2D colours (sRGB hex), converted to linear in the export. The 2D pictures fake light with
gradients; the 3D base colour is the flat colour of the part, not a 2D gradient stop.

## 5. Exports

| Format | Exporter | Settings |
|---|---|---|
| GLB | `bpy.ops.export_scene.gltf` | `export_format='GLB'`, `export_extras=True` (the key properties), `export_yup=True`, Draco off by default (UE Interchange reads it, Quick Look does not need GLB) |
| USDZ | `bpy.ops.wm.usd_export` (`.usdz`) | Y up, metres; check Quick Look; custom properties as `customData` |
| FBX | `bpy.ops.export_scene.fbx` | `axis_up='Z'` for UE, `apply_unit_scale`, `use_custom_props=True`, the `UCX_` meshes, LOD groups as `_LOD0..2` |
| OBJ | `bpy.ops.wm.obj_export` | with MTL; one object per node |
| STL | `bpy.ops.wm.stl_export` | per part, no legends; watertight case |

## 6. Checks

- **Key set**: the key nodes equal `keys.json` (ids, `zx` / `pc` / `line`, count, twins); every key has a mapping.
- **Footprint**: an orthographic top render overlaid on the POC 031 Qt render at the same scale: each key's top face
  centre within 0.5 mm; the case outline within 0.5 mm.
- **Budgets**: triangles per LOD and texture sizes against NFR1.
- **Validity**: the Khronos glTF validator (`gltf_validator`), `usdchecker` on the USDZ, a UE import of the FBX in a
  test project (manual until there is a UE CI).
- **Determinism**: build twice, compare the files byte for byte.
- **Look**: the preview sheet next to the reference photos of [references.md](references.md), shown to the owner.

## 7. Phases and estimates

| Phase | What | Estimate |
|---|---|---|
| 0 | The owner's answers (requirements §9), the measurements of the first keyboard | — |
| 1 | The framework: data loading, the scene conventions, export of all formats, the checks; one keyboard end to end (proposal: `zx48k`, the simplest case and one cap family) | 2–3 days |
| 2 | The cap families one by one (`rubber`, `amstrad`, `soviet`, `next`, `plus`, `mech`), each with its first keyboard | 0.5–1 day each |
| 3 | The cases with their detail (the Datacorder, the heatsink, the vents, the wedge of the Next) | 0.5–1.5 days each |
| 4 | LODs, baking, the Quest budget, the UE import test with `ZXKeyComponent` | 2 days |
| 5 | The keyboards that POC 031 adds later (Orel, Profi, the PC keyboards) | per keyboard |

The cases, not the caps, are most of the work: the 2D pictures fake their relief with gradients, the 3D needs the
real shapes and heights.

## 8. Risks

| Risk | Mitigation |
|---|---|
| Heights and profiles unknown (only top views measured) | the measurements of keyboards.md §4 first; the owner's machines; mark every guess UNVERIFIED |
| Sculpted cap profiles are hard in bpy | prototype the family in build123d and import the mesh; or a lofted profile with a few control curves |
| Legend geometry blows the triangle budget (88 keys × 6 legends) | geometry at LOD0 only, baked texture at LOD1+ (open question 2) |
| Booleans on hundreds of vent slots are slow or break | instanced cutters applied once; or the slots as separate dark-floored pockets |
| A material looks right in Cycles and shiny in UE | check in UE early (phase 1), keep roughness high |
| The 2D data changes under the 3D build | the 3D reads the modules, never copies numbers; the checks of §6 catch drift |
