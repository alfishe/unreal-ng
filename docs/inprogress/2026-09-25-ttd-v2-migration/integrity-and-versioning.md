# Integrity and versioning — open investigation

**Status: open. Nothing here is decided.** The CRC and versioning parts of
[target-architecture.md](target-architecture.md) §7–8 and
[requirements.md](requirements.md) FR-11…FR-13 state a *direction*; the
mechanisms need a separate investigation because of the nuances below.

What is established so far:

- Today most random corruption goes unnoticed (bit-flip experiment,
  [current-state.md](current-state.md) §9): only 4 KB page pieces carry a
  CRC32C, it is checked lazily at restore, and a mismatch zero-fills silently.
- The format has never been versioned: "amended in place, re-record the
  fixtures" ([current-state.md](current-state.md) §5).
- Checksums would not have caught any TTD defect found in September 2026
  (all were determinism or emulator bugs producing well-formed state).

---

## 1. Integrity (CRC) — questions to settle

### I-1. What does a checksum cover?

| Option | Detects | Costs / limits |
|---|---|---|
| CRC of **stored bytes** (compressed payload) | storage and transfer damage; can be checked without decompressing | does not catch codec bugs |
| CRC of **reconstructed content** (today's per-piece CRC) | storage damage *and* codec / XOR-chain bugs | checking requires decoding the whole chain; a damaged link is reported at every later piece of the chain |
| Both | everything above, and tells the two apart | two checks, more bytes |

### I-2. Granularity

Per piece (4 KB), per checkpoint, per chunk (~50 frames), per stream, whole
file. Finer = better localisation of damage and less data lost to one flip,
coarser = less overhead and fewer fields to get wrong. Device blobs, the
reference table and CPU/chipset have **no** checksum today — which unit covers
them?

### I-3. When is it checked?

- **Eager at load**: simple and definitive, but conflicts with lazy loading of
  large files (PR-12: first seek ≤ 2 s on 1 GB) and with disk mode, where
  most of the file is never read in a session.
- **Lazy at access**: cheap, but damage surfaces in the middle of a seek or a
  reverse search, possibly deep inside a replay.
- **Background verify** after load, with results feeding the status — a
  third option, adds a thread and a state.

### I-4. What happens on a failure?

- Refuse the whole file vs open it with **holes** (damaged frame ranges
  reported, seeks into them refused).
- **Damage propagates**: an XOR-chain link, a shared "unchanged" device blob or
  a copy-on-write reference block is used by every later frame until the next
  full copy. A single flip can invalidate a long range, not one frame. The
  failure model has to compute that range.
- The write journal and coverage index are accelerators: damage there could
  disable the accelerator (fall back to replay) instead of failing the load.
- Every surface (WebAPI, MCP, CLI, DeZog, Qt) needs a representation of
  "partially damaged session".

### I-5. Streaming, crash safety and mutable fields

- Header fields known only at the end (counts, end frame, cue-table offset)
  cannot be covered by a CRC written at the start → move them to the footer,
  or rewrite + re-checksum the header at finalize (not atomic).
- Torn writes: a chunk half on disk after power loss; the CRC tells it apart
  only if the header with the length is itself protected.
- `fsync` policy vs recording overhead (PR-1); what "crash safe" guarantees
  without it.
- Finalize = rename of a temporary file vs in-place footer append.

### I-6. In-memory checks

The hot tier today recomputes CRC32C for every captured piece (cost inside
PR-1) and verifies at every restore (cost inside PR-5). Is that worth keeping
once files have their own checks? It catches codec bugs and memory corruption,
not storage damage. Measure its share of capture and seek time.

### I-7. Algorithm and platforms

CRC32C is hardware-accelerated on x86-64 (SSE4.2) and ARMv8; the table
fallback is ~10× slower. Check the fallback path on every supported compiler
(MSVC, MinGW, GCC, Clang) and the cost on a 1 GB file. zstd's own frame
checksum: redundant with a stored-bytes CRC, not with a content CRC. 32 bits
detect random damage, not tampering (not a goal).

### I-8. Tooling

`ttd.ksy`, the Python analyzer (`validate` must check exactly what the C++
reader checks) and the fuzz test (QR-4) define "detected"; they must be
specified together with the mechanism.

## 2. Versioning — questions to settle

### V-1. What carries a version?

Container framing, each stream's record layout, CPU and chipset structs,
region table, configuration fingerprint, each device blob layout. One number
per item, or a single format version plus per-device layout versions? Every
number is a compatibility promise that needs old fixtures and tests kept
forever.

### V-2. Compatibility directions

| Direction | Question |
|---|---|
| New reader, old file | Always readable? Up to which age? Converted on load, or restored with degraded devices? |
| Old reader, new file | Refuse, or read what it understands? |
| Device layout changed | Per-device upgrade function (old layout → new), or refuse that device (degraded restore)? Who writes and tests the converters? |

### V-3. Skippable vs required content

"Readers skip unknown streams" is safe only for content that is not needed for
exact restore. A new stream that *is* required (for example a new memory
region type) must make old readers refuse. Needs a per-stream "required"
flag (PNG's critical / ancillary chunks are the model), and the same question
for device blobs.

### V-4. Behaviour vs layout

The file layout can stay identical while emulation changes (the 2026-09-25
timed AY writes changed what a replay produces, not the blob size alone).
A file then loads and restores exactly, but **replay** from it no longer
matches the recording. Is the emulator's behaviour version part of the
configuration fingerprint? What does a user see — "restore exact, replay may
differ"?

### V-5. Fixed-size contract

Device state is fixed-size per device today (`TTDStateSize()`), and the loader
rejects size mismatches. Versioned layouts mean sizes change between versions;
the size check becomes a version check plus a per-version size.

### V-6. When the promise starts

Until v2 ships, amending in place is cheap (re-record the fixtures). From the
first versioned release, every change costs a version bump, a converter or a
documented refusal, and fixtures of every supported old version stay in the
repo. Decide exactly which release starts the promise, and whether pre-release
v2 builds are exempt.

### V-7. Identifiers

`PeripheralId` and region ids become permanent once files are versioned
(the id-9 collision between the three branches shows the risk). Rules for
allocation, retirement (never reuse) and renames.

## 3. How to settle it

1. **Survey prior art** for the same problems: PNG (critical / ancillary
   chunks, per-chunk CRC), Matroska/EBML (versioned elements, unknown-element
   skipping, crash-tolerant streaming), SQLite (header change counters,
   journals), RZX and the snapshot formats already in the emulator
   (`.sna`/`.z80`/`.szx` versioning in practice), rr / UndoDB trace formats,
   **MAME save states** (per-device registration and version stamps) and
   **QEMU VMState** (`version_id` / `minimum_version_id`, optional
   subsections that old readers skip — the same problem as V-2 / V-3).
2. **Measure** on the fixture corpus and a 1 GB synthetic session: CRC cost
   per granularity (I-2, I-7), in-memory CRC share of capture and seek (I-6),
   damage-propagation ranges for real XOR chains and shared blobs (I-4).
3. **Enumerate failure scenarios** with the expected behaviour for each: bit
   flip per stream, truncation, torn chunk, missing footer, old file / new
   reader, new file / old reader, device layout change, emulator behaviour
   change.
4. **Write the decision** as a separate design section (target-architecture
   §7–8 replaced), with the fuzz test and compatibility fixtures that enforce it.

Until then, only the uncontroversial fix went ahead (2026-09-25): the Python
analyzer compares the stored piece CRC instead of overwriting it (a bug in the
tool, independent of any design choice), and the false "writer stores 0"
comments in `ttd_format.py`, `ttd.ksy` and `timetravelmanager.cpp` that
justified the missing compare are corrected.
