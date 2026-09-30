"""Lossless clip format (prototype of test-plan.md §2 "clip file").

Directory layout:
  clip.json        {"shape": [H, W], "chunk": N, "start": first_frame, "end": last_frame,
                    "palette": {"<index>": "#rrggbb", ...}}
  meta.jsonl       one line per frame: {"frame", "active_screen", "border", "p7FFD"}
  chunk_NNNN.zst   zstd of N consecutive HxW uint8 palette-index planes

Each plane is the frame's FINAL beam-rendered picture (border and multicolor
included), i.e. what the emulator displays for that frame.
"""
import json
import os

import numpy as np
import zstandard


class ClipWriter:
    def __init__(self, path, chunk=500):
        os.makedirs(path, exist_ok=True)
        self.path, self.chunk = path, chunk
        self.palette = {}          # packed 0xRRGGBB -> index, in order of appearance
        self.buf, self.chunk_id = [], 0
        self.meta = open(os.path.join(path, "meta.jsonl"), "w")
        self.cctx = zstandard.ZstdCompressor(level=10)
        self.shape, self.first, self.last = None, None, None

    def add(self, rgb, meta):
        packed = (rgb[..., 0].astype(np.uint32) << 16) | (rgb[..., 1].astype(np.uint32) << 8) | rgb[..., 2]
        uniq, inv = np.unique(packed, return_inverse=True)
        lut = np.empty(len(uniq), np.uint8)
        for i, c in enumerate(uniq.tolist()):
            lut[i] = self.palette.setdefault(c, len(self.palette))
        plane = lut[inv].reshape(packed.shape)
        self.shape = plane.shape
        self.first = meta["frame"] if self.first is None else self.first
        self.last = meta["frame"]
        self.buf.append(plane)
        self.meta.write(json.dumps(meta) + "\n")
        if len(self.buf) == self.chunk:
            self._flush()

    def _flush(self):
        if self.buf:
            with open(os.path.join(self.path, f"chunk_{self.chunk_id:04d}.zst"), "wb") as f:
                f.write(self.cctx.compress(np.stack(self.buf).tobytes()))
            self.buf, self.chunk_id = [], self.chunk_id + 1

    def close(self):
        self._flush()
        self.meta.close()
        with open(os.path.join(self.path, "clip.json"), "w") as f:
            json.dump({"shape": list(self.shape), "chunk": self.chunk, "start": self.first, "end": self.last,
                       "palette": {str(v): f"#{k:06x}" for k, v in self.palette.items()}}, f, indent=1)


class Clip:
    def __init__(self, path):
        self.path = path
        self.info = json.load(open(os.path.join(path, "clip.json")))
        self.h, self.w = self.info["shape"]
        self.chunk = self.info["chunk"]
        self.meta = [json.loads(l) for l in open(os.path.join(path, "meta.jsonl"))]
        self.palette_rgb = np.zeros((256, 3), np.uint8)
        for k, v in self.info["palette"].items():
            self.palette_rgb[int(k)] = [int(v[1:3], 16), int(v[3:5], 16), int(v[5:7], 16)]
        self._dctx = zstandard.ZstdDecompressor()
        self._cache_id, self._cache = None, None

    def __len__(self):
        return len(self.meta)

    def _load(self, cid):
        if cid != self._cache_id:
            raw = self._dctx.decompress(open(os.path.join(self.path, f"chunk_{cid:04d}.zst"), "rb").read())
            self._cache, self._cache_id = np.frombuffer(raw, np.uint8).reshape(-1, self.h, self.w), cid
        return self._cache

    def plane(self, i):
        """Palette-index plane of clip frame i (0-based)."""
        return self._load(i // self.chunk)[i % self.chunk]

    def rgb(self, i):
        return self.palette_rgb[self.plane(i)]

    def planes(self, start=0, end=None):
        for i in range(start, len(self) if end is None else end):
            yield i, self.plane(i)

    def index_of_frame(self, frame):
        return frame - self.meta[0]["frame"]


class ClipV2:
    """Clip written by the core (POST /ttd/export-clip, format "unreal-ng-clip" v2):
    rgba_NNNN.zst (H x W x 4 RGBA8 per frame), planeb_NNNN.zst (H x W uint16 plane B:
    attr[0:7] color[8:11] ink[12] role[13:14], see p0a-plane-b.md), meta.jsonl, clip.json."""

    ROLE_SCREEN, ROLE_BORDER = 1, 2

    def __init__(self, path):
        self.path = path
        self.info = json.load(open(os.path.join(path, "clip.json")))
        assert self.info.get("format") == "unreal-ng-clip" and self.info.get("version") == 2, self.info
        self.h, self.w = self.info["height"], self.info["width"]
        self.chunk = self.info["chunk"]
        self.has_planeb = "planeb" in self.info["planes"]
        self.zx_palette = self._palette16()
        self.meta = [json.loads(l) for l in open(os.path.join(path, "meta.jsonl"))]
        self._dctx = zstandard.ZstdDecompressor()
        self._cache = {}

    def __len__(self):
        return len(self.meta)

    def _palette16(self):
        """16 x 3 uint8: the ZX colors plane B's color indices were drawn in (the
        emulator's live palette at export, index = bright * 8 + color). An index the
        clip never draws is null in clip.json and black here (nothing can need it)."""
        if not self.has_planeb:
            return None
        entries = self.info.get("palette16")
        if entries is None:
            raise ValueError(f"{self.path}: clip.json has no palette16 - exported before the core wrote it; "
                             f"run capture/add_palette16.py on it")
        return np.array([[int(v[1:3], 16), int(v[3:5], 16), int(v[5:7], 16)] if v else [0, 0, 0]
                         for v in entries], np.uint8)

    def _load(self, kind, cid, dtype, shape):
        key = (kind, cid)
        if key not in self._cache:
            if len(self._cache) > 4:
                self._cache.clear()
            raw = self._dctx.decompress(open(os.path.join(self.path, f"{kind}_{cid:04d}.zst"), "rb").read())
            self._cache[key] = np.frombuffer(raw, dtype).reshape((-1,) + shape)
        return self._cache[key]

    def rgb(self, i):
        return self._load("rgba", i // self.chunk, np.uint8, (self.h, self.w, 4))[i % self.chunk][..., :3]

    def planeb(self, i):
        return self._load("planeb", i // self.chunk, "<u2", (self.h, self.w))[i % self.chunk]

    def plane(self, i):
        """Color index plane (0..15) from plane B - the same role the palette-index plane plays in Clip."""
        return ((self.planeb(i) >> 8) & 0xF).astype(np.uint8)

    @staticmethod
    def decode_planeb(pb):
        """-> attr, color, ink, role arrays"""
        return {"attr": (pb & 0xFF).astype(np.uint8), "color": ((pb >> 8) & 0xF).astype(np.uint8),
                "ink": ((pb >> 12) & 1).astype(bool), "role": ((pb >> 13) & 3).astype(np.uint8)}

    def index_of_frame(self, frame):
        return frame - self.meta[0]["frame"]
