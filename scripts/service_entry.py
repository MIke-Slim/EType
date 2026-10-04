"""Installed local worker: assets beside the EXE, writes only to user cache."""
import ctypes
import os
from pathlib import Path
import sys
import traceback


def main():
    root = Path(sys.executable).resolve().parents[1]
    cache = Path(os.environ["LOCALAPPDATA"]) / "EType" / "local-ai"
    # Alternate ports isolate release smoke tests from the user's running service.
    if "--port" in sys.argv:
        cache = cache / ("test-" + sys.argv[sys.argv.index("--port") + 1])
    cache.mkdir(parents=True, exist_ok=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    ctypes.set_last_error(0)
    handle = kernel.CreateMutexW(None, False, "Local\\EType.LocalWorker." + cache.name)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    duplicate = ctypes.get_last_error() == 183
    if duplicate:
        kernel.CloseHandle(handle)
        return
    os.environ.update(ETYPE_RESOURCE_ROOT=str(root), ETYPE_CACHE_ROOT=str(cache),
                      ETYPE_MODEL_ROOT=str(root / "models"), ETYPE_LLAMA_ROOT=str(root / "llama"))
    with (cache / "service.log").open("a", encoding="utf-8", buffering=1) as log:
        sys.stdout = sys.stderr = log
        try:
            if "--self-check" in sys.argv:
                from local_ai import Speech
                Speech().synthesize("apple")
                print("Standalone speech self-check passed", flush=True)
                return
            from local_ai_lab import main as serve
            serve()
        except Exception:
            traceback.print_exc()
            return 1
        finally:
            kernel.CloseHandle(handle)


if __name__ == "__main__":
    sys.exit(main() or 0)
