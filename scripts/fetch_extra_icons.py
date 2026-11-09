"""Fetch any extra icons by name (run after fetch_assets.py)."""
import os
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "resources", "icons")

EXTRA = [
    "plus", "check-check", "scan-face", "hard-drive-download", "hard-drive-upload",
    "folder-open", "git-branch", "circle-check-big", "circle-x", "ellipsis",
    "arrow-down-narrow-wide", "calendar-sync", "triangle-alert", "cloud-download",
]
COLORS = {"white": "#FFFFFF", "muted": "#A8B3C7"}

ok = fail = 0
for name in EXTRA:
    for key, color in COLORS.items():
        dst = os.path.join(ICON_DIR, key, name + ".svg")
        if os.path.exists(dst):
            ok += 1
            continue
        try:
            data = urllib.request.urlopen(
                f"https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/{name}.svg", timeout=30
            ).read().decode()
            data = data.replace('stroke="currentColor"', f'stroke="{color}"')
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w", encoding="utf-8") as fh:
                fh.write(data)
            ok += 1
        except Exception as exc:  # noqa: BLE001
            fail += 1
            print("FAIL", name, key, exc)
print("ok", ok, "fail", fail)
