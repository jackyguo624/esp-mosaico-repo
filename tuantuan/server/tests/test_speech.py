import io
import sys
import threading
import types
import unittest
import wave
from unittest.mock import patch
from services import Services

class SpeechTests(unittest.TestCase):
    def test_preload_is_single_and_readiness_is_nonblocking(self):
        started=threading.Event();release=threading.Event();calls=[]
        def load(*args,**kwargs):
            calls.append(kwargs);started.set();release.wait(2);return object()
        s=Services({'DATA_DIR':'unused','DEEPSEEK_API_KEY':'dummy-only'})
        with patch.dict(sys.modules,{'faster_whisper':types.SimpleNamespace(WhisperModel=load)}):
            s.start_warmup();self.assertTrue(started.wait(1));s.start_warmup()
            self.assertEqual(s.readiness['asr'],'loading')
            release.set();s.warmup_thread.join(2)
        self.assertEqual(len(calls),1);self.assertEqual(s.readiness['asr'],'ready')
        self.assertEqual(calls[0]['cpu_threads'],4)

    def test_model_failure_does_not_expose_provider_details(self):
        s=Services({'DATA_DIR':'unused'})
        def fail(*a,**kw):raise Exception('private-download-url-and-token')
        with patch.dict(sys.modules,{'faster_whisper':types.SimpleNamespace(WhisperModel=fail)}):
            with self.assertRaises(RuntimeError) as caught:s._load_asr()
        self.assertNotIn('private',str(caught.exception));self.assertEqual(s.readiness['asr'],'error')

    def test_tts_cache_eviction_engine_change_and_disable(self):
        s=Services({'TTS_CACHE_MB':'1'})
        with patch.object(s,'_synthesize_uncached',side_effect=lambda t:bytes(t,'ascii')*600000) as synth:
            s.synthesize('a');s.synthesize('a');self.assertEqual(synth.call_count,1)
            s.synthesize('b');s.synthesize('a');self.assertEqual(synth.call_count,3)
            self.assertLessEqual(s.tts_cache_bytes,1024*1024)
            s.config['TTS_RATE']='200';s.synthesize('a');self.assertEqual(synth.call_count,4)
            s.config['TTS_CACHE_MB']='0';s.synthesize('a');s.synthesize('a')
            self.assertEqual(synth.call_count,6);self.assertEqual(s.tts_cache_bytes,0)

    def test_wav_samples_bypass_media_decoder_and_truncation_is_rejected(self):
        try:import numpy as np
        except ImportError:self.skipTest('NumPy is installed with the optional ASR dependencies')
        stream=io.BytesIO()
        pcm=(b'\x00\x80\xff\x7f')*2000
        with wave.open(stream,'wb') as w:
            w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(pcm)
        s=Services({});received=[]
        def transcribe(samples,**kwargs):
            received.append((samples,kwargs));return [types.SimpleNamespace(text='喝水')],None
        s.asr=types.SimpleNamespace(transcribe=transcribe)
        self.assertEqual(s.transcribe(stream.getvalue()),'喝水')
        samples,kwargs=received[0]
        self.assertIsInstance(samples,np.ndarray);self.assertEqual(samples.dtype,np.float32)
        self.assertEqual(samples[0],-1);self.assertLess(samples[1],1)
        self.assertEqual(kwargs['language'],'zh');self.assertTrue(kwargs['vad_filter'])
        with self.assertRaises(ValueError):s.transcribe(stream.getvalue()[:-2])
        self.assertEqual(len(received),1)
