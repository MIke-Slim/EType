"""Package a passed build and verify every archived byte against the package."""
import hashlib
import json
import pathlib
import zipfile
from datetime import datetime, timezone, timedelta

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
PACKAGE = BUILD / 'EType'


def main():
    evidence = json.loads((BUILD / 'build-test-evidence.json').read_text(encoding='utf-8'))
    if not evidence.get('passed'):
        raise RuntimeError('Verified build evidence is missing.')
    for relative, expected in evidence['files'].items():
        if hashlib.sha256((PACKAGE / relative).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'File changed after tests: {relative}')
    ui = json.loads((BUILD / 'ui-selftest.json').read_text(encoding='utf-8'))
    if not ui.get('passed'):
        raise RuntimeError('Preview text-box tests did not pass.')
    for name in ('core-test-results.txt', 'tsf-test-results.txt', 'tsf-test-results-x86.txt'):
        result = (BUILD / name).read_text(encoding='utf-16')
        if 'failures=0' not in result:
            raise RuntimeError(f'Test evidence missing or failed: {name}')
    for name in ('EType.exe', 'x64/EType.dll', 'x86/EType.dll', 'data/dictionary.tsv',
                 'data/manifest.json', 'install.ps1', 'uninstall.ps1', 'registration.ps1', '使用说明.txt'):
        if not (PACKAGE / name).is_file():
            raise RuntimeError(f'Package file missing: {name}')
    (PACKAGE / '验证记录.md').write_bytes((ROOT / 'docs/验证记录.md').read_bytes())
    files = {}
    for path in sorted(PACKAGE.rglob('*')):
        if path.is_file() and path.name != 'package-manifest.json':
            content = path.read_bytes()
            files[path.relative_to(PACKAGE).as_posix()] = {
                'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
    manifest = {'version': '0.1.1', 'platform': 'Windows x64 with x86 input service',
                'verified_on': datetime.now(timezone(timedelta(hours=8))).date().isoformat(),
                'tested_build': evidence, 'ui_selftest': ui, 'files': files}
    (PACKAGE / 'package-manifest.json').write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    archive = BUILD / 'EType-0.1.1-Windows.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in sorted(PACKAGE.rglob('*')):
            if path.is_file():
                output.write(path, path.relative_to(BUILD).as_posix())
    with zipfile.ZipFile(archive) as result:
        if result.testzip():
            raise RuntimeError('Archive integrity check failed.')
        for relative, evidence in files.items():
            if hashlib.sha256(result.read('EType/' + relative)).hexdigest() != evidence['sha256']:
                raise RuntimeError(f'Archive contents differ: {relative}')
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (archive.with_suffix('.zip.sha256')).write_text(digest + '  ' + archive.name + '\n', encoding='ascii')
    print(json.dumps({'archive': str(archive), 'bytes': archive.stat().st_size,
                      'sha256': digest, 'files': len(files) + 1}, ensure_ascii=False))


if __name__ == '__main__':
    main()
