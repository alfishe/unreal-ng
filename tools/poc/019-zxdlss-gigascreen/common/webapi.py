"""Minimal unreal-ng WebAPI client (stdlib only)."""
import base64
import io
import json
import urllib.request

import numpy as np
from PIL import Image

DEFAULT_BASE = "http://localhost:8090/api/v1"


class Emulator:
    def __init__(self, emulator_id, base=DEFAULT_BASE):
        self.id = emulator_id
        self.base = base
        self.url = f"{base}/emulator/{emulator_id}"

    def call(self, method, path, body=None, timeout=120):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.url + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read())

    def frame_rgb(self):
        """Current output framebuffer, full frame with border, as HxWx3 uint8."""
        data = self.call("GET", "/capture/screen?format=png&mode=full")["data"]
        return np.asarray(Image.open(io.BytesIO(base64.b64decode(data))).convert("RGB"))

    @staticmethod
    def create(model="PENTAGON", ram_kb=512, base=DEFAULT_BASE):
        req = urllib.request.Request(f"{base}/emulator/start", method="POST",
                                     data=json.dumps({"model": model, "ram_size": ram_kb}).encode(),
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=30) as r:
            return Emulator(json.loads(r.read())["id"], base)
