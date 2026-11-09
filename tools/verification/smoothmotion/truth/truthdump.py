"""Dumps the truth of a generic scene (scenes.py) for truthscore: <out>/<scene>.rgba (300 source frames at 50 Hz),
<scene>.truth (rgb + element id per pixel, 500 Hz from 1 ms: frame j at j / 500 + 0.001 s), <scene>.eyes (per source frame, per
element: the seen velocity in px/s or -, the eye following a layer)"""
import sys
import numpy as np
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[1]))
import scenes as S

out, name = sys.argv[1], sys.argv[2]
el = S.scenes()[name]
# Periodic layers move at the velocity the frames show (the slowest alias), what the eye follows
# Truth set i (argv[3], 0 by default): between two frames the changing content (animated shapes, flux) shows frame
# k's (set 0) or k + 1's (set 1), both right; the score takes the better per element
alias = int(sys.argv[3]) if len(sys.argv) > 3 else 0
suffix = f".a{alias}" if alias else ""
seen = {e: x.seen()[0] for e, x in enumerate(el) if isinstance(x, S.Layer)}
eyes_only = len(sys.argv) > 4 and sys.argv[4] == "eyes"
if not alias and not eyes_only:
    S.write(el, f"{out}/{name}.rgba")
n = int((S.FRAMES - 2) / S.SOURCE_HZ * 500)
with open(f"{out}/{name}{suffix}.truth", "ab" if eyes_only else "wb") as f:
    for j in range(0 if eyes_only else n):
        img, ids = S.render(el, (j / 500 + 0.001) * S.SOURCE_HZ, velocities=seen, rounding="round", shapes=alias)
        f.write(img.tobytes())
        f.write(np.clip(ids, -1, 127).astype(np.int8).tobytes())
with open(f"{out}/{name}{suffix}.eyes", "w") as f:
    f.write(f"{len(el)}\n")
    for k in range(S.FRAMES):
        row = []
        for e in el:
            # A layer whose motion ties with another (half its period a frame: a 1 px dither, dots moving half their
            # spacing) is a counterphase flicker, no motion the eye can follow: only fixation scores it
            if not isinstance(e, S.Layer) or e.v == (0, 0) or len(e.seen()) > 1:
                row.append("-")
                continue
            slope = (e.moves(k + 6) - e.moves(k - 6)) / 12 * S.SOURCE_HZ
            vx, vy = e.seen()[0]
            row.append(f"{vy * slope:.4f},{vx * slope:.4f}" if slope else "-")
        f.write(" ".join(row) + "\n")
