"""Collect real outputs against authored meaning criteria. This is not an accuracy scorer."""
import argparse
import json
from pathlib import Path
import time
from local_ai import ROOT, BASE, Translator


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="baseline")
    args = parser.parse_args()
    if not args.label.replace("-", "").isalnum():
        parser.error("Use letters, numbers and hyphens for the report label")
    cases = json.loads((ROOT / "data/sentence-evaluation.json").read_text(encoding="utf-8"))["cases"]
    translator, results = Translator(), []
    start = time.perf_counter()
    target = BASE / ("evaluation-" + args.label + ".json")
    for case in cases:
        try:
            result = translator.translate(case["text"])
        except Exception as error:
            result = {"error": str(error), "attempts": getattr(error, "attempts", [])}
        results.append({**case, "result": result, "semantic_review": "pending"})
        target.write_text(json.dumps({"label": args.label, "scope": "development sample; manual semantic review required",
            "seconds": round(time.perf_counter() - start, 3), "cases": results}, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps({"id": case["id"], **result}, ensure_ascii=False), flush=True)
    print(str(target), flush=True)


if __name__ == "__main__":
    main()
