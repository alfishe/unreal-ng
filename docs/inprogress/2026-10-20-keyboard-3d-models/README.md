# Keyboard 3D models: scripted photoreal models of every emulated keyboard

| | |
|---|---|
| **Date** | 2026-10-20 |
| **Status** | Hand-off package for a separate project; no code yet |
| **Builds on** | POC 031 [authentic OSD keyboards](../../../tools/poc/031-authentic-osd-keyboards/README.md) (the 2D data and references), the Unreal Engine integration ([ue-integration-design.md](../2026-09-19-unrealengine-integration/ue-integration-design.md), G5: "physical ZX keyboard ... Desktop and VR input paths") |
| **Consumers** | AR / VR integration later: the UE plugin's keyboard mesh with pressable keys (OpenXR, PC VR, Quest), Apple AR (USDZ), any DCC tool |

POC 031 draws each emulated machine's keyboard as a photoreal **vector picture** for the on-screen keyboard. Every
picture comes from one data model per keyboard: the case parts and the keys in mm, the cap materials and real colours,
the legends by role, and the matrix (or PC) key each cap presses. A style only draws that data.

This project adds a **3D style** to the same data. A script builds a real-scale, photoreal, game-ready 3D model of
each keyboard, with one node per key that knows what it presses. The model exports to any format and is rebuilt
from the data after any change. The models are needed later for AR / VR: a keyboard on a desk in a headset, its keys
pressed by the user's fingers or by the emulator's own key state.

It is a separate project, not part of POC 031 (owner 2026-10-20). The OSD needs vector SVG, so a 3D render cannot
replace it. 3D needs data the 2D pictures never needed (heights, cap profiles, the case's side and front), which
means new measurements. The one rule for POC 031 meanwhile: geometry stays in the keyboard data, not in the styles,
so the 3D generator can read it as it is.

## Documents

| Document | What |
|:--|:--|
| [requirements.md](requirements.md) | **Start here.** Goals, consumers, FR / NFR, formats, scene conventions, non-goals, acceptance, open questions |
| [keyboards.md](keyboards.md) | The inventory: every model of the emulator, its keyboard, the 2D status, cap family, case features to model, what is measured and what is not |
| [references.md](references.md) | The gallery of reference photos per keyboard, with what each photo is good for in 3D (top, side, profile, colour) |
| [data-model.md](data-model.md) | What the POC 031 model gives (Model, Part, Key, Legend, keys.json), how each part kind and cap material maps to 3D, the fields 3D adds |
| [pipeline.md](pipeline.md) | The proposed generator: Blender (bpy) headless, parametric cap families, case builders, legends, PBR materials, export, validation, phases and estimates |

## Starting points

- The data: `tools/poc/031-authentic-osd-keyboards/keyboards/<id>.py` (`layout()` → `Model`), `model.py`;
  `keyboards/out/<id>.keys.json` (the keys, their rectangles and mappings).
- The 2D renders to match, colours and placement: `tools/poc/031-authentic-osd-keyboards/atlas/<id>.photo.atlas.png`.
- The references: `tools/poc/031-authentic-osd-keyboards/references/` (`sinclair.md`, `clones.md`, `next.md`,
  `pc.md`, `photos/`).
- Blender 5.2.2 LTS is installed on the dev Mac (`/opt/homebrew/bin/blender`, `blender -b -P script.py`).

## Owner rules that carry over from POC 031

- **Matte, dim highlights.** The owner rejected strong sheen three times as kitsch ("опять переборщил с засветкой").
  Roughness stays high, specular low; compare with the photos before showing.
- **Never draw dirt, wear, patina or uneven tint**, even when the reference photo shows them ("никаких дефектов мы
  не рисуем"). Take the geometry and the clean colour from the photo.
- **Correct references.** A colour or a size comes from a photo shot straight down in neutral light, or from the
  owner's photos. A front shot in yellow light is not for colours or depths.
- **A comment or a forum post is not proof.** Every dimension states its source; an estimate is marked
  **UNVERIFIED**.
- The owner judges each model when it is ready. Show one keyboard at a time.
