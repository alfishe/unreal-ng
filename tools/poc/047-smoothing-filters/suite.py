"""Generic suite: every scene of scenes.py through filters, scored against the truth per element (POC 047).

    python3 suite.py <filters binary> <filter,...> <work dir> [scene,...]
    SUITE_SCORE_ONLY=1 python3 suite.py ...   # score the outputs a previous run left

Per element and filter: the share of wrong pixels over all refreshes (a moving or static layer: the truth at the
refresh's time, rounded either way on each axis, or its slowest look-alike when the tiled bitmap hides the motion; the
best one of these for the whole element, a rigid layer is shown at one place; an animated one in frame k's shape or
k + 1's), or of wrong 8 x 8 cells (flux: a cell
must be one source frame whole). "screen" is the plain paper, "border" the border: motion smeared onto them counts
there.
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import scenes as S  # noqa: E402

ROM = os.path.join(os.path.dirname(__file__), "../../../data/rom/48.rom")
DISPLAY_HZ = 60.0


def times(refreshes):
    period, frame = 1e9 / DISPLAY_HZ, 1e9 / S.SOURCE_HZ
    return [(3e6 + round(period) * r) / round(frame) for r in range(refreshes)]


def score(elements, source, outputs):
    """{filter: wrong share per row} with rows: border, screen, then the elements"""
    n = len(elements) + 2
    flux = [False, False] + [isinstance(e, S.Flux) for e in elements]
    wrong = {f: np.zeros(n) for f in outputs}
    total = np.zeros(n)
    refreshes = min(len(o) for o in outputs.values())
    roundings = [(rx, ry) for rx in ("floor", "ceil") for ry in ("floor", "ceil")]  # either way on each axis
    seen = [e.seen() if isinstance(e, S.Layer) else None for e in elements]
    cells_h, cells_w = S.H // 8, S.W // 8
    for r, t in enumerate(times(refreshes)):
        k = int(t)
        _, ids = S.render(elements, t, "floor")
        shapes = (0, 1) if any(isinstance(e, S.Layer) and e.frames is not None for e in elements) else (0,)
        plain = [S.render(elements, t, rounding, None, shape)[0] for rounding in roundings for shape in shapes]
        # a rigid element is shown at one place: its candidates (a rounding, a look-alike velocity) whole
        candidates = {}
        for e, element in enumerate(elements):
            if isinstance(element, S.Layer):
                vs = seen[e] + ([element.v] if element.v not in seen[e] else [])
                own_shapes = (0, 1) if element.frames is not None else (0,)  # animated: frame k's shape or k + 1's
                candidates[e] = [S.render(elements, t, rounding, {e: v}, {e: shape})
                                 for v in vs for rounding in roundings for shape in own_shapes]
        a, b = source[k, :, :, :3], source[k + 1, :, :, :3]
        _, ids_a = S.render(elements, k)
        _, ids_b = S.render(elements, k + 1)
        row = ids.astype(np.int32) + 1  # -1 border -> 0, 0 screen -> 1, element e -> e + 1
        # flux cells: any pixel of a flux element in either frame or the truth
        is_flux = np.zeros((S.H, S.W), bool)
        cell_row = np.zeros((cells_h, cells_w), np.int32)
        for e, element in enumerate(elements):
            if isinstance(element, S.Flux):
                m = (ids == e + 1) | (ids_a == e + 1) | (ids_b == e + 1)
                c = m.reshape(cells_h, 8, cells_w, 8).any((1, 3))
                cell_row[c] = e + 2
                is_flux |= c.repeat(8, 0).repeat(8, 1)
        for e in range(n):
            if not flux[e]:
                total[e] += ((row == e) & ~is_flux).sum()
            else:
                total[e] += (cell_row == e).sum()
        for f, out in outputs.items():
            o = out[r, :, :, :3]
            good = np.zeros(ids.shape, bool)
            for truth in plain:
                good |= (o == truth).all(-1)
            bad = ~good & ~is_flux
            counts = np.bincount(row[bad], minlength=n)[:n]
            for e, cands in candidates.items():
                own = np.zeros(ids.shape, bool)
                for _, cid in cands:
                    own |= cid == e + 1
                own &= ~is_flux
                counts[e + 2] = min(((o != img).any(-1) & own).sum() for img, _ in cands)
            wrong[f] += counts
            cell_a = (o == a).all(-1).reshape(cells_h, 8, cells_w, 8).all((1, 3))
            cell_b = (o == b).all(-1).reshape(cells_h, 8, cells_w, 8).all((1, 3))
            bad_cells = (cell_row > 0) & ~cell_a & ~cell_b
            wrong[f] += np.bincount(cell_row[bad_cells], minlength=n)[:n] * flux_mask(flux, n)
    return {f: wrong[f] / np.maximum(total, 1) for f in outputs}, total


def flux_mask(flux, n):
    return np.array([1 if flux[e] else 0 for e in range(n)])


def main():
    binary, filters, work = sys.argv[1], sys.argv[2], sys.argv[3]
    all_scenes = S.scenes()
    names = sys.argv[4].split(",") if len(sys.argv) > 4 else list(all_scenes)
    fs = filters.split(",")
    summary = {f: [] for f in fs}
    for name in names:
        elements = all_scenes[name]
        src = os.path.join(work, name + ".rgba")
        if not os.path.exists(src):
            S.write(elements, src)
        out = os.path.join(work, name)
        os.makedirs(out, exist_ok=True)
        env = dict(os.environ, FILTERS_ONLY=filters, FILTERS_RAW="1")
        if not os.environ.get("SUITE_SCORE_ONLY"):  # set: score the outputs a previous run left
            subprocess.run([binary, src, str(S.W), str(S.H), str(S.FRAMES), out, ROM, str(S.SOURCE_HZ), str(DISPLAY_HZ)],
                           env=env, check=True, stdout=subprocess.DEVNULL)
        for f in os.listdir(out):
            if f.endswith(".mp4"):
                os.remove(os.path.join(out, f))
        source = np.memmap(src, np.uint8, "r").reshape(-1, S.H, S.W, 4)
        outputs = {f: np.memmap(os.path.join(out, f + ".rgba"), np.uint8, "r").reshape(-1, S.H, S.W, 4) for f in fs}
        shares, total = score(elements, source, outputs)
        rows = ["border", "screen"] + [e.name for e in elements]
        kinds = ["", ""] + ["flux" if isinstance(e, S.Flux) else "still" if e.v == (0, 0) else "move"
                            for e in elements]
        print(f"\n{name}" + "".join(f"{f[-14:]:>15}" for f in fs))
        for i, label in enumerate(rows):
            if total[i] == 0:
                continue
            print(f"  {label[:20]:20} {kinds[i]:5}" + "".join(f"{100 * shares[f][i]:14.2f}%" for f in fs))
        for f in fs:
            summary[f].append(shares[f][2:].mean())
    print("\nmean over the elements" + "".join(f"{f[-14:]:>15}" for f in fs))
    for name, i in zip(names, range(len(names))):
        print(f"  {name:26}" + "".join(f"{100 * summary[f][i]:14.2f}%" for f in fs))


if __name__ == "__main__":
    main()
