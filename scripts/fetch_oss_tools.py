"""Download open-source helper binaries used by XMF (payload-dumper-go, scrcpy)."""
import json
import os
import ssl
import sys
import urllib.request
import zipfile
import tarfile
import io

ROOT = r"C:\Users\shu\Desktop\sjgj"
OSS_DIR = os.path.join(ROOT, "data", "thirdparty", "oss")
CTX = ssl.create_default_context()


def api(url):
    req = urllib.request.Request(url, headers={"User-Agent": "XMF", "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=60, context=CTX) as resp:
        return json.load(resp)


def download(url, dst):
    req = urllib.request.Request(url, headers={"User-Agent": "XMF"})
    with urllib.request.urlopen(req, timeout=180, context=CTX) as resp, open(dst, "wb") as fh:
        while True:
            chunk = resp.read(1024 * 256)
            if not chunk:
                break
            fh.write(chunk)
    print("downloaded", dst, os.path.getsize(dst))


def latest_assets(repo):
    data = api(f"https://api.github.com/repos/{repo}/releases/latest")
    return data.get("tag_name"), data.get("assets", [])


def find_asset(assets, *needles):
    for asset in assets:
        name = asset["name"].lower()
        if all(needle in name for needle in needles):
            return asset
    return None


def main():
    os.makedirs(OSS_DIR, exist_ok=True)

    # ---- payload-dumper-go -------------------------------------------------
    try:
        tag, assets = latest_assets("ssut/payload-dumper-go")
        asset = find_asset(assets, "windows", "amd64") or find_asset(assets, "windows")
        if asset:
            dst = os.path.join(OSS_DIR, "payload-dumper-go.zip")
            download(asset["browser_download_url"], dst)
            with zipfile.ZipFile(dst) as archive:
                archive.extractall(os.path.join(OSS_DIR, "payload-dumper-go"))
            print("payload-dumper-go", tag)
        else:
            print("payload-dumper-go: no windows asset")
    except Exception as exc:  # noqa: BLE001
        print("payload-dumper-go failed:", exc)

    # ---- scrcpy ------------------------------------------------------------
    try:
        tag, assets = latest_assets("Genymobile/scrcpy")
        asset = find_asset(assets, "win64") or find_asset(assets, "windows")
        if asset:
            dst = os.path.join(OSS_DIR, "scrcpy.zip")
            download(asset["browser_download_url"], dst)
            with zipfile.ZipFile(dst) as archive:
                archive.extractall(os.path.join(OSS_DIR, "scrcpy"))
            print("scrcpy", tag)
        else:
            print("scrcpy: no windows asset")
    except Exception as exc:  # noqa: BLE001
        print("scrcpy failed:", exc)


if __name__ == "__main__":
    main()
