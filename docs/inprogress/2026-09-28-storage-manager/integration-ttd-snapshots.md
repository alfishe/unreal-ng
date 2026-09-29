# Integration: time travel and snapshots

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Reviewed |
| **Before this design** | research §3: disk / tape insert invalidates a TTD session, eject does not; WD1793 writes are replay barriers, uPD765 writes are not; ZX-Evo SD commands end a recording (E5); NeoGS (branch) keeps its SD protocol state in its blob, marks card writes as barriers, and refuses insert / eject while recording |

## 1. The problem

A checkpoint stores machine state, not media contents. A medium changes in two ways:
- **the user swaps it**: insert, eject, rescan, discard;
- **the guest writes to it**.

A replay or a restore is only right if every medium holds, at that moment, what it held when the
state was captured.

## 2. TTD v1 (today): media-agnostic

TTD v1 works only with the peripherals and media that are present. It loads and stores no media;
its sessions record at port level, so a recorded response never changes on replay. On top of that,
the media manager enforces two rules that keep a v1 session honest:

| Event | Rule |
|---|---|
| insert / eject / rescan / discard / save while recording | **refused** with `recording`, unless the caller asks to end the recording first (`endRecording`; the GUI asks the user). The media set is fixed for a recording's whole length (the NeoGS branch rule, now for every slot) |
| guest write to a medium | a replay barrier (`RecordExternalEvent(DiskWrite)`, at most one per slot per frame), from the manager's activity hook. A seek never crosses a write silently |
| peripheral protocol state (SD card, Z-Controller, ATA registers, FDC) | in the TTD blob of the device that owns the slot |

Changes this brings:
- ZX-Evo SD: E5's "the first SD command ends the recording" becomes "a write is a barrier", and
  the card and `ZControllerSpi` state join the ATM3 model state.
- Floppy and tape insert: today they invalidate the session; now they are refused while recording,
  or end it on request.
- +3 uPD765 writes get the barrier.
- IDE (IDE design §10.0): "the first command ends the recording" becomes "a write is a barrier".

## 3. TTD v2 and the universal snapshot: media at any moment

Both are addressed in their own designs and take media from the change layer's versions
([media-history-design.md](media-history-design.md) §7):
- **TTD v2**: each checkpoint references, per medium, the version current at that moment. A seek
  switches each medium to that version, so replay reads what the machine read the first time, and
  seeks cross writes freely. Tracked as roadmap **ST-6** and the "Media" item of
  [ttd-v2-migration/TODO.md](../2026-09-25-ttd-v2-migration/TODO.md).
- **Universal snapshot (UNS)**: the manifest's media section holds, per slot, the source reference
  (path, `ContentId`), format, access and the change layer at the snapshot's version: embedded
  pieces, or a reference to a spill file plus a version id. On load the manager compares it with
  what is inserted, **reports** mismatches ("expects EYEACHE.TRD in fdd.b, the drive is empty")
  and never swaps media by itself; the GUI offers "insert the expected media". Tracked as roadmap
  **UNS-6**.
- **SNA / Z80 / SZX**: no media section in the format; loading one reports nothing and changes no
  media.

## 4. Tests

- **Recording guard**: while recording, insert / eject return `recording`, and with `endRecording`
  they end the session and then apply.
- **Barriers**: a floppy write, an SD write and a +3 write each add exactly one barrier per frame,
  and a seek stops there.
- **ZX-Evo SD boot under recording**: a whole ERS SD boot records without ending the session;
  seeking back and forth before the first write replays exactly.
- **TTD v2 / UNS**: the storage-overlay conformance test of roadmap §8 (write, seek back before
  the write, reread → pre-write contents), and a UNS round trip with an SD card changed after the
  snapshot.
