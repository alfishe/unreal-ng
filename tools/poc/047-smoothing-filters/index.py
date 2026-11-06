#!/usr/bin/env python3
"""POC 047: index.html over the rendered clips: the long smooth scrollers first, each clip's grids looping, the single
filters as links."""
import os, sys
d = sys.argv[1] if len(sys.argv) > 1 else "scratch/2026-10-28-poc-039"
long_clips = ["hysteria", "cobra", "zynaps", "slapfight", "moonstrike", "scramblespirits", "uridiumplus", "flyingshark"]
short_clips = ["aleste1", "s4b-2", "across-the-edge-second", "vibrations", "earshaver-1", "insult-3", "action",
               "greenberet-300", "garfield-300", "thundercats-300", "Dizzy_X", "dargonscrypt-300", "eyeache1",
               "7threality", "darkwingduck", "scroller_by_demarche"]


def grids(c):
    p = os.path.join(d, c)
    if not os.path.isdir(p):
        return []
    g = sorted(f for f in os.listdir(p) if f.startswith("grid") and f.endswith(".mp4"))
    return sorted(g, key=lambda f: f != "grid-short.mp4")


html = ["<!doctype html><meta charset=utf-8><title>POC 047 smoothing filters</title>",
        "<style>body{background:#111;color:#ddd;font:14px sans-serif}video{width:100%;max-width:2560px;"
        "image-rendering:pixelated}a{color:#8cf}</style>",
        "<h1>POC 047: 50 &rarr; 60 Hz smoothing filters</h1><p>Every picture exactly on its 60 Hz refresh (no "
        "presenter timing). <b>grid-short</b>: off, blend-narrow, interpolate, interp-rows at 3x; <b>grid</b>: all "
        "12 filters. Play full screen; the links open one filter at 3x.</p>"]
for title, clips in (("30 s, smooth scrolling games (RZX Archive)", long_clips), ("4-10 s clips", short_clips)):
    html.append(f"<h1>{title}</h1>")
    for c in clips:
        g = grids(c)
        if not g:
            continue
        html.append(f"<h2 id='{c}'>{c}</h2>")
        for f in g:
            html.append(f"<p>{f[:-4]}</p><video src='{c}/{f}' controls loop muted preload=metadata></video>")
        singles = " ".join(f"<a href='{c}/{f}'>{f[:-4]}</a>" for f in sorted(os.listdir(os.path.join(d, c)))
                           if f.endswith(".mp4") and not f.startswith("grid"))
        if singles:
            html.append(f"<p>{singles}</p>")
open(os.path.join(d, "index.html"), "w").write("\n".join(html))
print(os.path.join(d, "index.html"))
