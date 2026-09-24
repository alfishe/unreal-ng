# Article: Physical Copy-Protection Forensics (Low-Level RE)

Worked workflow for investigating *physical* disk protections: tracks
formatted by non-WD1793 procedures or exotic formatters (odd CHRN geometry,
turbo layouts, FM), deliberately damaged/flaky track fragments (weak flux
positions that read differently every revolution), and the copy-check code
that consumes them. Companion pieces: loader-side triage in
[disk-protection-triage.md](disk-protection-triage.md), loader detection in
[nonstandard-loader.md](../analysis/nonstandard-loader.md), image authoring in
[author-udi-images.md](../media/author-udi-images.md).

Ground truth on real protections:
`docs/disasm/black-raven-voron-protection/` (worked case study with snapshot
and artifacts).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred for the
> emulator-side stages — `load_software`, `analyze_performance` porttrace,
> `time_travel` bookmarks, `invoke_api` the raw views. Use [WebAPI](#webapi)
> for the host-side Python stages and scripted differential tests or when MCP
> is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# stage 2 — raw inspection through the emulator:
load_software      {"path":"scratch/title.udi"}                # .udi is a load_software extension
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/sector/40/0/1/raw"}
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/track/40/0/raw"}
#   → clock_bitmap_base64 rides inside the track response

# stage 3 — differential test (behavioral weak-bit detection):
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/disk/A/insert",
                   "body":{"path":"scratch/title.hfe"}}       # HFE: NOT a load_software extension — use the router
control_execution  {"action":"run_frames","frames":600}
inspect_state      {"aspects":["screen_digest","screen_ocr"]}  # protection passes?
analyze_performance {"action":"porttrace","frames":600,"limit":40}   # repeated reads of one sector ID

# stage 4 — reverse the check (TTD-record the failing boot first):
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/find-last","body":{"addr":63,"access":"io"}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/reverse-continue","body":{"pcs":[33156]}}
time_travel        {"action":"bookmark_add","label":"before-check"}
```

Stages 1 and 5 are host-side (`diskinfo`, `diskconverter`) — they live in
the WebAPI section below, which also carries the curl forms of stages 2-4.

## WebAPI

The walkthrough below — right choice for the host-side stages (Python
tools) and scripted differential comparisons.

### Stage 1 — structural scan, host side (no emulator)

```bash
python3 tools/diskinfo/diskinfo.py scratch/title.udi --tracks
python3 tools/diskinfo/diskinfo.py scratch/title.udi --dump idam | head -40
```

Read out, in order:

- **per-track notes** — `id-mismatch` (CHRN claims a track/head the sector is
  not physically on: the classic trick), `deleted` (F8 marks), `data-crc
  errors` (deliberate or damage), `id-only` (ID field with no data field)
- **`protection analysis` verdict** — structural signals only, title-agnostic:
  partial non-TR-DOS reformatting (system track standard, data tracks
  reformatted), `R >= 0xC0` sector numbers, duplicate IDs, over-length
  geometry (>80 cylinders), unformatted holes inside the used area
- **`filesystems` block** — a valid TR-DOS catalog *plus* weird tracks is the
  usual protected-original shape; no catalog at all suggests a pure custom
  format (CP/M, iS-DOS, NedoOS, or proprietary)

Unformatted holes and weak signals are ambiguous — protection or media
damage; Stage 2-3 disambiguate behaviorally.

### Stage 2 — raw inspection through the emulator

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" \
     -H 'Content-Type: application/json' -d '{"path":"scratch/title.udi"}' | jq .

# decoded sector vs raw stream: the raw view keeps gaps, marks and interleave
curl -s "$BASE/emulator/$EMU_ID/disk/A/sector/40/0/1/raw" | jq '{raw_size, has_data}'
curl -s "$BASE/emulator/$EMU_ID/disk/A/track/40/0/raw" \
  | jq '{track_size, clock: (.clock_bitmap_base64 | length)}'
```

`clock_bitmap_base64` exposes the missing-clock marks — dense/unusual clock
patterns mark non-standard sync writing (formatted by something other than a
plain WD1793 format command). Compare a suspect track against a known-clean
system track on the same disk.

### Stage 3 — flaky-sector behavior (the heart of physical protection)

A flaky/weak sector reads *differently on different revolutions*: the ID
address mark may decode or not, data bytes come back as varying garbage. The
emulator implements this (`FlakySectorEmulator`): a weak IDAM is visible on
about 1 of 4 revolutions (so a persistent sector search eventually finds it,
per the WD1793 datasheet window), and weak data bytes mutate per read —
deterministically, from emulated state, so a TTD replay reproduces the exact
same "random" sequence.

Weak bits exist only in **SCP/HFE/extended-DSK** images (the loaders mark
them); TRD/FDI/UDI have no field for them — FDI never did, and UDI drops them
on save with a warning.

**The differential test** proves reliance on flakiness:

```bash
# 1. original HFE (weak bits live) — protection passes / title boots
# 2. same disk re-saved as UDI through the emulator: insert the HFE, then
#    Qt Disk → Save As .udi — the save warns "UDI cannot store weak bits"
#    (tools/diskconverter cannot do this conversion: it reads SCL/TRD/FDI/UDI/TD0 only)
# 3. insert the UDI copy and boot again
```

If behavior changes between the two, the check *reads a known-flaky spot and
expects it to vary* — a bit-perfect copy is deterministic and fails. You can
watch it happen: [fdc-only port trace](../analysis/port-trace.md) shows the repeated reads
of the same sector ID with different returned data, from the
[custom loader's PC range](../analysis/nonstandard-loader.md).

There is no WebAPI field for the weak bitmap — detect weak bits *behaviorally*
(reads vary across revolutions) or host-side in the SCP/HFE source.

### Stage 4 — reverse the check

With the check-loop PC from the trace:

```bash
# (TTD-record the failing boot first — analysis/ttd-recording.md)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/find-last" \
     -H 'Content-Type: application/json' \
     -d '{"addr": 0x3F, "access": "io"}' | jq .     # last FDC track-register touch
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-continue" \
     -H 'Content-Type: application/json' -d '{"pcs":[<check-loop-pc>]}' | jq .
```

Reconstruct: which sector it read twice, where it compares the two results,
which branch takes the "this is a copy" path. Bookmarks at each discovery
(`before-check`, `after-first-read`) keep the timeline navigable; full method
in [ttd-reverse-debugging.md](../analysis/ttd-reverse-debugging.md) and the
worked [bug-hunt-ttd.md](bug-hunt-ttd.md).

### Stage 5 — archive what you learned

Produce the faithful image and document the protection:

1. Pick the format by capability matrix
   ([author-udi-images.md](../media/author-udi-images.md)) — UDI for
   everything except weak bits; SCP/HFE when flakiness is load-bearing.
2. For UDI: author host-side (planted CHRN/CRC/F8/ID-only signals), or
   in-emulator via real formatter code (byte-authentic raw stream + clock
   marks).
3. Re-verify: `diskinfo` must report exactly the planted signals; the title
   must boot through the whole protection path; TTD-record one clean pass and
   keep the `.ttd` next to the image as evidence.

## Pitfalls

- **"Protected" vs "damaged"**: identical structural signals — only behavior
  (does the original title boot from the faithful image?) separates them.
- **UDI save silently degrades flaky tracks** (warning only) — always test the
  *converted* image, not just the conversion exit code.
- **Deterministic flakiness**: same instruction stream ⇒ same "random" reads;
  do not expect `rand()`-style change under TTD replay — vary the revolution
  count instead.
- **Multi-revolution UDI type** (`0x80|n`) is reserved but unimplemented —
  loaders reject it; do not hand-craft it.
- **Sector-level views hide raw tricks**: `diskinfo` re-derives sync marks and
  skips the clock bitmap; gap games and exotic sync layouts are only visible
  in `track/.../raw` + clock bitmap.
