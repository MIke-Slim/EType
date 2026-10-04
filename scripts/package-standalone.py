"""Stage the verified native build and offline service for a single EXE installer."""
import hashlib
import argparse
import importlib.metadata
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
PACKAGE = BUILD / "standalone" / "EType"


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--refresh", action="store_true")
    args = parser.parse_args()
    if PACKAGE.exists() and not args.refresh:
        raise RuntimeError("Standalone staging already exists; use a fresh verified staging directory.")
    if PACKAGE.exists() and not (PACKAGE / "package-manifest.json").is_file():
        raise RuntimeError("Cannot refresh unmanaged standalone staging.")
    evidence = json.loads((BUILD / "build-test-evidence.json").read_text(encoding="utf-8"))
    if not evidence.get("passed"):
        raise RuntimeError("Native tests have not passed.")
    native = BUILD / "EType"
    for name, expected in evidence["files"].items():
        if sha(native / name) != expected:
            raise RuntimeError("Native file changed since verification: " + name)
    identity = json.loads((native / "input-service-identity.json").read_text(encoding="utf-8"))
    if identity["clsid"] != "{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}":
        raise RuntimeError("Build the unified word/sentence profile before packaging.")
    sentence = json.loads((BUILD / "release-sentence-ui.json.sentences.json").read_text(encoding="utf-8"))
    if not sentence.get("passed"):
        raise RuntimeError("Sentence Enter/Ctrl+Enter UI verification missing.")
    shutil.copytree(native, PACKAGE, dirs_exist_ok=True, ignore=shutil.ignore_patterns("package-manifest.json", "验证记录.md"))
    frozen = BUILD / "frozen" / "ETypeService"
    shutil.copytree(frozen, PACKAGE / "runtime", dirs_exist_ok=True)
    shutil.copytree(BUILD / "local-ai" / "llama", PACKAGE / "llama", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("llama-*.exe", "ggml-rpc-server.exe", "llama.exe"))
    # Only server entrypoint is needed; DLL backends include CPU and CUDA fallback.
    shutil.copy2(BUILD / "local-ai/llama/llama-server.exe", PACKAGE / "llama/llama-server.exe")
    (PACKAGE / "models").mkdir(exist_ok=True)
    for asset in json.loads((ROOT / "tools/local-ai-assets.json").read_text(encoding="utf-8-sig")):
        if not asset["file"].startswith("models/"):
            continue
        path = BUILD / "local-ai" / asset["file"]
        if sha(path) != asset["sha256"]:
            raise RuntimeError("Model checksum mismatch: " + asset["file"])
        shutil.copy2(path, PACKAGE / asset["file"])
    shutil.copy2(ROOT / "assets/local-ai-lab.html", PACKAGE / "assets/local-ai-lab.html")
    shutil.copy2(ROOT / "docs/独立安装包说明.md", PACKAGE / "独立安装包说明.md")
    shutil.copy2(ROOT / "docs/句子评测报告.md", PACKAGE / "句子评测报告.md")
    shutil.copy2(ROOT / "tools/local-ai-assets.json", PACKAGE / "models/source-manifest.json")
    # Preserve installed package license notices, including bundled native dependencies.
    notices = PACKAGE / "licenses/python-packages"
    notices.mkdir(parents=True, exist_ok=True)
    for dist in importlib.metadata.distributions():
        for file in dist.files or ():
            if any(word in file.name.lower() for word in ("license", "copying", "notice")):
                source = Path(dist.locate_file(file))
                if source.is_file():
                    target = notices / (dist.metadata["Name"] + "/" + str(file).replace("../", "").replace("..\\", ""))
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target)
    python_license = Path(sys.base_prefix) / "LICENSE.txt"
    shutil.copy2(python_license, PACKAGE / "licenses/Python-LICENSE.txt")
    for path in (ROOT / "third_party/local-ai").glob("*"):
        shutil.copy2(path, PACKAGE / "licenses" / path.name)
    files = {path.relative_to(PACKAGE).as_posix(): {"bytes": path.stat().st_size, "sha256": sha(path)}
             for path in sorted(PACKAGE.rglob("*")) if path.is_file() and path.name != "package-manifest.json"}
    manifest = {"version": "0.2.0", "platform": "Windows x64 with x86 input service",
                "offline_models_included": True, "requires_python_installation": False,
                "tested_build": evidence, "sentence_ui": sentence, "files": files}
    (PACKAGE / "package-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"package": str(PACKAGE), "files": len(files),
                      "bytes": sum(item["bytes"] for item in files.values())}, ensure_ascii=False))


if __name__ == "__main__":
    main()
