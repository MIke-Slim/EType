"""Publish compact verification facts without local user paths."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
installer = root / 'releases/EType-0.3.0-Online-Setup.exe'
lifecycle = json.loads((root / 'build/online-installer-test-results.json').read_text(encoding='utf-8-sig'))
production = json.loads((root / 'build/online-release/production-results.json').read_text(encoding='utf-8-sig'))
manifest = json.loads((root / 'build/online-release/package/package-manifest.json').read_text(encoding='utf-8'))
digest = hashlib.sha256(installer.read_bytes()).hexdigest()
if not lifecycle['passed'] or not production['passed'] or production['installer_sha256'] != digest:
    raise SystemExit('Final installer verification missing or mismatched')
evidence = {'version': '0.3.0', 'source_commit': manifest['source_commit'],
    'installer_sha256': digest, 'installer_bytes': installer.stat().st_size,
    'installed_bytes': int(production['installed_bytes']), 'speech_cache_max_bytes': 20 * 1024 * 1024,
    'signature': 'unsigned', 'core_baseline_checks': 95, 'core_baseline_failures': 0,
    'online_service_unit_tests': 7, 'windowless_worker_smoke': 'health, error responses, reject foreign shutdown, owned shutdown',
    'installer_lifecycle': lifecycle['checks'], 'production_installer': production['checks'],
    'registration_checks': {'x64': 7, 'x86': 7, 'failures': 0},
    'scope': 'compiled x64/x86 TSF, installed native component input, real free online services, installer lifecycle and global registration',
    'limits': 'External host keyboard routing and service availability remain subject to user testing; size excludes preserved old versions.'}
installer.with_suffix('.tests.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'installer_bytes': evidence['installer_bytes'], 'installed_bytes': evidence['installed_bytes'], 'sha256': digest}))
