"""Extract and inventory the reference flashing tool archive provided by the user."""
import os
import sys

import py7zr

SRC = r"C:\Users\shu\Documents\匣(0.5.24.1031-A).7z"
DST = r"C:\Users\shu\Desktop\sjgj\_research\reference"


def main():
    os.makedirs(DST, exist_ok=True)
    with py7zr.SevenZipFile(SRC, mode="r") as archive:
        names = archive.getnames()
        print("entries:", len(names))
        for name in names[:200]:
            print(" ", name)
        archive.extractall(path=DST)
    print("extracted to", DST)


if __name__ == "__main__":
    main()
