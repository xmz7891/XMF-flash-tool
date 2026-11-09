"""Generate neutral light/dark window backdrops for XMF Flash Tool.

The UI keeps a strictly monochrome palette (light / dark), so the backdrop is a
very quiet grey gradient with a soft top light, vignette and fine grain - no
colour glows.
"""
import os

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "resources", "images")
W, H = 1920, 1200


def render(top, bottom, light_center, light_amp, vignette, grain_sigma, rng):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    t = (yy / H)[..., None]
    base = np.array(top, dtype=np.float32) * (1 - t) + np.array(bottom, dtype=np.float32) * t

    # soft directional light from the top-left corner
    cx, cy = W * 0.32, -H * 0.15
    dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    falloff = np.exp(-2.0 * (dist / (H * 1.05)) ** 2)[..., None]
    base = base + falloff * light_amp

    # gentle vignette
    vx, vy = W / 2, H / 2
    d = np.sqrt(((xx - vx) / (W * 0.74)) ** 2 + ((yy - vy) / (H * 0.74)) ** 2)
    vig = np.clip(1.0 - vignette * d ** 1.5, 1.0 - vignette, 1.0)[..., None]
    base = base * vig

    grain = rng.normal(0.0, grain_sigma, (H, W, 1)).astype(np.float32)
    img = np.clip(base + grain, 0.0, 1.0)
    return (img * 255).round().astype(np.uint8)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    rng = np.random.default_rng(20261108)
    dark = render(top=(0.030, 0.030, 0.034), bottom=(0.055, 0.055, 0.062),
                  light_center=(0.5, 0.5, 0.5), light_amp=0.055,
                  vignette=0.32, grain_sigma=0.0045, rng=rng)
    Image.fromarray(dark, "RGB").save(os.path.join(OUT_DIR, "bg_dark.jpg"), quality=94, optimize=True)

    light = render(top=(0.966, 0.966, 0.972), bottom=(0.915, 0.915, 0.925),
                   light_center=(1.0, 1.0, 1.0), light_amp=0.035,
                   vignette=0.10, grain_sigma=0.0035, rng=rng)
    Image.fromarray(light, "RGB").save(os.path.join(OUT_DIR, "bg_light.jpg"), quality=94, optimize=True)

    for name in ("bg_dark.jpg", "bg_light.jpg"):
        path = os.path.join(OUT_DIR, name)
        print("wrote", path, os.path.getsize(path), "bytes")


if __name__ == "__main__":
    main()
