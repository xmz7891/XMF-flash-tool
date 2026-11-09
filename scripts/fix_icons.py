"""Fill in icons whose canonical Lucide name differs (renamed icons)."""
import os
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "resources", "icons")

ALIASES = {
    "home": "home",
    "download-cloud": "cloud-download",
    "list-checks": "list-checks",
    "alert-triangle": "triangle-alert",
    "more-horizontal": "ellipsis",
    "unlock": "lock-open",
    "wand-2": "wand-sparkles",
    "right-left": "arrow-right-left",
    "calendar-rotate": "calendar-sync",
    "sort-desc": "arrow-down-narrow-wide",
    "unfold-vertical": "unfold-vertical",
    "x-circle": "circle-x",
    "check-circle-2": "circle-check-big",
    "info-zap": "zap",
    "scan-face": "scan-face",
    "folder-cog": "folder-cog",
    "file-cog": "file-cog",
    "circle-dot": "circle-dot",
    "list-filter": "list-filter",
}
COLORS = {"white": "#FFFFFF", "muted": "#A8B3C7"}

ok = fail = 0
for name, target in ALIASES.items():
    for key, color in COLORS.items():
        dst = os.path.join(ICON_DIR, key, name + ".svg")
        if os.path.exists(dst):
            ok += 1
            continue
        url = f"https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/{target}.svg"
        try:
            data = urllib.request.urlopen(url, timeout=30).read().decode()
            data = data.replace('stroke="currentColor"', f'stroke="{color}"')
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w", encoding="utf-8") as fh:
                fh.write(data)
            ok += 1
        except Exception as exc:  # noqa: BLE001
            fail += 1
            print("FAIL", name, key, exc)
print("ok", ok, "fail", fail)
