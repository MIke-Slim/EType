"""Exercise the real windowless Windows worker, with no third-party requests."""
import json
from pathlib import Path
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
pythonw = Path(sys.argv[1])
service = ROOT / 'scripts/online_service.py'
cache = ROOT / 'build/online-test/smoke-cache'
process = subprocess.Popen([str(pythonw), '-B', str(service), '--port', '49184', '--cache', str(cache)],
    creationflags=subprocess.CREATE_NO_WINDOW)
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
try:
    deadline = time.monotonic() + 10
    while True:
        try:
            with opener.open('http://127.0.0.1:49184/health', timeout=1) as response:
                assert json.load(response)['online_only']
            break
        except (OSError, ValueError):
            if time.monotonic() > deadline:
                raise
            time.sleep(.1)
    request = urllib.request.Request('http://127.0.0.1:49184/native/word', b'x' * 501,
        headers={'X-EType-Lab': '1', 'Content-Type': 'text/plain; charset=utf-8'})
    try:
        opener.open(request, timeout=2)
        raise AssertionError('Oversized word accepted')
    except urllib.error.HTTPError as error:
        assert error.code == 400
        assert json.loads(error.read())['error']
    print('Windowless worker: health and error responses passed')
finally:
    process.terminate()
    process.wait(timeout=5)
