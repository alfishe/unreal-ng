"""ZX Spectrum Next, the white edition: the same case, keyboard and legends as keyboards/zxnext.py in white plastic,
white two-tone caps with black print, the "sinclair" wordmark moulded white on white (its relief's flanks read as a
thin grey contour), "ZX Spectrum Next" in red, the rainbow inserts at the right end as on the black one.

Sources: references/next.md §8 (the white edition); the owner's photo references/photos/next/next-white-top-owner.jpg
(2026-10-21, a 3/4 top view: the colours sampled and read from it, the geometry is the black one's). UNVERIFIED: the
exact whites (a phone photo under room light), whether BLACK on Ø is a black box with white print (it reads so), the
LED's colour when off.
"""

from keyboards import zxnext

KEYBOARD = {**zxnext.KEYBOARD, "id": "zxnextwhite", "name": "ZX Spectrum Next (white)", "highlights": 0.5,
            "next": {"frame": [(0, "#f6f6f4"), (0.12, "#e9e9e6"), (0.85, "#dcdcd8"), (1, "#c9c9c5")], "gloss": 0.12,
                     "sheen": 0.05, "relief": (1.03, 0.95), "rim": 0.25, "edge": "#9a9b98", "drop": 0.16}}

CASE = "#e3e3df"
CAP = "#e6e7e4"
INK = "#1c1d1f"


def layout():
    return zxnext.build(case=CASE, cap=CAP, ink=INK, well="#a9aaa7",
                        deck={"back": "#7d7e7b", "rim": 0.3, "shadow": 0.25},
                        wordmark=(CASE, {"emboss": True, "shadow": 0.22, "lit": 0.6, "edge": "#8c8a8a"}),
                        name="#c41f2b",
                        grille={"slot": "#a3a4a0", "lit": 0.6}, groove={"dark": "#b4b5b1"}, led="#a8181c",
                        tab="#dadad6")
