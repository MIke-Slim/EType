"""Exercise frozen runtime and bundled models without external Python/PATH assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def request(port, path, body=None):
    headers = {"X-EType-Lab": "1", "Content-Type": "text/plain; charset=utf-8"}
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=body, headers=headers)
    with OPENER.open(req, timeout=120) as response:
        return response.read()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", type=Path, default=ROOT / "build/standalone/EType")
    args = parser.parse_args()
    package = args.package.resolve()
    checks = []
    def check(name, value):
        checks.append({"name": name, "passed": bool(value)})
        if not value:
            raise AssertionError(name)
    environment = dict(os.environ, PATH=os.environ["SystemRoot"] + r"\System32",
                       PYTHONHOME="X:\\missing-python", PYTHONPATH="X:\\missing-source")
    passed = False
    try:
        with tempfile.TemporaryDirectory(prefix="etype-independent-") as cwd:
            for cpu, port in ((False, 49191), (True, 49201)):
                label = "cpu" if cpu else "gpu_default"
                arguments = [str(package / "runtime/ETypeService.exe"), "--port", str(port),
                             "--model-port", str(port - 1)] + (["--cpu"] if cpu else [])
                worker = subprocess.Popen(arguments, cwd=cwd, env=environment)
                try:
                    deadline = time.monotonic() + 90
                    health = None
                    while time.monotonic() < deadline:
                        if worker.poll() is not None:
                            raise RuntimeError("Frozen worker exited; inspect user-cache service.log")
                        try:
                            health = json.loads(request(port, "/health"))
                            break
                        except Exception:
                            time.sleep(0.3)
                    check(label + "_standalone_ready_without_external_python", health and
                          health.get("runtime") == "standalone" and health.get("local_only"))
                    word = request(port, "/native/speak", b"apple")
                    check(label + "_bundled_word_speech", word[:4] == b"RIFF" and len(word) > 1000)
                    sentence = request(port, "/native/speak", b"I have 2 apples.")
                    check(label + "_bundled_sentence_speech", sentence[:4] == b"RIFF" and len(sentence) > len(word))
                    chinese = request(port, "/native/translate", b"I have 2 apples.").decode("utf-8")
                    check(label + "_bundled_translation", "苹果" in chinese and any(c in chinese for c in "2两二"))
                    # Installed service also serves its settings assets from the chosen directory.
                    check(label + "_settings_assets", b"EType" in request(port, "/"))
                    duplicate = subprocess.run(arguments, cwd=cwd, env=environment, timeout=10)
                    check(label + "_duplicate_worker_exits_without_stopping_original", duplicate.returncode == 0 and worker.poll() is None)
                    request(port, "/shutdown", b"{}")
                    check(label + "_graceful_shutdown", worker.wait(timeout=15) == 0)
                finally:
                    if worker.poll() is None:
                        try:
                            request(port, "/shutdown", b"{}")
                            worker.wait(timeout=15)
                        except Exception:
                            worker.terminate()
                            worker.wait(timeout=10)
        passed = True
    finally:
        manifest = package / "package-manifest.json"
        evidence = {"passed": passed, "checks": checks, "package": str(package),
                    "scope": "frozen worker from foreign cwd and isolated ports; CPU and default GPU backend",
                    "package_manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest()}
        (ROOT / "build/standalone-service-results.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"passed": passed, "checks": len(checks)}))


if __name__ == "__main__":
    main()
