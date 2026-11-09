"""Generate the low-contrast 'aurora' backdrop used by the XMF glass UI.

Pure numpy/Pillow, no network. Run with the bundled runtime:
    <dsh python> scripts/gen_aurora.py
"""
import os

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "resources", "images")
W, H = 1920, 1200


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # vertical base gradient
    top = np.array([0.043, 0.055, 0.082], dtype=np.float32)
    bottom = np.array([0.016, 0.020, 0.030], dtype=np.float32)
    t = (yy / H)[..., None]
    base = top * (1 - t) + bottom * t

    def additive(cx, cy, r, color, amp):
        d = np.sqrt((xx - cx * W) ** 2 + (yy - cy * H) ** 2)
        falloff = np.exp(-2.6 * (d / (r * H)) ** 2).astype(np.float32)
        glow = (np.array(color, dtype=np.float32) * amp)[None, None, :] * falloff[..., None]
        return glow

    glows = np.zeros((H, W, 3), dtype=np.float32)
    glows += additive(0.18, 0.10, 0.62, (0.30, 0.42, 1.00), 0.85)   # indigo top-left
    glows += additive(0.86, 0.04, 0.55, (0.62, 0.34, 1.00), 0.55)   # violet top-right
    glows += additive(0.78, 0.98, 0.60, (0.10, 0.75, 0.82), 0.22)   # teal bottom-right
    glows += additive(0.10, 0.92, 0.55, (0.22, 0.42, 0.95), 0.20)   # blue bottom-left
    glows += additive(0.50, 0.45, 0.75, (0.35, 0.55, 1.00), 0.10)   # centre haze

    img = 1 - (1 - base) * (1 - glows)  # screen blend

    # vignette
    cx, cy = W / 2, H / 2
    dist = np.sqrt(((xx - cx) / (W * 0.72)) ** 2 + ((yy - cy) / (H * 0.72)) ** 2)
    vign = np.clip(1.0 - 0.55 * dist ** 1.6, 0.55, 1.0).astype(np.float32)
    img *= vign[..., None]

    # film grain
    rng = np.random.default_rng(20261108)
    grain = rng.normal(0.0, 0.0055, (H, W, 1)).astype(np.float32)
    img = np.clip(img + grain, 0.0, 1.0)

    out = (img ** (1 / 1.02) * 255).round().astype(np.uint8)
    path = os.path.join(OUT_DIR, "aurora.jpg")
    Image.fromarray(out, "RGB").save(path, quality=94, optimize=True)
    print("wrote", path, os.path.getsize(path), "bytes")

    # 2x2 noise tile for subtle texture overlays
    tile = rng.integers(0, 255, (96, 96, 1), dtype=np.uint8)
    tile = np.repeat(tile, 3, axis=2)
    alpha = np.full((96, 96, 1), 10, dtype=np.uint8)
    rgba = np.concatenate([tile, alpha], axis=2)
    noise_path = os.path.join(OUT_DIR, "noise.png")
    Image.fromarray(rgba, "RGBA").save(noise_path, optimize=True)
    print("wrote", noise_path, os.path.getsize(noise_path), "bytes")


if __name__ == "__main__":
    main()
