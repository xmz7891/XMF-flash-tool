"""Normalise fractional font.pixelSize values to integers (QML expects int)."""
import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
pattern = re.compile(r"(font\.pixelSize:\s*)([0-9]+(?:\.[0-9]+)?)")

changed = 0
for path in glob.glob(os.path.join(ROOT, "qml", "**", "*.qml"), recursive=True):
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()

    def repl(match):
        value = float(match.group(2))
        return match.group(1) + str(int(round(value)))

    new = pattern.sub(repl, text)
    if new != text:
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(new)
        changed += 1
        print("fixed", os.path.relpath(path, ROOT))
print("files changed:", changed)
