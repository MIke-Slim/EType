"""Isolated online development worker. Does not install or start a local model."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import sys
import threading
from online_services import FreeTranslator, OnlineDictionary, OnlineSpeech, ServiceError, QuotaError

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=49182)
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/online-lite/audio')
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('Invalid port')
    dictionary, translator, speech = OnlineDictionary(), FreeTranslator(), OnlineSpeech(args.cache)
    active = threading.BoundedSemaphore(4)
    origin = f'http://127.0.0.1:{args.port}'

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *values):
            # pythonw has no stderr. Logging must not abort a valid HTTP response.
            if sys.stderr is not None:
                super().log_message(format, *values)

        def send(self, status, body, content_type='application/json; charset=utf-8'):
            if not isinstance(body, bytes):
                body = json.dumps(body, ensure_ascii=False).encode('utf-8')
            try:
                self.send_response(status)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-store')
                self.send_header('X-Content-Type-Options', 'nosniff')
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def local(self):
            return self.headers.get('Host') == f'127.0.0.1:{args.port}' and self.headers.get('Origin') in (None, origin)

        def do_GET(self):
            if not self.local():
                return self.send(403, {'error': '仅允许本机访问'})
            if self.path == '/health':
                self.send(200, {'status': 'ok', 'native_protocol': 1, 'online_only': True,
                    'translation': 'MyMemory', 'speech': 'Edge online'})
            elif self.path == '/':
                self.send(200, (ROOT / 'assets/online-lab.html').read_bytes(), 'text/html; charset=utf-8')
            elif re.fullmatch('/audio/[0-9a-f]{64}\\.mp3', self.path):
                file = args.cache / self.path.rsplit('/', 1)[1]
                if file.is_file():
                    self.send(200, file.read_bytes(), 'audio/mpeg')
                else:
                    self.send(404, {'error': '音频缓存已清除，请重新朗读'})
            else:
                self.send(404, {'error': '不存在'})

        def do_POST(self):
            if not self.local() or self.headers.get('X-EType-Lab') != '1':
                return self.send(403, {'error': '仅允许本机界面发起操作'})
            if not active.acquire(False):
                return self.send(503, {'error': '正在处理，请稍后重试'})
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 8192:
                    raise ServiceError('请求大小不正确')
                payload = self.rfile.read(length)
                native = self.path.startswith('/native/')
                if native:
                    text = payload.decode('utf-8')
                else:
                    data = json.loads(payload)
                    text = data.get('text', '')
                if self.path in ('/word', '/native/word'):
                    entry = dictionary.lookup(text)
                    if native:
                        if not entry:
                            result = '#MISSING'
                        else:
                            result = '\n'.join('\t'.join([entry['word'], entry['phonetic'], entry['root'], entry['form'],
                                str(entry['rank']), c['text'], c['pos']]) for c in entry['candidates'])
                        self.send(200, result.encode('utf-8'), 'text/plain; charset=utf-8')
                    else:
                        self.send(200, {'entry': entry, 'provider': 'EType online dictionary'})
                elif self.path in ('/correct', '/native/correct'):
                    corrections = dictionary.corrections(text)
                    if native:
                        self.send(200, '\n'.join(corrections).encode(), 'text/plain; charset=utf-8')
                    else:
                        self.send(200, {'corrections': corrections})
                elif self.path in ('/translate', '/native/translate'):
                    result = translator.translate(text)
                    if native:
                        self.send(200, '\n'.join(result['candidates']).encode('utf-8'), 'text/plain; charset=utf-8')
                    else:
                        self.send(200, result)
                elif self.path in ('/speak', '/native/speak'):
                    voice = self.headers.get('X-EType-Voice', 'female') if native else data.get('voice', 'female')
                    speed = float(self.headers.get('X-EType-Speed', '1')) if native else data.get('speed', 1.0)
                    accent = self.headers.get('X-EType-Accent', 'us') if native else data.get('accent', 'us')
                    result = speech.synthesize(text, voice, speed, accent)
                    if native:
                        self.send(200, Path(result['file']).read_bytes(), 'audio/mpeg')
                    else:
                        result['audio_url'] = '/audio/' + Path(result.pop('file')).name
                        self.send(200, result)
                else:
                    self.send(404, {'error': '不存在'})
            except QuotaError as error:
                self.send(429, {'error': str(error)})
            except (ServiceError, ValueError, KeyError, TypeError) as error:
                self.send(400, {'error': str(error)})
            except Exception:
                self.send(503, {'error': '在线服务暂时不可用，英文已保留，请重试'})
            finally:
                active.release()

    if sys.stdout is not None:
        print(f'Online preview: {origin}', flush=True)
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()

if __name__ == '__main__':
    main()
