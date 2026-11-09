"""Fetch UI assets (Lucide SVG icons + Inter font) for XMF Flash Tool.

Icons: MIT licensed, from https://lucide.dev (via jsDelivr).
Font : Inter, SIL Open Font License 1.1 (https://rsms.me/inter).
"""
import os
import sys
import urllib.request
import concurrent.futures

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "resources", "icons")
FONT_DIR = os.path.join(ROOT, "resources", "fonts")

ICONS = """
home box zap layout-dashboard cpu microchip shield-check layers sun key-round
lock-keyhole terminal download-cloud list-checks smartphone usb settings file-text
package disc-3 hard-drive database split merge refresh-cw download upload folder-open
save copy trash-2 power play square check x alert-triangle info circle-help search
chevron-right chevron-down chevron-left arrow-up-right arrow-right more-horizontal
external-link monitor-smartphone tablet watch tv gamepad-2 battery-charging wifi
bluetooth memory-stick scan-line rotate-ccw flask-conical wrench shield shield-alert
unlock lock activity gauge server radio plug plug-zap cable barcode qr-code git-branch
archive file-archive file-up file-down hash loader-circle rocket sparkles star clock
history filter sliders-horizontal eye eye-off wand-2 crosshair target map-pin globe
cloud cloud-download cloud-off triangle-alert badge-check badge-alert circle-check-big
octagon-x package-check hard-drive-download hard-drive-upload arrow-left right-left
folder-cog file-cog calendar-rotate fingerprint scan-face circle-dot list-filter
sort-desc unfold-vertical maximize-2 minimize-2 x-circle check-circle-2 info-zap
""".split()

COLORS = {
    "white": "#FFFFFF",
    "muted": "#A8B3C7",
}


def fetch_icon(name, color_key):
    color = COLORS[color_key]
    url = f"https://cdn.jsdelivr.net/npm/lucide-static@0.469.0/icons/{name}.svg"
    try:
        with urllib.request.urlopen(url, timeout=30) as resp:
            data = resp.read().decode("utf-8")
    except Exception as exc:  # noqa: BLE001
        return name, color_key, f"FAIL {exc}"
    data = data.replace('stroke="currentColor"', f'stroke="{color}"')
    out_dir = os.path.join(ICON_DIR, color_key)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, name + ".svg"), "w", encoding="utf-8") as fh:
        fh.write(data)
    return name, color_key, "ok"


def fetch_font():
    os.makedirs(FONT_DIR, exist_ok=True)
    url = "https://raw.githubusercontent.com/rsms/inter/master/docs/font-files/InterVariable.ttf"
    dst = os.path.join(FONT_DIR, "InterVariable.ttf")
    try:
        urllib.request.urlretrieve(url, dst)
        return f"font ok ({os.path.getsize(dst)} bytes)"
    except Exception as exc:  # noqa: BLE001
        return f"font FAIL {exc}"


def main():
    os.makedirs(ICON_DIR, exist_ok=True)
    jobs = [(name, key) for name in ICONS for key in COLORS]
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
        for res in pool.map(lambda a: fetch_icon(*a), jobs):
            results.append(res)
    fails = [r for r in results if r[2] != "ok"]
    print(f"icons: {len(results) - len(fails)} ok, {len(fails)} failed")
    for name, key, status in fails[:20]:
        print("  ", name, key, status)
    print(fetch_font())


if __name__ == "__main__":
    sys.exit(main())
