"""The truth of a generic scene (scenes.py) refreshed at 60 Hz from 3 ms: the best a 60 Hz display can show"""
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
with open(f"{out}/{name}{suffix}-truth60.rgba", "wb") as f:
    for i in range(int((S.FRAMES - 2) / S.SOURCE_HZ * 60)):
        img, _ = S.render(el, (0.003 + i / 60) * S.SOURCE_HZ, velocities=seen, rounding="round", shapes=alias)
        f.write(np.concatenate([img, np.full((S.H, S.W, 1), 255, np.uint8)], 2).tobytes())
