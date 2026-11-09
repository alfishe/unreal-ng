# 048 — analysis

Numbers: [measurements-2026-11-07-m3-ultra.md](measurements-2026-11-07-m3-ultra.md).

## Why the gain is almost the whole bound

- **The handoff is a copy and a lock, once per piece.** A queued piece copies its 4 KB difference and 4 KB content
  into a job (about 8 KB) and pushes it under one mutex; the worker is woken once per capture. 4.3 pieces per
  capture cost 1.3-1.8 µs, copy included, against 50 µs of zstd taken away.
- **Applying the answer is cheap.** The worker returns the packed bytes; the emulation thread picks the encoding with
  today's rule and appends to the arena (0.4-0.6 µs per capture). The store stays single-threaded: no lock on any
  read path.
- **Nobody waits.** The next capture needs a piece's previous version only for its depth and base, which the pending
  version already carries; the worker finishes a capture's pieces within the frame (50 µs of work in a 700 µs frame),
  so 99 % of captures find nothing to wait on. Only a read of the history (decode for a seek, payload bytes, heap)
  settles, and right after a capture that is at most one capture's worth.

## Why idle machines see nothing

An idle capture packs one or two pieces (2-14 µs of zstd in a 164-6763 µs frame). The most the worker can remove is
that, 0.2-4.4 % of the frame, while single benchmark rounds of one configuration spread over 7-14 points. Neither the
gain nor a loss is visible; the counters show the handoff for them at the same 1-2 µs per capture.

## Why the seek case got faster, not slower

A seek every 10 frames waits for the last capture's pieces about 0.77 times per seek, 5.5 µs, under 1 % of a seek.
The 9 frames between seeks keep their 50 µs saving each, so the net is a gain of about 4 %.

## Why the queue needs a bound

The first capture of a session offers the whole memory (1.43 MB of jobs on the fixture, about 170 at 8.3 KB each). Bounded to
96 pieces, the rest of that capture packs on the emulation thread as today; steady-state captures stay far below the
bound, so the bound costs nothing measurable (−6.4 % unbounded against −6.7 % bounded, different rounds).

## Correctness: the free order

The pending version holds a reference on its base until the worker's answer is applied (a Full answer drops it).
Freed later than the synchronous store would free it, the base's id is reused in another order, and the stores differ
in ids though not in content. Applying the pending versions before a base whose only other holders they are is freed
gives the same id sequence; the byte-identity test checks ids, encodings, payloads and CRCs over 400 frames.
