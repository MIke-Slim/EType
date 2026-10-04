"""Loopback-only development preview. Does not modify the installed input method."""
import argparse
import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import threading
import time
import traceback
import urllib.request
from local_ai import BASE, ROOT, MODELS, Speech, Translator


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=49181)
    parser.add_argument("--model-port", type=int, default=49180)
    parser.add_argument("--cpu", action="store_true")
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535 or not 1024 <= args.model_port <= 65535 or args.port == args.model_port:
        parser.error("Invalid preview port")
    speech, translator = Speech(), Translator(args.model_port)
    origin = f"http://127.0.0.1:{args.port}"
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    model_process = None
    log_file = None
    active = threading.BoundedSemaphore(2)

    class Handler(BaseHTTPRequestHandler):
        def send(self, status, body, content_type):
            try:
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Content-Type-Options", "nosniff")
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                # The native stop button can disconnect while synthesis finishes.
                self.close_connection = True

        def send_json(self, status, data):
            self.send(status, json.dumps(data, ensure_ascii=False).encode("utf-8"), "application/json; charset=utf-8")

        def do_GET(self):
            if self.headers.get("Host") != f"127.0.0.1:{args.port}":
                self.send_json(403, {"error": "仅允许本机访问"})
                return
            if self.path == "/":
                self.send(200, (ROOT / "assets/local-ai-lab.html").read_bytes(), "text/html; charset=utf-8")
            elif self.path == "/logo.png":
                self.send(200, (ROOT / "assets/etype-logo.png").read_bytes(), "image/png")
            elif self.path == "/health":
                self.send_json(200, {"status": "ok", "model": "Qwen3-4B-Q4_K_M", "speech": "Kokoro", "local_only": True, "native_protocol": 1,
                                    "runtime": "standalone" if os.environ.get("ETYPE_RESOURCE_ROOT") else "development"})
            elif re.fullmatch(r"/audio/[0-9a-f]{64}\.wav", self.path):
                path = BASE / "audio" / self.path.rsplit("/", 1)[1]
                if path.is_file():
                    self.send(200, path.read_bytes(), "audio/wav")
                else:
                    self.send_json(404, {"error": "音频不存在"})
            else:
                self.send_json(404, {"error": "页面不存在"})

        def do_POST(self):
            if (self.headers.get("Host") != f"127.0.0.1:{args.port}"
                or self.headers.get("Origin") not in (None, origin)
                or self.headers.get("X-EType-Lab") != "1"):
                self.send_json(403, {"error": "仅允许本地验证页面发起操作"})
                return
            if not active.acquire(blocking=False):
                self.send_json(429, {"error": "正在处理，请稍后重试"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 8192:
                    raise ValueError("请求长度不正确")
                payload = self.rfile.read(length)
                if self.path in ("/native/translate", "/native/speak"):
                    text = payload.decode("utf-8")
                    if self.headers.get("Content-Type") != "text/plain; charset=utf-8":
                        raise ValueError("请求格式不正确")
                    if self.path == "/native/translate":
                        result = translator.translate(text)
                        # Each record is one candidate. No JSON parser in a host process.
                        values = [re.sub(r"[\r\n\t]+", " ", value) for value in result["candidates"]]
                        self.send(200, "\n".join(values).encode("utf-8"), "text/plain; charset=utf-8")
                    else:
                        result = speech.synthesize(text, self.headers.get("X-EType-Voice", "female"),
                                                   float(self.headers.get("X-EType-Speed", "1.0")))
                        self.send(200, Path(result["file"]).read_bytes(), "audio/wav")
                    return
                data = json.loads(payload)
                if not isinstance(data, dict):
                    raise ValueError("请求格式不正确")
                if self.path == "/translate":
                    self.send_json(200, translator.translate(data.get("text")))
                elif self.path == "/speak":
                    result = speech.synthesize(data.get("text"), data.get("voice", "female"), data.get("speed", 1.0))
                    result["audio_url"] = "/audio/" + Path(result.pop("file")).name
                    self.send_json(200, result)
                elif self.path == "/shutdown":
                    self.send_json(200, {"status": "stopping"})
                    threading.Thread(target=server.shutdown, daemon=True).start()
                else:
                    self.send_json(404, {"error": "操作不存在"})
            except ValueError as error:
                self.send_json(400, {"error": str(error)})
            except Exception:
                traceback.print_exc()
                self.send_json(503, {"error": "本地模型暂时无法完成，请重试；英文内容已保留"})
            finally:
                active.release()

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    try:
        # Fail rather than attaching to an unrelated service on the model port.
        with socket.socket() as check:
            check.bind(("127.0.0.1", args.model_port))
        log_file = (BASE / "lab-model.log").open("w", encoding="utf-8")
        runtime = Path(os.environ.get("ETYPE_LLAMA_ROOT", BASE / "llama"))
        command = [str(runtime / "llama-server.exe"), "-m", str(MODELS / "Qwen3-4B-Q4_K_M.gguf"),
                   "--alias", "etype-local", "--host", "127.0.0.1", "--port", str(args.model_port), "--ctx-size", "4096",
                   "--parallel", "1", "--gpu-layers", "0" if args.cpu else "99", "--flash-attn", "on", "--jinja", "--reasoning", "off",
                   "--no-webui", "--cors-origins", ""]
        # Frozen Python alters DLL lookup for its own extensions. The independent
        # llama runtime must resolve its matching DLLs from its own directory.
        frozen_windows = sys.platform == "win32" and getattr(sys, "frozen", False)
        if frozen_windows:
            ctypes.windll.kernel32.SetDllDirectoryW(None)
        try:
            model_process = subprocess.Popen(command, cwd=runtime, stdout=log_file, stderr=log_file,
                creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, "CREATE_NO_WINDOW") else 0)
        finally:
            if frozen_windows:
                ctypes.windll.kernel32.SetDllDirectoryW(sys._MEIPASS)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            if model_process.poll() is not None:
                raise RuntimeError("Model process exited; see build/local-ai/lab-model.log")
            try:
                with opener.open(f"http://127.0.0.1:{args.model_port}/health", timeout=1) as response:
                    if json.load(response).get("status") == "ok":
                        break
            except Exception:
                time.sleep(0.25)
        else:
            raise TimeoutError("Model startup timed out")
        print("Local preview ready: " + origin, flush=True)
        server.serve_forever()
    finally:
        server.server_close()
        if model_process is not None and model_process.poll() is None:
            model_process.terminate()
            try:
                model_process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                model_process.kill()
                model_process.wait()
        if log_file:
            log_file.close()


if __name__ == "__main__":
    main()
