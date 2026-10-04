"""Boundary tests for the local-model development adapters, no model downloads."""
import json
from pathlib import Path
import sys
import threading
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from local_ai import Speech, Translator, parse_candidates, validate_text, semantic_guard_reasons


class LocalAIContracts(unittest.TestCase):
    def test_text_boundaries(self):
        for text in (None, "", "  ", "纯中文", "hello\x00world", "a" * 501):
            with self.subTest(text=str(text)[:30]), self.assertRaises(ValueError):
                validate_text(text)
        self.assertEqual(validate_text("  bank  "), "bank")
        self.assertEqual(len(validate_text("a" * 500)), 500)

    def test_invalid_model_output_does_not_become_candidate(self):
        for value in ([], {"candidates": []}, {"candidates": ["hello"]},
                      {"candidates": [False]}, {"candidates": ["中文"] * 4},
                      {"candidates": ["中文"], "extra": "中文"}, {"candidates": [" "]}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_candidates(json.dumps(value))

    def test_duplicate_candidates_preserve_first_order(self):
        result = parse_candidates(json.dumps({"candidates": ["河岸。", " 河岸! ", "银行"]}))
        self.assertEqual(result, ["河岸。", "银行"])

    def test_review_rejects_semantic_change(self):
        responses = [(json.dumps({"candidates": ["我没有说他偷钱。", "我说他没有偷钱。"]}), {}),
                     (json.dumps({"accepted": [True, False]}), {})]
        with patch.object(Translator, "_request", side_effect=responses):
            result = Translator().translate("I didn't say he stole the money.")
        self.assertEqual(result["candidates"], ["我没有说他偷钱。"])
        self.assertEqual(result["filtered_count"], 1)

    def test_review_all_rejected_preserves_failure(self):
        responses = [(json.dumps({"candidates": ["错误的中文"]}), {}),
                     (json.dumps({"accepted": [False]}), {})] * 2
        with patch.object(Translator, "_request", side_effect=responses), self.assertRaises(ValueError):
            Translator().translate("A sentence.")

    def test_invalid_review_is_rejected(self):
        for accepted in ([True, True], [1], "true", None):
            responses = [(json.dumps({"candidates": ["正确中文"]}), {}),
                         (json.dumps({"accepted": accepted}), {})]
            with self.subTest(accepted=accepted), patch.object(Translator, "_request", side_effect=responses), self.assertRaises(ValueError):
                Translator().translate("A sentence.")

    def test_speech_options_rejected_before_loading_model(self):
        for voice, speed in (("unknown", 1.0), ("female", 3.0)):
            with self.subTest(voice=voice, speed=speed), self.assertRaises(ValueError):
                Speech().synthesize("hello", voice, speed)

    def test_model_endpoint_is_loopback(self):
        self.assertEqual(Translator().url, "http://127.0.0.1:49180/v1/chat/completions")

    def test_cached_replay_does_not_wait_for_unrelated_generation(self):
        entered, release = threading.Event(), threading.Event()
        class FakeVoice:
            def create(self, text, **kwargs):
                if text == "an unrelated sentence":
                    entered.set()
                    release.wait(2)
                return [0.0] * 240, 24000
        with tempfile.TemporaryDirectory() as temp, patch("local_ai.BASE", Path(temp)):
            speech = Speech()
            speech.engine = FakeVoice()
            speech.synthesize("apple")
            errors = []
            def generate():
                try: speech.synthesize("an unrelated sentence")
                except Exception as error: errors.append(error)
            worker = threading.Thread(target=generate)
            worker.start()
            try:
                self.assertTrue(entered.wait(1))
                start = time.perf_counter()
                result = speech.synthesize("apple")
                self.assertTrue(result["cached"])
                self.assertLess(time.perf_counter() - start, 0.2)
            finally:
                release.set()
                worker.join(3)
            self.assertFalse(worker.is_alive())
            self.assertEqual(errors, [])

    def test_guard_rejects_quantity_as_money_and_lost_counterfactual(self):
        self.assertTrue(semantic_guard_reasons("Sales increased from 200 to 230 units.", "销售额从200件增加到230件。"))
        self.assertFalse(semantic_guard_reasons("Sales increased from 200 to 230 units.", "销量从200件增加到230件。"))
        source = "If I had known, I would have called you."
        self.assertTrue(semantic_guard_reasons(source, "如果我知道，我就会给你打电话。"))
        self.assertFalse(semantic_guard_reasons(source, "如果我当时知道，我就会给你打电话了。"))
        self.assertTrue(semantic_guard_reasons("Keep report_2026.csv unchanged.", "保留report.csv不变。"))
        self.assertTrue(semantic_guard_reasons("She seldom travels.", "她 seldom 旅行。"))
        self.assertFalse(semantic_guard_reasons("She seldom travels.", "她很少旅行。"))
        self.assertTrue(semantic_guard_reasons("Help with the box.", "帮忙搬箱子。"))
        self.assertTrue(semantic_guard_reasons("Help with the box.", "帮我一把。"))
        self.assertFalse(semantic_guard_reasons("Help with the box.", "这个箱子帮我搭把手。"))
        self.assertFalse(semantic_guard_reasons("Help carry the box.", "帮忙搬箱子。"))
        self.assertTrue(semantic_guard_reasons("Send the results tomorrow.", "明天寄结果。"))
        self.assertFalse(semantic_guard_reasons("Mail the report tomorrow.", "明天寄报告。"))

    def test_guarded_repair_is_reviewed_and_bounded(self):
        responses = [(json.dumps({"candidates": ["销售额从200件增加到230件。"]}), {}), (json.dumps({"accepted": [True]}), {}),
                     (json.dumps({"candidates": ["销量从200件增加到230件。"]}), {}), (json.dumps({"accepted": [True]}), {})]
        with patch.object(Translator, "_request", side_effect=responses) as request:
            result=Translator().translate("Sales increased from 200 to 230 units.")
        self.assertEqual(result["candidates"],["销量从200件增加到230件。"])
        self.assertEqual(len(result["attempts"]),2)
        self.assertEqual(request.call_count,4)


if __name__ == "__main__":
    unittest.main()
