"""Side-by-side video writer (ffmpeg): raw | processed | class map, nearest-neighbour upscale."""
import subprocess

import numpy as np


class SideBySide:
    def __init__(self, path, h, w, scale=2, fps=50, panels=3):
        self.scale, self.h, self.w = scale, h, w
        out_w, out_h = w * panels * scale, h * scale
        args = ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                "-s", f"{out_w}x{out_h}", "-r", str(fps), "-i", "-"]
        if path.endswith(".gif"):
            args += ["-vf", "split[a][b];[a]palettegen=max_colors=256[p];[b][p]paletteuse=dither=none", path]
        else:
            args += ["-c:v", "libx264", "-crf", "0", "-preset", "veryfast", "-pix_fmt", "yuv444p", path]
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE)

    def add(self, *panels):
        row = np.concatenate(panels, axis=1)
        row = row.repeat(self.scale, axis=0).repeat(self.scale, axis=1)
        self.proc.stdin.write(np.ascontiguousarray(row).tobytes())

    def close(self):
        self.proc.stdin.close()
        self.proc.wait()
