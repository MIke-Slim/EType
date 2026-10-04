"""Validate generated offline dictionary data and reviewed ambiguous forms."""
import hashlib
import json
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
DATA = ROOT / 'data' / 'dictionary.tsv'


class DictionaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = {}
        cls.metadata = {}
        cls.row_count = 0
        with DATA.open(encoding='utf-8') as source:
            for line in source:
                if line.startswith('#'):
                    continue
                fields = line.rstrip('\n').split('\t')
                if len(fields) != 7:
                    raise AssertionError('dictionary record does not have seven fields')
                word, ipa, root, form, rank, text, pos = fields
                if not re.fullmatch(r"[a-z]+(?:[-'][a-z]+)*", word) or len(word) > 48:
                    raise AssertionError('invalid word token: ' + word)
                metadata = (ipa, root, form, int(rank))
                if word in cls.metadata and cls.metadata[word] != metadata:
                    raise AssertionError('inconsistent metadata: ' + word)
                cls.metadata[word] = metadata
                pair = (text, pos)
                senses = cls.entries.setdefault(word, [])
                if pair in senses:
                    raise AssertionError('duplicate candidate: ' + word + ' / ' + text)
                senses.append(pair)
                cls.row_count += 1

    def texts(self, word):
        return [text for text, _ in self.entries[word]]

    def test_manifest_matches_artifact(self):
        manifest = json.loads((ROOT / 'data' / 'manifest.json').read_text(encoding='utf-8'))
        self.assertEqual(manifest['entry_count'], len(self.entries))
        self.assertEqual(manifest['candidate_count'], self.row_count)
        self.assertEqual(manifest['dictionary_sha256'], hashlib.sha256(DATA.read_bytes()).hexdigest())

    def test_all_forms_have_existing_roots(self):
        for word, (_, root, form, _) in self.metadata.items():
            if root:
                self.assertIn(root, self.entries, word)
                self.assertTrue(form, word)

    def test_direct_homograph_meanings_survive(self):
        for word, direct, inflected in [('saw', '锯子', '看见'), ('left', '左边的', '离开'), ('wound', '伤口', '缠绕'), ('lay', '放置', '躺着')]:
            self.assertIn(direct, self.texts(word), word)
            self.assertIn(inflected, self.texts(word), word)

    def test_invalid_homograph_senses_do_not_propagate(self):
        for word, rejected in [('lay', '说谎'), ('lain', '说谎'), ('laid', '躺着'), ('laying', '躺着'), ('wound', '风'), ('wound', '嗅出'), ('wounded', '缠绕'), ('wounding', '上发条'), ('left', '生叶')]:
            self.assertNotIn(rejected, self.texts(word), word)

    def test_inflected_verbs_do_not_inherit_nouns(self):
        for word, invalid in [('went', '去'), ('seen', '主教的职位'), ('eaten', '吃')]:
            self.assertFalse(any(text == invalid and pos == '名词' for text, pos in self.entries[word]), word)

    def test_comparatives_preserve_degree(self):
        for word, expected, root_meaning in [('better', '更好', '好'), ('best', '最好', '好'), ('worse', '更坏', '坏'), ('worst', '最坏', '坏')]:
            self.assertIn(expected, self.texts(word))
            self.assertNotIn(root_meaning, self.texts(word))

    def test_reviewed_roots_lead_but_direct_senses_remain(self):
        self.assertEqual(self.texts('books')[0], '书')
        self.assertIn(('预订', '动词'), self.entries['books'])
        self.assertEqual(self.texts('running')[0], '跑步')
        self.assertIn(('运转', '名词'), self.entries['running'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
