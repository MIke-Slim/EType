"""Free online providers; no bundled model, CUDA, numpy, or automatic paid fallback."""
import asyncio
from collections import OrderedDict
import hashlib
import html
import json
import os
from pathlib import Path
import re
import threading
import urllib.parse
import urllib.request

DICTIONARY_URL = 'https://raw.githubusercontent.com/MIke-Slim/EType/codex/online-lite/online-data/v1/'
MYMEMORY_URL = 'https://api.mymemory.translated.net/get'

class ServiceError(ValueError):
    pass

def validate_text(text):
    if not isinstance(text, str) or not text.strip() or len(text.encode('utf-8')) > 500:
        raise ServiceError('请输入不超过 500 字节的英文单词或句子')
    if not re.search('[A-Za-z]', text) or re.search(r'[\x00-\x1f\x7f]', text):
        raise ServiceError('请输入英文，不能包含控制字符')
    return text.strip()

def download(url, limit):
    request = urllib.request.Request(url, headers={'User-Agent': 'EType/online-lite', 'Accept': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            data = response.read(limit + 1)
        if len(data) > limit:
            raise ServiceError('在线服务返回内容过大')
        return data
    except ServiceError:
        raise
    except Exception as error:
        raise ServiceError('在线服务连接失败，英文已保留，请稍后重试') from error

class OnlineDictionary:
    def __init__(self, fetch=download):
        self.fetch = fetch
        self.manifest = None
        self.buckets = OrderedDict()
        self.spelling = None
        self.lock = threading.RLock()

    def resource(self, name):
        if self.manifest is None:
            self.manifest = json.loads(self.fetch(DICTIONARY_URL + 'manifest.json', 100000))
            if self.manifest.get('schema') != 1:
                raise ServiceError('在线词库版本不兼容')
        metadata = self.manifest['files'][name]
        size = metadata['bytes']
        if not isinstance(size, int) or not 0 < size <= 2000000:
            raise ServiceError('词库大小不正确')
        data = self.fetch(DICTIONARY_URL + name, size)
        if len(data) != size or hashlib.sha256(data).hexdigest() != metadata['sha256']:
            raise ServiceError('在线词库校验失败，请重试')
        return json.loads(data)

    def lookup(self, word):
        word = validate_text(word).lower()
        if not re.fullmatch(r"[a-z][a-z'-]{0,47}", word):
            raise ServiceError('单词模式请输入完整英文拼写')
        bucket = hashlib.sha256(word.encode('ascii')).hexdigest()[:2]
        with self.lock:
            if bucket not in self.buckets:
                self.buckets[bucket] = self.resource(bucket + '.json')
            self.buckets.move_to_end(bucket)
            while len(self.buckets) > 16:
                self.buckets.popitem(last=False)
            entry = self.buckets[bucket].get(word)
        if entry and (entry.get('word') != word or not entry.get('candidates')):
            raise ServiceError('词库返回内容不正确')
        return entry

    def corrections(self, word):
        word = validate_text(word).lower()
        if len(word) < 2 or len(word) > 48:
            return []
        with self.lock:
            if self.spelling is None:
                self.spelling = self.resource('spelling.json')
            words = self.spelling
        if word in words:
            return []
        # Search bounded edit operations; adjacent transpositions are included.
        alphabet = 'abcdefghijklmnopqrstuvwxyz'
        splits = [(word[:i], word[i:]) for i in range(len(word) + 1)]
        variants = {a + b[1:] for a, b in splits if b}
        variants.update(a + b[1] + b[0] + b[2:] for a, b in splits if len(b) > 1)
        variants.update(a + c + b[1:] for a, b in splits if b for c in alphabet)
        variants.update(a + c + b for a, b in splits for c in alphabet)
        return sorted(variants.intersection(words), key=lambda value: (words[value], value))[:5]

class FreeTranslator:
    def __init__(self, fetch=download):
        self.fetch = fetch

    def translate(self, text):
        text = validate_text(text)
        url = MYMEMORY_URL + '?' + urllib.parse.urlencode({'q': text, 'langpair': 'en|zh-CN'})
        data = json.loads(self.fetch(url, 300000))
        if data.get('quotaFinished') or str(data.get('responseStatus')) == '429':
            raise ServiceError('免费翻译额度已用完，英文已保留；不会转为付费服务')
        if str(data.get('responseStatus')) != '200':
            raise ServiceError('免费翻译服务暂时不可用，英文已保留')
        value = html.unescape(data.get('responseData', {}).get('translatedText', '')).strip()
        if not value or len(value) > 2000 or not re.search(r'[\u3400-\u9fff]', value) or re.search(r'[\x00-\x1f\x7f]', value):
            raise ServiceError('未获得有效中文译文，英文已保留')
        # Similar translation-memory segments are not alternate translations.
        return {'text': text, 'candidates': [value], 'provider': 'MyMemory'}

class OnlineSpeech:
    VOICES = {'female': 'en-US-AriaNeural', 'male': 'en-US-GuyNeural'}
    CACHE_LIMIT = 20 * 1024 * 1024

    def __init__(self, cache):
        self.cache = Path(cache)
        self.cache.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()

    def synthesize(self, text, voice='female', speed=1.0):
        text = validate_text(text)
        if voice not in self.VOICES or speed not in (1.0, 0.8):
            raise ServiceError('音色或语速不正确')
        key = hashlib.sha256(json.dumps([text, self.VOICES[voice], speed]).encode()).hexdigest()
        file = self.cache / (key + '.mp3')
        with self.lock:
            if not file.is_file():
                data = asyncio.run(self._synthesize(text, voice, speed))
                temporary = file.with_suffix('.tmp')
                try:
                    temporary.write_bytes(data)
                    os.replace(temporary, file)
                finally:
                    temporary.unlink(missing_ok=True)
            file.touch()
            files = sorted(self.cache.glob('*.mp3'), key=lambda value: value.stat().st_mtime)
            total = sum(value.stat().st_size for value in files)
            for old in files:
                if total <= self.CACHE_LIMIT:
                    break
                if old != file:
                    total -= old.stat().st_size
                    old.unlink()
        return {'file': str(file), 'provider': 'Edge online', 'voice': self.VOICES[voice]}

    async def _synthesize(self, text, voice, speed):
        import edge_tts
        result = bytearray()
        async def collect():
            communication = edge_tts.Communicate(text, self.VOICES[voice], rate='-20%' if speed == 0.8 else '+0%',
                proxy=os.environ.get('HTTPS_PROXY') or os.environ.get('HTTP_PROXY'))
            async for chunk in communication.stream():
                if chunk['type'] == 'audio':
                    result.extend(chunk['data'])
                    if len(result) > 8 * 1024 * 1024:
                        raise ServiceError('音频过大')
        try:
            await asyncio.wait_for(collect(), timeout=45)
        except Exception as error:
            raise ServiceError('在线语音暂时不可用，请重试') from error
        if len(result) < 100:
            raise ServiceError('在线语音未返回音频')
        return bytes(result)
