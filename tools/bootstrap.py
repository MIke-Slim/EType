"""Fetch the pinned build toolchain and the offline dictionary's upstream source."""
import hashlib
import json
import pathlib
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def fetch(url, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={'User-Agent': 'EType-development'})
    temporary = destination.with_suffix(destination.suffix + '.download')
    with urllib.request.urlopen(request, timeout=180) as response, temporary.open('wb') as output:
        while chunk := response.read(1024 * 1024):
            output.write(chunk)
    temporary.replace(destination)
    print(destination.name, destination.stat().st_size, flush=True)


def main():
    release = json.loads((ROOT / 'tools/toolchain-release.json').read_text(encoding='utf-8'))
    extracted = ROOT / 'tools' / release['name'].removesuffix('.zip')
    archive = ROOT / 'tools/llvm-mingw.zip'
    if not extracted.exists():
        if not archive.exists():
            fetch(release['url'], archive)
        with zipfile.ZipFile(archive) as source:
            source.extractall(ROOT / 'tools')
    source = ROOT / 'third_party/ecdict.csv'
    if not source.exists():
        fetch('https://raw.githubusercontent.com/skywind3000/ECDICT/master/ecdict.csv', source)
    license_file = ROOT / 'third_party/ECDICT-LICENSE'
    if not license_file.exists():
        fetch('https://raw.githubusercontent.com/skywind3000/ECDICT/master/LICENSE', license_file)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    manifest = ROOT / 'data/manifest.json'
    expected = json.loads(manifest.read_text(encoding='utf-8'))['source_sha256']
    print('ECDICT source SHA256:', digest)
    if digest != expected:
        print('Upstream snapshot differs from the recorded build. Rebuilding will record its new hash.')
    print('Ready. Run scripts/build.ps1 to build and test EType.')


if __name__ == '__main__':
    main()
