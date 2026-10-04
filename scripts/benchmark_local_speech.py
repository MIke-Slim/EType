"""Compare CPU and DirectML; stores real timings and generated audio."""
import json
import time
import onnxruntime as ort
import soundfile as sf
from kokoro_onnx import Kokoro
from local_ai import BASE

TEXT = "I went to the bank to deposit some money."
rows = []
for name, threads, model, providers in [
    ("cpu-1", 1, "kokoro-v1.0.int8.onnx", ["CPUExecutionProvider"]),
    ("cpu-2", 2, "kokoro-v1.0.int8.onnx", ["CPUExecutionProvider"]),
    ("dml-0", 2, "kokoro-v1.0.fp16-gpu.onnx", [("DmlExecutionProvider", {"device_id": "0"}), "CPUExecutionProvider"]),
    ("dml-1", 2, "kokoro-v1.0.fp16-gpu.onnx", [("DmlExecutionProvider", {"device_id": "1"}), "CPUExecutionProvider"]),
]:
    try:
        if name.startswith("dml") and "DmlExecutionProvider" not in ort.get_available_providers():
            raise RuntimeError("DirectML experimental backend is not installed; not tested")
        options = ort.SessionOptions()
        options.intra_op_num_threads = threads
        options.inter_op_num_threads = 1
        options.enable_mem_pattern = False
        options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        options.log_severity_level = 3
        start = time.perf_counter()
        session = ort.InferenceSession(str(BASE / "models" / model), options, providers=providers)
        engine = Kokoro.from_session(session, str(BASE / "models/voices-v1.0.bin"))
        load_seconds = time.perf_counter() - start
        start = time.perf_counter()
        audio, rate = engine.create(TEXT, voice="af_heart", lang="en-us")
        first_seconds = time.perf_counter() - start
        start = time.perf_counter()
        audio, rate = engine.create(TEXT, voice="af_heart", lang="en-us")
        warm_seconds = time.perf_counter() - start
        sf.write(BASE / "samples" / (name + ".wav"), audio, rate)
        row = {"name": name, "providers": session.get_providers(), "model": model,
               "load_seconds": round(load_seconds, 3), "first_seconds": round(first_seconds, 3),
               "warm_seconds": round(warm_seconds, 3), "audio_seconds": round(len(audio) / rate, 3)}
        del engine, session
    except Exception as error:
        row = {"name": name, "error": str(error)}
    rows.append(row)
    print(json.dumps(row, ensure_ascii=False), flush=True)
    (BASE / "speech-benchmark.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
