import io
import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import wave
from datetime import datetime, timedelta
from http.server import ThreadingHTTPServer
from pathlib import Path
from services import Services
from store import Store
from server import Companion, handler_for

class FakeServices:
    demo=False
    def __init__(self): self.calls=0
    def plan(self,text,history,tasks,now):
        self.calls+=1
        return {'reply':'已理解','emotion':'happy','actions':[{'action':'add','title':'提交周报','due':(now+timedelta(hours=1)).isoformat(),'repeat':'none'}]}
    def synthesize(self,text): return b'\0\0'*160

class TaskTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.path=Path(self.tmp.name)/'db'
        self.store=Store(self.path)
    def tearDown(self): self.store.close();self.tmp.cleanup()
    def add(self,title='喝水',repeat='none',due=True,device='board1',turn='1'):
        return self.store.apply_plan(device,turn,'提醒我喝水',{'reply':'好','actions':[
            {'action':'add','title':title,'due':(self.store.now()+timedelta(hours=1)).isoformat() if due else None,'repeat':repeat}]})
    def test_idempotent_turn(self):
        self.add();self.add();self.assertEqual(len(self.store.tasks('board1')),1)
    def test_persistence(self):
        self.add();self.store.close();self.store=Store(self.path)
        self.assertEqual(self.store.tasks('board1')[0]['title'],'喝水')
    def test_cross_device_ids_rejected(self):
        ident=self.add()['tasks'][0]['id']
        with self.assertRaises(ValueError): self.store.task_action('board2',ident,'delete')
        self.assertEqual(len(self.store.tasks('board1')),1)
    def test_invalid_action_rolls_back(self):
        plan={'reply':'好','actions':[{'action':'add','title':'有效事项','due':None}, {'action':'delete','id':999}]}
        with self.assertRaises(ValueError):self.store.apply_plan('board1','1','test',plan)
        self.assertEqual(self.store.tasks('board1'),[])
        self.assertIsNone(self.store.cached('board1','1'))
    def test_past_time_rejected(self):
        with self.assertRaises(ValueError): self.store.validate_add({'title':'喝水','due':(self.store.now()-timedelta(days=1)).isoformat()})
    def test_ambiguous_naive_time_rejected(self):
        with self.assertRaises(ValueError): self.store.validate_add({'title':'喝水','due':'2027-01-01T09:00:00'})
    def test_weekdays_advance_skips_weekend(self):
        self.store.now=lambda:datetime(2026,10,16,18,0,tzinfo=self.store.zone) # Friday
        result=self.add(repeat='weekdays');t=result['tasks'][0]
        self.store.now=lambda:datetime(2026,10,16,20,0,tzinfo=self.store.zone)
        self.store.task_action('board1',t['id'],'complete')
        due=datetime.fromtimestamp(self.store.tasks('board1')[0]['due'],self.store.zone)
        self.assertEqual(due.weekday(),0);self.assertEqual(due.hour,19)
    def test_snooze_preserves_daily_time(self):
        self.store.now=lambda:datetime(2026,10,13,8,0,tzinfo=self.store.zone)
        task=self.add(repeat='daily')['tasks'][0]
        self.store.now=lambda:datetime(2026,10,13,9,1,tzinfo=self.store.zone)
        tasks=self.store.task_action('board1',task['id'],'snooze',600)
        original=next(t for t in tasks if t['id']==task['id'])
        due=datetime.fromtimestamp(original['due'],self.store.zone)
        self.assertEqual((due.day,due.hour,due.minute),(14,9,0))
        self.assertEqual(len(tasks),2)
    def test_ack_suppresses_once(self):
        t=self.add()['tasks'][0];self.store.now=lambda:datetime.fromtimestamp(t['due']+1,self.store.zone)
        self.assertEqual(len(self.store.due('board1')),1)
        self.store.task_action('board1',t['id'],'ack')
        self.assertEqual(self.store.due('board1'),[])
    def test_daily_reminder_rolls_when_next_occurrence_is_due(self):
        self.store.now=lambda:datetime(2026,10,13,8,0,tzinfo=self.store.zone)
        t=self.add(repeat="daily")["tasks"][0]
        self.store.now=lambda:datetime(2026,10,13,9,1,tzinfo=self.store.zone)
        self.store.task_action("board1",t["id"],"ack")
        self.assertEqual(self.store.due("board1"),[])
        self.store.now=lambda:datetime(2026,10,14,9,1,tzinfo=self.store.zone)
        self.store.rollover("board1")
        due=datetime.fromtimestamp(self.store.tasks("board1")[0]["due"],self.store.zone)
        self.assertEqual((due.day,due.hour,due.minute),(14,9,0))
        self.assertEqual(len(self.store.due("board1")),1)
    def test_task_cap(self):
        for i in range(16):self.add(turn=str(i),due=False)
        with self.assertRaises(ValueError):self.add(turn='overflow',due=False)
    def test_silent_or_short_audio_rejected(self):
        stream=io.BytesIO()
        with wave.open(stream,'wb') as wav:
            wav.setparams((1,2,16000,0,'NONE','not compressed'));wav.writeframes(b'\0\0'*100)
        with self.assertRaises(ValueError):Services({'DEMO_MODE':'1'}).transcribe(stream.getvalue())

class ApiTests(unittest.TestCase):
    def setUp(self):
        self.store=Store(':memory:');self.services=FakeServices()
        self.app=Companion({'DEVICE_TOKEN':'test-connection-token'},self.store,self.services)
        self.server=ThreadingHTTPServer(('127.0.0.1',0),handler_for(self.app))
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        self.url=f'http://127.0.0.1:{self.server.server_port}'
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.thread.join();self.store.close()
    def request(self,path,body=None,device='board1',token='test-connection-token'):
        return urllib.request.urlopen(urllib.request.Request(self.url+path,
            data=json.dumps(body).encode() if body is not None else None,
            headers={'X-Device-Id':device,'Authorization':'Bearer '+token}),timeout=5)
    def test_auth_required(self):
        with self.assertRaises(urllib.error.HTTPError) as err:self.request('/v1/state',token='wrong')
        self.assertEqual(err.exception.code,401)
    def test_audio_owner_isolation(self):
        sid=self.app.speech('board1','hello')
        with self.assertRaises(urllib.error.HTTPError) as err:self.request('/v1/speech/'+sid,device='board2')
        self.assertEqual(err.exception.code,404)
    def test_http_idempotency(self):
        for _ in range(2):
            with self.request('/v1/text',{'text':'明天提醒我提交周报','turn_id':'unique-turn'}) as r:result=json.load(r)
        self.assertEqual(len(result['tasks']),1);self.assertEqual(self.services.calls,1)
        self.assertTrue(result['speech_id'])
    def test_tts_failure_keeps_successful_task(self):
        def fail(text):raise RuntimeError('TTS unavailable')
        self.services.synthesize=fail
        result=self.app.turn('board1','one',{'text':'记录任务'},False)
        self.assertEqual(len(result['tasks']),1);self.assertIn('audio_error',result)
        self.app.turn('board1','one',{'text':'记录任务'},False)
        self.assertEqual(len(self.store.tasks('board1')),1)

if __name__=='__main__':unittest.main()
