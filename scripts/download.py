"""Download a URL to a local file using Python (avoids curl/TLS quirks)."""
import os
import ssl
import sys
import urllib.request


def download(url, dst, timeout=180):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    ctx = ssl.create_default_context()
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 XMF"})
    with urllib.request.urlopen(req, timeout=timeout, context=ctx) as resp, open(dst, "wb") as fh:
        total = 0
        while True:
            chunk = resp.read(1024 * 256)
            if not chunk:
                break
            fh.write(chunk)
            total += len(chunk)
    print("saved", dst, os.path.getsize(dst), "bytes")
    return True


if __name__ == "__main__":
    download(sys.argv[1], sys.argv[2])
