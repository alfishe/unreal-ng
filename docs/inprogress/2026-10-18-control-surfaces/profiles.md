# Control surfaces: context layers and defaults

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Design, for review |
| **Proposals** | P6, P9 in [README.md](README.md#proposals-for-the-owner-2026-10-18) |
| **Extends** | [mapping-and-profiles.md §2 layers](../2026-10-17-controller-manager/mapping-and-profiles.md#2-routing-and-layers), [§7 profile format](../2026-10-17-controller-manager/mapping-and-profiles.md#7-profile-format), [debug-and-ttd.md §4](../2026-10-17-controller-manager/debug-and-ttd.md#4-default-debug-profiles) |

## Contents

- [1. Context layers](#1-context-layers)
- [2. The Surface Dial profile](#2-the-surface-dial-profile)
- [3. The Stream Deck](#3-the-stream-deck)
- [4. Selection](#4-selection)

## 1. Context layers

No new profile kind. A `debug` profile ([D8](../2026-10-17-controller-manager/README.md#decisions-2026-10-17):
separate from play, never per game) gets layers whose names start with `@`:

- `@ttd` is active while the instance's context is `ttd`; `@debug.*` while it starts with `debug.`; the longest match
  wins.
- A context layer sits above the profile's `debug` layer and overrides only the controls it names (the shift-layer
  rule, [mapping-and-profiles §2](../2026-10-17-controller-manager/mapping-and-profiles.md#2-routing-and-layers)).
- It is switched by the context ([architecture §6](architecture.md#6-contexts-and-the-followed-instance)), never by a layer key.
- Feedback per context goes into the profile's existing `haptics` section (today game haptics: sources and rules,
  `core/controllers/src/haptics/hapticprofile.h`); in a debug profile it is keyed by context layer:
  `haptics: { "@ttd": { steps, clicks, marks, min_interval_ms } }` (marks: mark kind → pattern; interval default 40).
  One concept for "what the device feels", no reserved key among the controls.
- For a device in role `debugger-remote` the `debug` layer is its base, so its context layers apply without a held
  layer key.

Parser changes (`core/controllers/src/profiles/profile.cpp`): a debug profile accepts `@` layers besides `debug`
(today it rejects every other layer); an `action` target passes its `curve` object (an option the parser keeps and
does not act on yet, reserved for the scrub curve) to the action as `params`; `haptics` of a debug profile takes the
per-layer form above.

## 2. The Surface Dial profile

```yaml
schema: 1
kind: debug
id: builtin/debug/surface-dial
label: Surface Dial
applies_to: { families: [surface-dial] }
layers:
  debug:
    push:  { action: run.pause_toggle }
  "@ttd":
    knob:  { action: ttd.seek_by, curve: { unit: frames, min: 1, max: 100 } }
  "@ttd.timecode":
    knob:  { action: ttd.seek_by, curve: { unit: seconds, min: 0.02, max: 2.0 } }
  "@debug.*":
    knob:  { action: run.step_by }
haptics:
  "@ttd":          { steps: 36, clicks: true,  marks: { second: tick, ten_seconds: double, bookmark: double, edge: buzz, barrier: buzz } }
  "@ttd.timecode": { steps: 36, clicks: false, marks: { second: tick, ten_seconds: double, edge: buzz, barrier: buzz } }
  "@debug.*":      { steps: 36, clicks: true }
```

- `curve` is the scrub curve of [actions.md §2](actions.md#2-ttdseek_by) (`unit`, `min`, `max`, optional `rate_lo`,
  `rate_hi`); it reaches the action as `params`.
- `clicks: false` in time code: the hand feels only the marks (Q-6). Steps stay 36 so a turn keeps its resolution
  (on the Dial steps and clicks share one register, [devices.md §1.2](devices.md#12-haptics)).
- Outside every context layer (`play`) the knob is unbound; the driver turns clicks off.
- **Debug layer switch**: debug profiles apply only while `Controllers/DebugLayer` is on, and it is off by default
  (`unreal-qt/src/controllers/hostcontrollers.cpp`); non-player roles are set in the dialog's Advanced mode only
  ([ui-ux U6](../2026-10-17-controller-manager/ui-ux.md)). So in v1 the owner turns the debug layer on and sets the
  Dial's role once in Advanced; whether a Dial should start in role `debugger-remote` by itself is Q-8.

## 3. The Stream Deck

The Stream Deck keeps its scenario (`streamdeck-bridge`): pages, faces, dials, the strip. In v1:

| Concern | Where |
|:--|:--|
| Which context a page means | the scenario: `"context": "ttd.timecode"` on a menu node; entering it calls `ctx.set` |
| What a Stream Deck dial does | the scenario: an `unreal_action` `{id: ttd.seek_by, value: {unit, min, max}}`; the bridge adds `d` and `rate` |
| The strip readout | `ttd.position` readouts |
| The Dial | its profile above |

So the Dial and the first Stream Deck dial share the action and its curve (both 1 → 100 frames per step, 4/s → 40/s),
while the scenario keeps its three ranges (1 → 100, 10 → 1000, 100 → 10 000).

## 4. Selection

As every debug profile ([mapping-and-profiles §6.2](../2026-10-17-controller-manager/mapping-and-profiles.md#62-layering)):
built-in ⊂ community ⊂ user, global → family → device; no machine or game level. Shipped in
`data/controllers/profiles/debug/surface-dial.yaml` (the first file of that directory), compiled in like the others.
