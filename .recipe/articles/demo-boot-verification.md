# Article: Deterministic Demo Boot Verification

A worked workflow for the most common automation job: "does this image boot
to the same state every time?" — using
[autostart](../run/autostart-disk.md), [run control](../_common/setup.md),
screen digests and OCR.

Use cases: regression gates after core changes, triaging "demo X broke"
reports, batch-verification of a disk/tape library, model-comparison
(PENTAGON vs SCORPION behavior on the same image).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred for
> single-image checks — `load_software` + `control_execution` +
> `inspect_state`. Use [WebAPI](#webapi) for the scripted sweeps below (shell
> loops over many images) or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# one boot-and-digest cycle (the verification primitive):
load_software      {"path":"scratch/demo.trd","autostart":true}
control_execution  {"action":"run_frames","frames":300}      # fixed frame budget — deterministic
inspect_state      {"aspects":["screen_digest"]}             # → structuredContent digest
# repeat the cycle and compare the two digests; compare against the stored golden

# triage on failure:
inspect_state      {"aspects":["screen_ocr"]}                # what a human sees
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/catalog"}   # boot died in TR-DOS?

# model comparison — recreate the instance per model:
emulator_manage    {"action":"create","model":"SCORPION"}    # → fresh id, repeat the cycle
```

The bash function and the sweep loops below are WebAPI territory by nature
(shell pipelines over many images).

## WebAPI

The bash-heavy workflows below — the right choice per policy: they are
scripted pipelines (shell functions, sweep loops).

### The verification primitive

Two endpoints, two different questions:

| Endpoint | Question it answers | Stability |
|:--|:--|:--|
| `GET /state/screen/digest` | is the *rendered content* identical to run A? | byte-exact, deterministic |
| `GET /capture/ocr` | what does a human see on screen? | fuzzy — text only |

Golden pattern — digest after a fixed frame budget:

```bash
boot_and_digest() {   # $1 = image path, $2 = frames
  local IMG=$1 FRAMES=${2:-300}
  curl -s -X POST "$BASE/emulator/$EMU_ID/reset" >/dev/null
  curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" \
       -H 'Content-Type: application/json' \
       -d "{\"path\":\"$IMG\",\"autostart\":true}" | jq -e '.autostarted' >/dev/null \
    || { echo "AUTOSTART FAILED"; return 1; }
  curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" \
       -H 'Content-Type: application/json' \
       -d "{\"frames\":$FRAMES}" >/dev/null
  curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq -r .digest
}
```

`run_frames` is the key: a fixed emulated-frame budget makes wall-clock
speed (turbo, machine load) irrelevant — frame 300 is frame 300.

### Single-image regression gate

```bash
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
         -d '{"model":"PENTAGON"}' | jq -r .id)

D1=$(boot_and_digest scratch/demo.trd 300)
D2=$(boot_and_digest scratch/demo.trd 300)

[ "$D1" = "$D2" ] && echo "self-consistent" || { echo "NON-DETERMINISTIC"; exit 1; }

# compare against the stored golden digest from the last known-good build:
[ "$D1" = "$(cat scratch/demo.golden)" ] && echo "PASS" || echo "CHANGED — investigate"
# first run? record it:  echo "$D1" > scratch/demo.golden
```

### Interpreting failures

```bash
# What's actually on screen?
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq -r .text

# Boot died in TR-DOS? Compare catalog expectations:
curl -s "$BASE/emulator/$EMU_ID/disk/A/catalog" | jq '.file_count'

# Stuck loading? OCR shows "R Tape loading error" → media/tape problem,
# loader timeout → try more frames before declaring failure.
```

Failure taxonomy:

| Symptom | Likely cause | Next step |
|:--|:--|:--|
| `autostarted: false` | no `.B` files / non-TR-DOS image | [manual TR-DOS](../run/manual-trdos-run.md) |
| digest differs across self-runs | timing-dependent demo (raster effects) | larger frame budget; check known-flaky list |
| OCR shows TR-DOS `A>` prompt | boot.b crashed | [TTD the boot](../analysis/ttd-recording.md) |
| OCR shows loading stripes forever | disk read errors | [disk protection triage](disk-protection-triage.md) |
| black screen, PC in nowhere-land | wrong model for the demo | retry with the model the demo targets |

### Library sweep (many images)

```bash
mkdir -p scratch/boot-report
for IMG in scratch/disks/*.trd; do
  NAME=$(basename "$IMG")
  D=$(boot_and_digest "$IMG" 400) && STATUS=ok || STATUS=autostart-failed
  GOLDEN="scratch/goldens/$NAME.digest"
  if [ "$STATUS" = ok ] && [ -f "$GOLDEN" ]; then
    [ "$D" = "$(cat "$GOLDEN")" ] && VERDICT=same || VERDICT=changed
  elif [ "$STATUS" = ok ]; then
    echo "$D" > "$GOLDEN"; VERDICT=golden-created
  else
    VERDICT="$STATUS"
  fi
  printf '%-28s %s\n' "$NAME" "$VERDICT" | tee -a scratch/boot-report/sweep.txt
done
```

Reset-per-image keeps runs independent; the loop takes seconds per image
because autostart skips the tape-era boot animations.

### Model comparison

```bash
for MODEL in PENTAGON 128k SCORPION; do
  # recreate the instance per model (switching resets state)
  curl -s -X DELETE "$BASE/emulator/$EMU_ID" >/dev/null
  EMU_ID=$(curl -s -X POST "$BASE/emulator/start" \
       -H 'Content-Type: application/json' \
       -d "{\"model\":\"$MODEL\"}" | jq -r .id)
  echo "$MODEL: $(boot_and_digest scratch/demo.trd 300)"
done
```

Same digest across models = model-neutral demo; divergence = the demo pokes
model-specific hardware (paging ports, memory layout) — exactly what you
want to know before shipping model support.

### Making failures debuggable

When a digest changes, attach the evidence in the same run:

```bash
curl -s "$BASE/emulator/$EMU_ID/capture/screen?format=png" \
  | jq -r .data | base64 -d > scratch/boot-report/frame.png
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/start" >/dev/null   # if instrumented from the start
```

Recording TTD during the sweep costs memory; the cheaper default is: sweep
clean, and re-run only the changed image under TTD for the deep dive
([bug-hunt-ttd.md](bug-hunt-ttd.md)).

## Pitfalls

- **Frame budget too small**: some demos settle after long intros — a digest
  at frame 300 may capture mid-animation. Verify by budgeting twice (300 vs
  900) and checking the digests differ but stabilize.
- **Turbo mode changes timing-visible behavior** — pin the speed multiplier
  (or leave default) for goldens; any change invalidates them.
- **OCR is not a contract**: use it for humans, use digests for machines.
- **Flash-phase frames**: the ULA FLASH attribute toggles every ~16 frames
  and *is* part of the digest — either keep budgets clear of phase flips or
  accept both digests (run the budget twice, 8 frames apart, compare sets).
