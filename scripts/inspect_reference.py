"""Inspect the reference GeekFlashTool binary: guide text + runtime/framework hints."""
import os
import re

ROOT = r"C:\Users\shu\Desktop\sjgj\_research\reference"


def read_guide():
    path = os.path.join(ROOT, "使用指南.txt")
    if not os.path.exists(path):
        print("guide missing")
        return
    raw = open(path, "rb").read()
    for enc in ("utf-8", "gbk", "utf-16"):
        try:
            print("---- guide (%s) ----" % enc)
            print(raw.decode(enc))
            break
        except Exception:
            continue


def inspect_exe():
    path = os.path.join(ROOT, "GeekFlashTool.exe")
    data = open(path, "rb").read()
    print("size", len(data))
    hints = {
        "dotnet": [b"mscoree.dll", b".NETFramework", b"BSJB"],
        "qt5": [b"Qt5Core.dll", b"Qt5Widgets.dll"],
        "qt6": [b"Qt6Core.dll", b"Qt6Widgets.dll", b"Qt6Quick.dll"],
        "wpf": [b"PresentationFramework", b"PresentationCore"],
        "winforms": [b"System.Windows.Forms"],
        "python": [b"python3", b"PyInstaller"],
        "electron": [b"electron", b"app.asar"],
        "duilib": [b"duilib", b"DuiLib"],
        "sciter": [b"sciter"],
    }
    for name, needles in hints.items():
        found = [n.decode("latin1") for n in needles if n in data]
        if found:
            print(name, "->", found)
    # version info strings (UTF-16LE)
    for needle in ("FileVersion", "ProductName", "CompanyName", "OriginalFilename"):
        idx = data.find(needle.encode("utf-16-le"))
        if idx > 0:
            chunk = data[idx:idx + 160].decode("utf-16-le", "ignore")
            print("ver:", chunk.split("\x00")[0][:120])


if __name__ == "__main__":
    read_guide()
    inspect_exe()
