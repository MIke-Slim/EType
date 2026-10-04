"""Download development-only local AI assets; never creates an installer."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "build" / "local-ai"
ASSETS = [(a["group"], a["file"], a["source"], a["sha256"])
          for a in json.loads((ROOT / "tools/local-ai-assets.json").read_text(encoding="utf-8-sig"))]


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(4 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def fetch(asset):
    group, name, url, expected = asset
    target = BASE / name
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        partial = target.with_suffix(target.suffix + ".download")
        request = urllib.request.Request(url, headers={"User-Agent": "EType-local-ai-validation"})
        print("Downloading " + name, flush=True)
        with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as out:
            total = int(response.headers.get("Content-Length", 0))
            done, next_report = 0, 100 * 1024 * 1024
            while block := response.read(1024 * 1024):
                out.write(block)
                done += len(block)
                if done >= next_report:
                    print(f"{name}: {done // (1024*1024)} MiB / {total // (1024*1024)} MiB", flush=True)
                    next_report += 100 * 1024 * 1024
            if total and done != total:
                raise RuntimeError("Incomplete download: " + name)
        if expected and digest(partial) != expected:
            raise RuntimeError("Pinned asset checksum mismatch: " + name)
        partial.replace(target)
    actual = digest(target)
    if expected and actual != expected:
        raise RuntimeError("Pinned asset checksum mismatch: " + name)
    print("Ready " + name, flush=True)
    return {"group": group, "file": name, "source": url, "size": target.stat().st_size,
            "sha256": actual, "expected_sha256": expected}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", choices=["speech", "translation", "all"], default="all")
    args = parser.parse_args()
    assets = [a for a in ASSETS if args.only in ("all", a[0])]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(fetch, assets))
    manifest = BASE / ("assets-" + args.only + ".json")
    manifest.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("Manifest: " + str(manifest), flush=True)


if __name__ == "__main__":
    main()
