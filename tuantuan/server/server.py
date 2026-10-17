#!/usr/bin/env python3
"""Run: python3 server.py --config config.env. Core server needs Python stdlib only."""
import argparse
import hmac
import json
import os
import re
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
from services import Services
from store import Store
from configuration import save_model_key, validate_key

MAX_BODY = 650000
ID_PATTERN = re.compile(r'[a-zA-Z0-9_-]{1,64}\Z')

def load_config(path):
    config = {}
    if path.exists():
        for line in path.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith('#'):
                key, sep, value = line.partition('=')
                if not sep: raise ValueError('配置文件须为 KEY=VALUE 格式')
                config[key.strip()] = value.strip().strip('"').strip("'")
    config.update({k:v for k,v in os.environ.items() if k in (
        'HOST','PORT','DEVICE_TOKEN','DEEPSEEK_API_KEY','LLM_BASE_URL','LLM_MODEL','TIMEZONE',
        'ASR_MODEL','ASR_DEVICE','ASR_COMPUTE_TYPE','ASR_PRELOAD','ASR_BEAM_SIZE','ASR_CPU_THREADS','ASR_DISABLE_XET',
        'TTS_ENGINE','TTS_VOICE','TTS_RATE','TTS_CACHE_MB','PIPER_BIN','PIPER_MODEL','DEMO_MODE','DATA_DIR')})
    return config

class Companion:
    def __init__(self, config, store=None, services=None, config_path=None):
        self.config = config
        self.config_path = config_path
        self.store = store or Store(Path(config['DATA_DIR']) / 'tuantuan.sqlite3', config.get('TIMEZONE','Asia/Shanghai'))
        self.services = services or Services(config)
        # Single owner per device for turns, short independent DB locks for state polling.
        self.turn_lock = threading.Lock()
        self.audio_lock = threading.Lock()
        self.audio = {}
        self.limit = threading.BoundedSemaphore(4)

    def speech(self, device, text):
        pcm = self.services.synthesize(text)
        sid = secrets.token_hex(16)
        with self.audio_lock:
            now = time.monotonic()
            self.audio = {k:v for k,v in self.audio.items() if v[0] > now}
            # Bound memory even when clients do not consume audio.
            if len(self.audio) >= 8:
                self.audio.pop(next(iter(self.audio)))
            self.audio[sid] = (now+180, device, pcm)
        return sid

    def state(self, device):
        self.store.rollover(device)
        ready = getattr(self.services,'readiness',{'asr':'ready','tts':True,'llm':True})
        return {'tasks': self.store.tasks(device), 'reminders':self.store.due(device),
                'now':int(self.store.now().timestamp()), 'timezone':self.store.zone.key,
                'speech_ready':ready['asr']=='ready' and ready['tts'] and ready['llm'],
                'speech_status':ready,
                'demo':self.services.demo}

    def turn(self, device, turn_id, data, is_audio):
        if not ID_PATTERN.fullmatch(turn_id): raise ValueError('请求编号无效')
        if not self.turn_lock.acquire(blocking=False): raise BusyError('正在处理对话，请稍后再试')
        try:
            self.store.rollover(device)
            cached = self.store.cached(device, turn_id)
            if cached:
                result = cached
            else:
                text = self.services.transcribe(data) if is_audio else data.get('text')
                if not isinstance(text, str) or not 1 <= len(text.strip()) <= 1000:
                    raise ValueError('请说一句话')
                plan = self.services.plan(text.strip(), self.store.history(device), self.store.tasks(device), self.store.now())
                result = self.store.apply_plan(device, turn_id, text.strip(), plan)
            # Mutations are already durable: TTS failure must not turn a successful add into a retry.
            result = dict(result)
            try:
                result['speech_id'] = self.speech(device, result['reply'])
            except (RuntimeError, ValueError) as exc:
                result['audio_error'] = str(exc)
            return result
        finally:
            self.turn_lock.release()

    def configure_key(self,data):
        key=validate_key(data.get('key'))
        if not self.config_path: raise RuntimeError('电脑端未启用配置保存')
        if 'DEEPSEEK_API_KEY' in os.environ: raise RuntimeError('该 Key 由电脑环境变量管理，请在电脑上修改')
        if not self.turn_lock.acquire(blocking=False): raise BusyError('正在处理对话，请稍后保存 Key')
        try:
            save_model_key(self.config_path,key)
            self.config['DEEPSEEK_API_KEY']=key
            return {'ok':True,'message':'API Key 已保存到电脑端'}
        finally: self.turn_lock.release()

class BusyError(RuntimeError):
    pass

def handler_for(app):
    class Handler(BaseHTTPRequestHandler):
        server_version = 'Tuantuan/0.1'
        def log_message(self, fmt, *args):
            # Do not log request bodies, authorization headers, transcripts, or query strings.
            print('%s %s' % (self.command, urlsplit(self.path).path), flush=True)

        def log_request(self, code='-', size='-'):
            # Status codes distinguish network success from authentication errors.
            print('%s %s %s' % (code, self.command, urlsplit(self.path).path), flush=True)

        def reply(self, status, body, content='application/json; charset=utf-8'):
            payload = json.dumps(body, ensure_ascii=False).encode() if isinstance(body, (dict,list)) else body
            self.send_response(status)
            self.send_header('Content-Type',content)
            self.send_header('Content-Length',str(len(payload)))
            self.send_header('Cache-Control','no-store')
            self.end_headers()
            self.wfile.write(payload)

        def authorized(self):
            expected = 'Bearer ' + app.config['DEVICE_TOKEN']
            if not hmac.compare_digest(self.headers.get('Authorization',''), expected):
                self.reply(401, {'error':'连接口令无效'})
                return None
            device = self.headers.get('X-Device-Id','')
            if not ID_PATTERN.fullmatch(device):
                self.reply(400, {'error':'设备编号无效'})
                return None
            return device

        def do_GET(self):
            path = urlsplit(self.path).path
            if path == '/health':
                return self.reply(200, {'ok':True, 'version':'1.0.2','demo':app.services.demo,
                                       'speech_status':getattr(app.services,'readiness',{})})
            device = self.authorized()
            if not device: return
            if path == '/v1/state':
                return self.reply(200, app.state(device))
            if path.startswith('/v1/speech/'):
                sid = path.removeprefix('/v1/speech/')
                with app.audio_lock:
                    item = app.audio.get(sid)
                if not item or item[0] < time.monotonic() or item[1] != device:
                    return self.reply(404, {'error':'语音已过期'})
                return self.reply(200, item[2], 'application/octet-stream')
            self.reply(404, {'error':'接口不存在'})

        def do_POST(self):
            device = self.authorized()
            if not device: return
            self.connection.settimeout(10)
            if not app.limit.acquire(blocking=False):
                return self.reply(503, {'error':'服务繁忙，请稍后重试'})
            try:
                length = int(self.headers.get('Content-Length','0'))
                if not 0 < length <= MAX_BODY:
                    return self.reply(413, {'error':'请求大小无效'})
                raw = self.rfile.read(length)
                if len(raw) != length: raise ValueError('请求不完整')
                path = urlsplit(self.path).path
                if path == '/v1/audio':
                    result = app.turn(device, self.headers.get('X-Turn-Id',''), raw, True)
                else:
                    data = json.loads(raw)
                    if not isinstance(data,dict): raise ValueError('请求格式无效')
                    if path == '/v1/text':
                        result = app.turn(device, data.get('turn_id',''), data, False)
                    elif path == '/v1/task':
                        result = {'tasks':app.store.task_action(device, data.get('id'),data.get('action'),data.get('seconds',600))}
                    elif path == '/v1/config/key':
                        result = app.configure_key(data)
                    elif path == '/v1/reminder':
                        task = next((t for t in app.store.tasks(device) if t['id'] == data.get('id')),None)
                        if not task or not task['due'] or task['due'] > app.store.now().timestamp():
                            raise ValueError('这条提醒尚未到时间')
                        result = {'reply':'时间到了，记得' + task['title'] + '。', 'speech_id':None}
                        try:
                            result['speech_id'] = app.speech(device, result['reply'])
                        except (RuntimeError,ValueError) as exc:
                            result['audio_error'] = str(exc)
                    else:
                        return self.reply(404, {'error':'接口不存在'})
                self.reply(200, result)
            except BusyError as exc:
                self.reply(503, {'error':str(exc)})
            except (ValueError, TypeError, json.JSONDecodeError) as exc:
                self.reply(400, {'error':str(exc)})
            except RuntimeError as exc:
                self.reply(502, {'error':str(exc)})
            except TimeoutError:
                self.reply(408, {'error':'请求超时'})
            except Exception:
                # Unexpected exceptions are reported without exposing provider data or keys.
                self.reply(500, {'error':'服务发生错误，请查看电脑端状态'})
            finally:
                app.limit.release()
    return Handler

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('config.env'))
    args = parser.parse_args()
    config = load_config(args.config)
    token = config.get('DEVICE_TOKEN','')
    if len(token) < 16 or token == 'replace-with-a-long-random-token':
        parser.error('请在 config.env 中设置至少16位的 DEVICE_TOKEN')
    config.setdefault('DATA_DIR', str(Path(__file__).with_name('data')))
    Path(config['DATA_DIR']).mkdir(mode=0o700, parents=True, exist_ok=True)
    os.umask(0o077)
    app = Companion(config,config_path=args.config)
    app.services.start_warmup()
    server = ThreadingHTTPServer((config.get('HOST','127.0.0.1'),int(config.get('PORT','8765'))), handler_for(app))
    server.daemon_threads = True
    print(f'团团服务已启动，端口 {server.server_port}；演示模式={app.services.demo}', flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        server.server_close()
        app.store.close()

if __name__ == '__main__': main()
