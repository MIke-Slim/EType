"""Prepare this computer's independent test directory, without an installer."""
import json
from pathlib import Path
import shutil
import sys
import argparse
import subprocess
import hashlib

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--release', action='store_true')
parser.add_argument('--toolchain', type=Path)
args = parser.parse_args()
TARGET = ROOT / ('build/online-release/package' if args.release else 'build/online-test/package')
if TARGET.exists():
    raise SystemExit('Test package directory already exists; choose a new version before replacing it')
native = ROOT / ('build/online-release/native' if args.release else 'build/online-test/native')
base = Path(sys.base_prefix)
sites = Path(sys.prefix) / 'Lib/site-packages'
TARGET.mkdir(parents=True)
for name in ('EType.exe', 'x64', 'x86', 'assets'):
    source = native / name
    if source.is_dir():
        shutil.copytree(source, TARGET / name)
    else:
        shutil.copy2(source, TARGET / name)
runtime = TARGET / 'runtime'
python = runtime / 'python'
python.mkdir(parents=True)
for name in ('python.exe', 'pythonw.exe', 'python3.dll', 'python312.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'LICENSE.txt'):
    shutil.copy2(base / name, python / name)
excluded = {'site-packages', 'test', 'tests', '__pycache__', 'idlelib', 'tkinter', 'turtledemo', 'ensurepip', 'pip', 'pip-25.0.1.dist-info'}
ignore = shutil.ignore_patterns(*excluded, '*.pyc', '*.pyo')
shutil.copytree(base / 'Lib', python / 'Lib', ignore=ignore)
shutil.copytree(sites, python / 'Lib/site-packages', ignore=ignore)
(python / 'DLLs').mkdir()
for file in (base / 'DLLs').iterdir():
    if file.is_file() and not file.name.startswith(('_test', '_ctypes_test', '_tkinter', 'tcl', 'tk')):
        shutil.copy2(file, python / 'DLLs' / file.name)
(python / 'python312._pth').write_text('Lib\nDLLs\nLib/site-packages\n..\nimport site\n', encoding='utf-8')
for name in ('online_service.py', 'online_services.py'):
    shutil.copy2(ROOT / 'scripts' / name, runtime / name)
shutil.copy2(ROOT / 'docs/在线测试版使用说明.txt', TARGET / '使用说明.txt')
shutil.copy2(ROOT / 'assets/online-lab.html', TARGET / 'assets/online-lab.html')
manifest = {'variant': 'online-test', 'port': 49183, 'clsid': '{2508C9AF-571F-45AC-8709-5CD3C0B14366}',
    'profile': '{078CBB5E-8CB6-4CAE-B5E3-6524A3485A53}', 'name': 'EType 在线测试版',
    'source_commit': '6ad597b plus installation integration', 'python': sys.version}
if args.release:
    if not args.toolchain:
        raise SystemExit('Pass --toolchain to include the statically linked runtime license notices')
    manifest.update(variant='online-release', version='0.3.0', port=49185,
        clsid='{7BD6247C-64A2-4AA9-B702-C731413CD0A2}', profile='{A25BF57C-145F-4AF6-88C8-13B8CBA492FB}', name='EType 在线版',
        source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip())
    (TARGET / '使用说明.txt').write_text((ROOT / 'docs/在线测试版使用说明.txt').read_text(encoding='utf-8').replace('EType 在线测试版','EType 在线版').replace('当前是直接安装的测试版，没有生成 EXE 安装包。','本版本提供独立 EXE 安装包，可自主选择安装路径。'), encoding='utf-8')
    licenses = TARGET / 'licenses'
    licenses.mkdir()
    shutil.copy2(ROOT / 'online-data/v1/ECDICT-LICENSE', licenses / 'ECDICT-LICENSE.txt')
    shutil.copy2(args.toolchain / 'LICENSE.TXT', licenses / 'LLVM-LICENSE.txt')
    for notice in (args.toolchain / 'x86_64-w64-mingw32/share/mingw32').glob('COPYING*'):
        shutil.copy2(notice, licenses / notice.name)
    manifest['files'] = {str(file.relative_to(TARGET)).replace('\\','/'): hashlib.sha256(file.read_bytes()).hexdigest()
        for file in TARGET.rglob('*') if file.is_file()}
(TARGET / ('package-manifest.json' if args.release else 'online-test-manifest.json')).write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
total = sum(file.stat().st_size for file in TARGET.rglob('*') if file.is_file())
print(json.dumps({'path': str(TARGET), 'bytes': total, 'MiB': round(total / 1048576, 2)}))
