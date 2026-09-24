# Flaky / Floating Sector Emulator

**Module**: `core/src/emulator/io/fdc/flakysectoremulator.h` (`FlakySectorEmulator`, header-only)
**Consumer**: `WD1793` (`core/src/emulator/io/fdc/wd1793.cpp`) — the only caller
**Underlying data**: `DiskImage::RawTrack` weak-bit bitmap (`core/src/emulator/io/fdc/diskimage.h`, pre-existing)

## 1. What problem this solves

A real WD1793 + drive reads whatever flux is physically on the disk. At a spot where the
flux transition is marginal — because the media is damaged, or because a copy-protection
scheme deliberately wrote it that way — different revolutions of the *same* disk decode
differently: an ID Address Mark that reads as sector 16 on one pass may fail to decode at
all on the next; a data byte may come back as `0x00` on one read and `0xF3` on another.
This is universally called a **weak**, **flaky**, or **floating** sector.

Copy-protection code exploits this directly: it reads a known-flaky spot two or more times
and expects to see the result *change*. A perfect digital copy is deterministic — the
"protection" byte is identical on every read — so the check fails and the game refuses to
run from a copy. On original media, the check passes because the drive really does see
different noise each time.

`unreal-ng`'s `WD1793` model was, until this module existed, **fully deterministic**: for a
given track, a given sector number always exists (or doesn't) and always contains the same
bytes, on every read, forever. A loader that relies on a floating sector to eventually
succeed (or on it never matching) sees the exact same failing result every attempt — so a
retry loop written to "keep trying until the flaky sector proves itself" never terminates
in emulation, even though it terminates quickly on real hardware. This is the mechanism
[behind the VORON1.FDI hang](../disasm/black-raven-voron-protection/README.md) analysis.

`FlakySectorEmulator` makes the FDC layer honor weak-bit metadata when it is present,
without touching behavior for the overwhelming majority of images that don't have any.

## 2. Where the weak-bit data comes from — and why FDI can't have it

`DiskImage::RawTrack` (`diskimage.h`) already carries an optional per-byte bitmap:

```cpp
bool hasWeakBits() const;
bool weakByte(size_t offset) const;
void setWeakByte(size_t offset, bool on);
```

This is **not new** — it exists so richer disk-image formats can round-trip the weak/fuzzy
information their own encoding carries. Four loaders already populate it:

| Format | How it captures flakiness | Where it's set |
|---|---|---|
| **SCP** (SuperCard Pro raw flux) | Captures **N full revolutions** of raw flux per track (`revolutions` field in the file header, typically 2–5). `LoaderSCP::mergeRevolutions` decodes each revolution independently, then takes a **per-byte majority vote** across revolutions; any byte where the revolutions didn't all agree is flagged weak. | `core/src/loaders/disk/loader_scp.cpp:106` (`mergeRevolutions`), applied at `:417` |
| **HFE** (HxC Floppy Emulator) | The HFE bitstream format has its own native **weak-range opcode** — the file itself declares "these bit-cell ranges are unreliable", no multi-revolution capture needed. | `core/src/loaders/disk/loader_hfe.cpp:334` |
| **DSK** (Extended CPC DSK) | The extended DSK spec allows a track to store **multiple raw copies of the same sector**; the loader diffs the copies byte-for-byte and flags any byte that differs between copies. | `core/src/loaders/disk/loader_dsk.cpp:261` |
| **UDI** (Ubik Disk Image) | **Supported since 2026-09-23** through the `UDIW` weak-bit map — a chunk in the trailer (the spec-legal comment area, CRC-covered) listing per-track weak byte ranges. Parse applies it to `RawTrack::_weak` and strips it from the preserved comment; save appends one record per maximal run of weak bytes, so weak bits round-trip losslessly. The reserved multi-revolution track type (`0x80\|n`) is still declined ("not supported", `loader_udi.cpp:155`). | `core/src/loaders/disk/loader_udi.cpp:207` (parse+apply), `:314` (serialize); [design + outcome](../inprogress/2026-09-22-udi-weak-bit-storage/design.md) |

**FDI cannot express any of this.** Per `docs/file-formats/disk-images/fdi.md`, an FDI track
is one fixed list of `(cylinder, head, sector, size, flags)` sector headers plus one fixed
data blob per sector — a single, static snapshot. There is no field for "this ID sometimes
reads as something else" or "this byte sometimes reads as something else": FDI has exactly
one opinion about every byte on the disk, forever. `VORON1.FDI`/`VORON2.FDI`
(`testdata/loaders/fdi/`) are ordinary FDI captures and therefore carry **zero** weak bits —
`hasWeakBits()` is `false` for every track in them, on both fixtures, today. This is a
format limitation, not a bug: nothing short of re-dumping the original media with a
flux-capable tool (KryoFlux/Greaseweasel → SCP, or a multi-revolution DSK rip, or an
HFE dump with native weak ranges) and converting that into a format above can recover
the weak-bit map for these two disks. UDI is *not* such a format (see the table above).
Until such a capture exists, `FlakySectorEmulator` has nothing to act on for VORON1/VORON2
and the loader will behave exactly as before this change. An alternative that needs no new
hardware — authoring weak marks into a copy of the existing `VORON1.UDI` via the designed
`UDIW` chunk — is specified in the
[UDI weak-bit storage design](../inprogress/2026-09-22-udi-weak-bit-storage/design.md).

## 3. What the module does

Two independent behaviors, each gated on `track.hasWeakBits()` so solid tracks (which is
every FDI/TRD/SCL/MGT/HOBETA image, and any track of any format with no weak bytes) pay a
single boolean check and then run exactly the old code path.

### 3.1 `findSector` — revolution-aware ID search

```cpp
static DiskImage::Sector* findSector(DiskImage::Track& track, int cyl, int side,
                                      uint8_t number, size_t fromOffset, size_t revolution);
```

Mirrors `DiskImage::Track::findSector(cyl, side, number, fromOffset)` (the rotational search
a WD1793 Type II command performs) but additionally skips any candidate sector whose ID
Address Mark carries a weak byte and is not "visible" on the given `revolution`
(`isIdamVisible`). A weak IDAM is visible on 1 out of every 4 revolutions — a deterministic
stand-in for a real drive occasionally resolving a marginal transition, chosen to land
inside the WD1793 datasheet's own "up to 4–5 revolutions" ID-search window, so a command
that keeps retrying across revolutions eventually succeeds instead of failing forever.

`WD1793::locateSectorForType2` is the only caller:

```cpp
DiskImage::Sector* sector = track->hasWeakBits()
    ? FlakySectorEmulator::findSector(*track, cylinder, side, sectorNo, headByteOffset(*track), _indexPulseCounter)
    : track->findSector(cylinder, side, sectorNo, headByteOffset(*track));
```

`_indexPulseCounter` is WD1793's own revolution counter (already maintained for the
existing 4/5-revolution RNF timeout logic — see `docs/WD1793/WD1793_Timeouts.md`), so no new
state was added to the controller for this.

### 3.2 `mutateWeakDataByte` — flaky data bytes

```cpp
static void mutateWeakDataByte(const DiskImage::Track& track, size_t offset,
                                uint64_t clock, uint8_t& value);
```

Called from `WD1793::processReadByte` (the per-byte sector-data transfer state) right after
a byte is fetched from the track's raw stream:

```cpp
_dataRegister = *(_rawDataBuffer++);
_bytesToRead--;

if (_currentReadTrack && _currentReadTrack->hasWeakBits())
{
    const size_t offset = static_cast<size_t>((_rawDataBuffer - 1) - _currentReadTrack->rawData());
    FlakySectorEmulator::mutateWeakDataByte(*_currentReadTrack, offset, _time, _dataRegister);
}
```

If `offset` is not marked weak, `value` is untouched. If it is, `value` is overwritten with
a byte derived from a hash of the FDC's T-state clock (`_time`) and `offset` — a different,
"random-looking" byte on every read attempt, matching a real drive returning different
garbage from a physically marginal spot each pass.

`WD1793::_currentReadTrack` is a new member (set in `cmdReadSector`'s FIFO callback,
alongside the pre-existing `_currentSector`) purely so `processReadByte` — which only ever
saw a raw `uint8_t*` into track data before this — can ask the owning `Track` whether the
byte it just fetched is weak.

## 4. Determinism (why this is safe under TTD)

Nothing in this module calls `rand()`, reads wall-clock time, or touches any state outside
what the CPU/FDC already deterministically compute. Every "random" decision is a bit-mixing
hash (`* 2654435761u`, xor-shift, `* 0x85ebca6b`, xor-shift — a standard 32-bit finalizer)
of values that are themselves part of the recorded, replayed simulation state:

- ID visibility: `sector.idamOffset` (static, from the image) and `revolution`
  (`WD1793::_indexPulseCounter`, deterministically driven by elapsed T-states).
- Data byte content: `offset` (static) and `clock` (`WD1793::_time`, the FDC's T-state
  clock).

A Time-Travel-Debugging replay re-executes the same instruction stream, which reproduces
the same `_indexPulseCounter`/`_time` sequence, which reproduces bit-for-bit the same
"flaky" values every time. This is required for the `docs/debugger/ttd` recording/replay
guarantees and is the reason this module does not use `std::rand`, a PRNG with external
seeding, or anything else that could diverge between a live run and its replay.

## 5. What this does *not* do (scope / follow-ups)

- **No FDI extension.** FDI still cannot represent weak sectors on disk; nothing was added
  to `loader_fdi.{h,cpp}`. Wiring this module end-to-end for a real title requires a
  weak-bit-carrying capture (SCP, HFE, or extended DSK — not UDI, which cannot store weak
  bits at all) of the actual media, not just the code path. (The UDI leg of this is
designed: the `UDIW` trailer chunk —
[design](../inprogress/2026-09-22-udi-weak-bit-storage/design.md) — will let a
  hand-authored copy carry the marks without a re-dump.)
- **No write-side flakiness.** A protection that formats/writes a sector and expects the
  write not to "stick" (a physically damaged, not just marginal, spot) is not modeled;
  `WD1793::writeDataRegister`/the write-byte state machine are untouched. If a real title
  needs that, it belongs in this same module as a parallel `mutateWeakWriteByte`-style
  entry point, gated the same way.
- **Duty cycle is fixed (1-in-4).** The 1-in-4 visibility ratio for weak IDAMs is a single
  reasonable constant, not derived from any measured protection. If a title needs a
  different apparent "success rate", that ratio (or a per-track/per-sector value carried by
  a richer format) is the natural next knob — kept inside `FlakySectorEmulator` so `WD1793`
  never needs to know the difference. The designed `UDIW` chunk reserves a `flags` byte per
  record earmarked for exactly this knob (design §4.2).

## 6. Tests

`FlakySectorEmulator` has no dedicated unit test file yet — it was verified by (1) building
`core` and `core-tests` clean, and (2) running the full WD1793/DiskImage/loader test suites
(`LoaderFDI_Test`, `LoaderDSK_Test`, `LoaderSCP_Test`, `LoaderHFE_Test`, `LoaderUDI_Test`,
`Wd1793*`, `DiskImage*` — 249 tests) unchanged and green, confirming the new code path is
inert for every existing fixture (none of which carry weak bits except the SCP/HFE/DSK
round-trip fixtures, which only exercise bitmap *storage*, not FDC read behavior — `UDI`
is included in that test run for coverage but never carries weak bits, see §2). A
synthetic-track unit test exercising `findSector`/`mutateWeakDataByte` directly (one weak
IDAM, assert visible on revolutions 0/4/8 and RNF elsewhere; one weak data byte, assert it
varies with `_time`) is a good next addition alongside any real weak-sector fixture. That
test is now specified (synthetic weak-IDAM sector visible on revolutions 0/4/8 and RNF
elsewhere; weak data byte varies with `_time`; solid track byte-identical) as step 2 of the
[UDI weak-bit storage design](../inprogress/2026-09-22-udi-weak-bit-storage/design.md) §7.
