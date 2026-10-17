"""ASR/LLM/TTS adapters. Speech stays local; only text is sent to the configured LLM."""
import io
import json
import logging
import os
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request
import wave
from collections import OrderedDict
from pathlib import Path

class _InvalidModelResponse(ValueError):
    """A retryable response-shape error; its message never contains provider text."""

def _parse_plan(raw):
    try:
        envelope = json.loads(raw)
        choices = envelope['choices']
        if not isinstance(choices, list) or not choices or not isinstance(choices[0], dict):
            raise _InvalidModelResponse('missing_choice')
        choice = choices[0]
        finish = choice.get('finish_reason')
        if finish == 'length':
            # Even valid-looking JSON can contain only part of the intended actions.
            raise _InvalidModelResponse('truncated')
        if finish == 'content_filter':
            raise RuntimeError('这句话暂时无法回答，请换个话题')
        if finish not in (None, 'stop'):
            raise RuntimeError('对话服务中断了回答，请再说一次')
        content = choice['message']['content']
        if not isinstance(content, str) or not content.strip():
            raise _InvalidModelResponse('empty_content')
        content = content.strip()
        # Some compatible providers wrap their JSON in one code fence in text
        # mode. Accept only a whole fenced document, never a JSON fragment in prose.
        lines = content.splitlines()
        if len(lines) >= 3 and lines[0].strip().lower() in ('```json', '```') and lines[-1].strip() == '```':
            content = '\n'.join(lines[1:-1])
        plan = json.loads(content)
    except (KeyError, IndexError, TypeError, json.JSONDecodeError, UnicodeDecodeError):
        raise _InvalidModelResponse('invalid_json') from None
    if not isinstance(plan, dict):
        raise _InvalidModelResponse('invalid_plan')
    reply, actions = plan.get('reply'), plan.get('actions', [])
    if not isinstance(reply, str) or not reply.strip() or len(reply) > 300:
        raise _InvalidModelResponse('invalid_reply')
    if not isinstance(actions, list) or len(actions) > 4 or any(not isinstance(item, dict) for item in actions):
        raise _InvalidModelResponse('invalid_actions')
    return plan

class Services:
    def __init__(self, config):
        self.config = config
        self.asr = None
        self.asr_lock = threading.Lock()
        self.tts_lock = threading.Lock()
        self.status_lock = threading.Lock()
        self.asr_status = 'cold'
        self.warmup_thread = None
        self.tts_cache = OrderedDict()
        self.tts_cache_bytes = 0
        self.demo = config.get('DEMO_MODE') == '1'

    @property
    def readiness(self):
        with self.status_lock:
            asr = self.asr_status
        return {'asr':asr, 'tts':self.config.get('TTS_ENGINE','say') != 'none',
                'llm':bool(self.config.get('DEEPSEEK_API_KEY'))}

    def _load_asr(self):
        # Caller owns asr_lock. Readiness uses a separate short lock so polling
        # remains responsive while a model downloads or loads.
        if self.asr is not None:
            return
        with self.status_lock:
            self.asr_status = 'loading'
        try:
            if self.config.get('ASR_DISABLE_XET','1') == '1':
                os.environ.setdefault('HF_HUB_DISABLE_XET','1')
            from faster_whisper import WhisperModel
            self.asr = WhisperModel(self.config.get('ASR_MODEL','small'),
                                    device=self.config.get('ASR_DEVICE','cpu'),
                                    compute_type=self.config.get('ASR_COMPUTE_TYPE','int8'),
                                    cpu_threads=int(self.config.get('ASR_CPU_THREADS','4')),
                                    download_root=str(Path(self.config['DATA_DIR']) / 'models'))
        except ImportError:
            with self.status_lock: self.asr_status = 'error'
            raise RuntimeError('请安装电脑端语音识别依赖') from None
        except Exception:
            with self.status_lock: self.asr_status = 'error'
            raise RuntimeError('语音识别模型加载失败，请检查电脑端网络或模型目录') from None
        with self.status_lock: self.asr_status = 'ready'

    def start_warmup(self):
        if self.demo or self.config.get('ASR_PRELOAD','1') != '1':
            return
        with self.status_lock:
            if self.warmup_thread is not None:
                return
            def warmup():
                try:
                    with self.asr_lock: self._load_asr()
                    logging.getLogger(__name__).info('ASR model ready')
                except RuntimeError:
                    logging.getLogger(__name__).error('ASR model warmup failed; check network or local model path')
            self.warmup_thread = threading.Thread(target=warmup, name='tuantuan-asr', daemon=True)
            self.warmup_thread.start()

    def transcribe(self, data):
        try:
            with wave.open(io.BytesIO(data)) as wav:
                if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, 16000):
                    raise ValueError('录音须为 16kHz、单声道、16bit WAV')
                if not 0.2 <= wav.getnframes() / 16000 <= 20.1:
                    raise ValueError('录音时长须为 0.2–20 秒')
                pcm = wav.readframes(wav.getnframes())
                if len(pcm) != wav.getnframes() * 2:
                    raise ValueError('录音数据不完整，请再说一次')
        except (wave.Error, EOFError) as exc:
            raise ValueError('录音格式无效') from exc
        if self.demo:
            raise ValueError('演示模式只支持文字，录音需要启用 ASR')
        with self.asr_lock:
            self._load_asr()
            # The device already supplies validated 16 kHz PCM. Feed samples
            # directly, avoiding an unnecessary media decoder and PyAV ABI drift.
            import numpy as np
            samples = np.frombuffer(pcm, dtype='<i2').astype(np.float32) / 32768.0
            segments, _ = self.asr.transcribe(samples, language='zh', beam_size=int(self.config.get('ASR_BEAM_SIZE','3')),
                                              vad_filter=True, condition_on_previous_text=False)
            text = ''.join(segment.text for segment in segments).strip()
        if not text:
            raise ValueError('没有听清，请再说一次')
        if len(text) > 1000:
            raise ValueError('这句话太长，请分几次说')
        return text

    def plan(self, text, history, tasks, now):
        if self.demo:
            return {'reply': '我是团团。我在演示模式陪着你；配置 DeepSeek 后就可以聊天和理解任务啦。',
                    'emotion':'happy', 'actions':[]}
        key = self.config.get('DEEPSEEK_API_KEY', '')
        if not key:
            raise RuntimeError('电脑端尚未配置 DeepSeek API Key')
        instructions = '''你是中文桌面陪伴助手“团团”，温柔自然，不扮演人类，不造成依赖。日常回答简短，最多100字。
只输出 JSON 对象，示例：{"reply":"我在这里。","emotion":"happy","actions":[]}。
emotion 只能 happy/calm/curious。actions 最多4项，只在用户明确要求时执行任务操作。
新增任务：{"action":"add","title":"提交周报","due":"2026-10-14T09:00:00+08:00","repeat":"none"}。
due 为带时区的 ISO8601 时间，无定时的待办 due 为 null；repeat 只能 none/daily/weekdays/weekly。
完成任务：{"action":"complete","id":1}；删除：{"action":"delete","id":1}；查询：{"action":"list"}。
只有已有任务中的 id 可以用于完成/删除。无法唯一定位任务、时间不明确时先提问，actions=[]。
相对日期以提供的当前时间为准。每天/工作日/每周提醒用相应 repeat 和下一个未来时间。
不执行其它命令或外部操作，不声称完成任务写入；任务实际执行结果会由程序追加。
不要擅自设定时间；不能从用户闲聊中擅自新增任务。不要输出Markdown。'''
        context = '\n当前时间：' + now.isoformat() + '\n当前任务：' + json.dumps(tasks, ensure_ascii=False)
        # Keep assistant history consistent with the JSON format requested now.
        # Stored history remains natural spoken text; previous actions are never replayed.
        model_history = [dict(message, content=json.dumps({'reply':message['content'], 'emotion':'calm', 'actions':[]},ensure_ascii=False))
                         if message['role']=='assistant' else dict(message) for message in history]
        payload = {'model': self.config.get('LLM_MODEL', 'deepseek-flash'),
                   'messages':[{'role':'system', 'content':instructions + context}] + model_history + [{'role':'user','content':text}],
                   'response_format':{'type':'json_object'}, 'max_tokens':2048, 'stream':False}
        # DeepSeek enables thinking by default, sharing the output-token budget
        # with the final JSON. Desktop conversation needs a prompt final answer.
        if payload['model'].startswith('deepseek-'):
            payload['thinking'] = {'type':'disabled'}
        endpoint = self.config.get('LLM_BASE_URL','https://api.deepseek.com').rstrip('/') + '/chat/completions'
        deadline = time.monotonic() + 45
        for attempt in range(2):
            request_payload = dict(payload, max_tokens=2048 * (attempt + 1))
            if attempt:
                # DeepSeek JSON mode can return empty content on some histories.
                # Bypass that decoder on the bounded retry, retaining the exact
                # same strict JSON parser and transactional action executor.
                request_payload.pop('response_format', None)
                request_payload['messages'] = list(payload['messages'])
                request_payload['messages'][0] = dict(payload['messages'][0], content=instructions + context +
                    '\n请返回一个完整 JSON 对象，包含非空 reply、emotion 和 actions 数组。不要返回空内容、思考文字或 Markdown。')
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError('对话服务连接超时，请稍后重试')
            req = urllib.request.Request(endpoint, data=json.dumps(request_payload).encode(),
                                         headers={'Authorization':'Bearer ' + key,'Content-Type':'application/json'})
            try:
                with urllib.request.urlopen(req, timeout=min(30, remaining)) as response:
                    raw = response.read(262145)
                    if len(raw) > 262144:
                        raise RuntimeError('模型响应过大')
                return _parse_plan(raw)
            except urllib.error.HTTPError as exc:
                raise RuntimeError(f'对话服务返回 HTTP {exc.code}，请检查电脑端配置或余额') from None
            except (urllib.error.URLError, TimeoutError):
                raise RuntimeError('对话服务连接超时，请稍后重试') from None
            except _InvalidModelResponse as exc:
                logging.getLogger(__name__).warning('LLM response invalid: reason=%s attempt=%d',str(exc),attempt+1)
                # No actions or history have been saved yet. Only a validated plan
                # is handed to the transactional executor, exactly once.
                if attempt:
                    raise RuntimeError('对话服务暂未返回完整回答，请再说一次') from None

    def synthesize(self, text):
        if not text or len(text) > 500:
            raise ValueError('朗读文本过长')
        key = (text, self.config.get('TTS_ENGINE','say'), self.config.get('TTS_VOICE','Tingting'),
               self.config.get('TTS_RATE','180'), self.config.get('PIPER_BIN','piper'), self.config.get('PIPER_MODEL',''))
        limit = max(0,min(32,int(self.config.get('TTS_CACHE_MB','8')))) * 1024 * 1024
        with self.tts_lock:
            while self.tts_cache and self.tts_cache_bytes > limit:
                _, old = self.tts_cache.popitem(last=False)
                self.tts_cache_bytes -= len(old)
            if key in self.tts_cache:
                self.tts_cache.move_to_end(key)
                return self.tts_cache[key]
            pcm = self._synthesize_uncached(text)
            if len(pcm) <= limit:
                while self.tts_cache and self.tts_cache_bytes + len(pcm) > limit:
                    _, old = self.tts_cache.popitem(last=False)
                    self.tts_cache_bytes -= len(old)
                self.tts_cache[key] = pcm
                self.tts_cache_bytes += len(pcm)
            return pcm

    def _synthesize_uncached(self, text):
        engine = self.config.get('TTS_ENGINE', 'say')
        if engine == 'none':
            raise RuntimeError('语音合成尚未启用')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'voice.wav'
            try:
                if engine == 'say':
                    aiff = Path(tmp) / 'voice.aiff'
                    subprocess.run(['say', '-v', self.config.get('TTS_VOICE', 'Tingting'),
                                    '-r', self.config.get('TTS_RATE','180'), '-o', str(aiff), '--', text],
                                   check=True, capture_output=True, timeout=35)
                    subprocess.run(['afconvert', '-f', 'WAVE', '-d', 'LEI16@16000', '-c', '1',
                                    str(aiff), str(src)], check=True, capture_output=True, timeout=10)
                elif engine == 'piper':
                    model = self.config.get('PIPER_MODEL')
                    if not model:
                        raise RuntimeError('尚未配置 Piper 中文语音模型')
                    subprocess.run([self.config.get('PIPER_BIN','piper'), '--model',model,
                                    '--output_file',str(src)], input=text.encode(),
                                   check=True, capture_output=True, timeout=45)
                else:
                    raise RuntimeError('不支持的 TTS 引擎')
            except FileNotFoundError:
                raise RuntimeError('找不到语音合成程序，请检查 TTS_ENGINE') from None
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                raise RuntimeError('语音合成失败，请检查音色配置') from None
            with wave.open(str(src)) as wav:
                if wav.getsampwidth() != 2 or wav.getnchannels() != 1:
                    raise RuntimeError('语音合成输出格式不受支持')
                rate, pcm = wav.getframerate(), wav.readframes(wav.getnframes())
            # Linear resampling for Piper's usual 22.05kHz voice; no audioop dependency.
            if rate != 16000:
                from array import array
                import sys
                samples = array('h', pcm)
                if sys.byteorder != 'little': samples.byteswap()
                count = len(samples) * 16000 // rate
                out = array('h')
                for index in range(count):
                    pos = index * rate / 16000
                    low = int(pos)
                    high = min(low + 1, len(samples)-1)
                    out.append(round(samples[low] + (samples[high]-samples[low]) * (pos-low)))
                if sys.byteorder != 'little': out.byteswap()
                pcm = out.tobytes()
            if not pcm or len(pcm) > 16000 * 2 * 90:
                raise RuntimeError('合成语音为空或超过90秒')
            return pcm
