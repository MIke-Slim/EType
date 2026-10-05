import hashlib
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from online_services import FreeTranslator, OnlineDictionary, ServiceError, validate_text

ROOT = Path(__file__).resolve().parents[1]

class OnlineTests(unittest.TestCase):
    def test_dictionary_multisense_and_full_spelling(self):
        def fetch(url, limit):
            content = (ROOT / 'online-data/v1' / url.rsplit('/', 1)[1]).read_bytes()
            self.assertLessEqual(len(content), limit)
            return content
        dictionary = OnlineDictionary(fetch)
        bank = dictionary.lookup('BANK')
        self.assertEqual({'银行', '河岸'}, {c['text'] for c in bank['candidates']})
        self.assertIsNone(dictionary.lookup('appl'))
        self.assertIn('apple', dictionary.corrections('aple'))
        self.assertIn('apple', dictionary.corrections('appel'))
        self.assertEqual([], dictionary.corrections('apple'))

    def test_dictionary_rejects_tampered_download(self):
        def fetch(url, limit):
            content = (ROOT / 'online-data/v1' / url.rsplit('/', 1)[1]).read_bytes()
            return content if url.endswith('manifest.json') else content + b' '
        with self.assertRaisesRegex(ServiceError, '校验失败'):
            OnlineDictionary(fetch).lookup('bank')

    def test_no_inexact_memory_candidates(self):
        data = {'responseStatus': 200, 'responseData': {'translatedText': '我坐在岸边。'},
            'matches': [{'segment': 'run on the bank', 'translation': '银行挤兑'}]}
        result = FreeTranslator(lambda *args: json.dumps(data).encode()).translate('I sat on the bank.')
        self.assertEqual(['我坐在岸边。'], result['candidates'])

    def test_quota_never_becomes_candidate(self):
        for value in [{'responseStatus': 429}, {'responseStatus': 200, 'quotaFinished': True}]:
            with self.assertRaisesRegex(ServiceError, '额度'):
                FreeTranslator(lambda *args: json.dumps(value).encode()).translate('hello')

    def test_error_or_non_chinese_not_committed(self):
        for value in [{'responseStatus': 500}, {'responseStatus': 200, 'responseData': {'translatedText': 'Hello'}}]:
            with self.assertRaises(ServiceError):
                FreeTranslator(lambda *args: json.dumps(value).encode()).translate('hello')

    def test_encoding_and_limits(self):
        captured = []
        def fetch(url, limit):
            captured.append(url)
            return json.dumps({'responseStatus': 200, 'responseData': {'translatedText': '你好'}}).encode()
        FreeTranslator(fetch).translate('A & B?')
        self.assertIn('q=A+%26+B%3F', captured[0])
        for value in ['', '你好', 'x' * 501, 'hello\nworld']:
            with self.assertRaises(ServiceError):
                validate_text(value)

if __name__ == '__main__':
    unittest.main()
