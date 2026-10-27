#!/usr/bin/env python3
"""Next3D Elite Wireframe (Olifiers/Next3D elite.nex): the picture the program's own source defines, against what the emulator draws.

Start the application with the NEX (`unreal-qt elite.nex`, offscreen is fine), wait ~15 s, run this. It reads the Layer 2 image of the last
frame (capture/screen?layer=layer2&size=pixel) and checks the pixels main.c fixes in the steady state: the background (ClearLayer2 colour 15), the
three vertices of the triangle drawn at (150,20) (200,30) (90,40) which the program plots with colour 0 after filling, interiors of the filled
triangles (colour 56) and a point outside everything. Colours are the default Layer 2 palette (RRRGGGBB). Exit code 0: all match.
"""
import base64
import json
import sys
import urllib.request

BASE = "http://localhost:8090/api/v1/emulator"


def get(path, emulator_id):
    return json.load(urllib.request.urlopen(f"{BASE}/{emulator_id}{path}"))


def rgb(index):
    def expand(value, bits):
        return round(value * 255 / ((1 << bits) - 1))
    return (expand((index >> 5) & 7, 3), expand((index >> 2) & 7, 3), expand(index & 3, 2))


def main():
    emulator_id = json.load(urllib.request.urlopen(BASE))["emulators"][0]["id"]
    layer = get("/capture/screen?layer=layer2&format=rgba&size=pixel", emulator_id)
    image, width = base64.b64decode(layer["data"]), layer["width"]

    def pixel(x, y):  # Layer 2 pixel (x, y) is grid pixel (x + 32, y + 32): the paper starts 32 pixels in
        offset = ((y + 32) * width + (x + 32)) * 4
        return tuple(image[offset:offset + 3])

    checks = [("background", (5, 5), 15), ("vertex (150,20)", (150, 20), 0), ("vertex (200,30)", (200, 30), 0), ("vertex (90,40)", (90, 40), 0),
              ("triangle A interior", (147, 30), 56), ("triangle B interior", (113, 120), 56), ("triangle C interior", (198, 184), 56),
              ("outside everything", (230, 100), 15)]
    bad = 0
    for name, (x, y), index in checks:
        got, want = pixel(x, y), rgb(index)
        print(f"{'ok  ' if got == want else 'DIFF'} {name} ({x},{y}) {got} expected {want}")
        bad += got != want
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
