"""Publish small, versioned dictionary shards; no model or user text involved."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def generate(source, destination):
    buckets = {f'{i:02x}': {} for i in range(256)}
    spelling = {}
    for row in source.read_text(encoding='utf-8-sig').splitlines():
        fields = row.split('\t')
        if len(fields) != 7 or row.startswith('#'):
            continue
        word, phonetic, root, form, rank, text, pos = fields
        key = hashlib.sha256(word.encode('ascii')).hexdigest()[:2]
        entry = buckets[key].setdefault(word, {'word': word, 'phonetic': phonetic,
            'root': root, 'form': form, 'rank': int(rank), 'candidates': []})
        entry['candidates'].append({'text': text, 'pos': pos})
        spelling[word] = int(rank)
    destination.mkdir(parents=True, exist_ok=True)
    manifest = {'schema': 1, 'entries': len(spelling), 'files': {}}
    for name, data in [('spelling.json', spelling)] + [(f'{key}.json', values) for key, values in buckets.items()]:
        content = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
        (destination / name).write_bytes(content)
        manifest['files'][name] = {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    (destination / 'ECDICT-LICENSE').write_bytes((ROOT / 'third_party/ECDICT-LICENSE').read_bytes())
    return manifest

if __name__ == '__main__':
    manifest = generate(ROOT / 'data/dictionary.tsv', ROOT / 'online-data/v1')
    print(json.dumps({'entries': manifest['entries'], 'data_bytes': sum(v['bytes'] for v in manifest['files'].values()),
        'maximum_shard_bytes': max(v['bytes'] for k, v in manifest['files'].items() if k != 'spelling.json')}))
