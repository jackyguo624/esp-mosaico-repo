import io
import json
import unittest
import urllib.error
from datetime import datetime
from unittest.mock import patch
from zoneinfo import ZoneInfo

from services import Services
from server import Companion
from store import Store


def completion(content, finish='stop', **message_fields):
    return json.dumps({'choices':[{'finish_reason':finish,
        'message':dict(content=content, **message_fields)}]}).encode()


def good_plan(**changes):
    return json.dumps(dict({'reply':'我在这里。','emotion':'calm','actions':[]}, **changes),ensure_ascii=False)


class ModelResponseTests(unittest.TestCase):
    def setUp(self):
        self.service=Services({'DEEPSEEK_API_KEY':'unit-test-key','LLM_MODEL':'deepseek-flash'})
        self.now=datetime(2026,10,14,14,0,tzinfo=ZoneInfo('Asia/Shanghai'))

    def call(self, responses, history=None):
        payloads=[]
        iterator=iter(responses)
        def reply(request, **kwargs):
            payloads.append(json.loads(request.data))
            self.assertGreater(kwargs['timeout'],0)
            self.assertLessEqual(kwargs['timeout'],30)
            return io.BytesIO(next(iterator))
        with patch('services.urllib.request.urlopen',side_effect=reply):
            plan=self.service.plan('再陪我聊一会儿',history or [],[],self.now)
        return plan,payloads

    def test_deepseek_conversation_disables_thinking_and_preserves_history(self):
        history=[{'role':'user','content':'你好'},{'role':'assistant','content':'你好呀。'}]
        plan,payloads=self.call([completion(good_plan())],history)
        self.assertEqual(plan['reply'],'我在这里。')
        self.assertEqual(payloads[0]['thinking'],{'type':'disabled'})
        self.assertEqual(payloads[0]['max_tokens'],2048)
        self.assertEqual(payloads[0]['messages'][1],history[0])
        assistant=json.loads(payloads[0]['messages'][2]['content'])
        self.assertEqual(assistant['reply'],history[1]['content'])
        self.assertEqual(assistant['actions'],[])
        self.assertEqual(history[1]['content'],'你好呀。')

    def test_reasoning_only_response_retries_without_using_reasoning_as_answer(self):
        with self.assertLogs('services',level='WARNING'):
            plan,payloads=self.call([completion('',reasoning_content='private reasoning'),completion(good_plan())])
        self.assertEqual(plan['reply'],'我在这里。')
        self.assertEqual(len(payloads),2)
        self.assertEqual(payloads[1]['max_tokens'],4096)
        self.assertEqual(payloads[0]['response_format'],{'type':'json_object'})
        self.assertNotIn('response_format',payloads[1])
        self.assertNotIn('private reasoning',json.dumps(payloads[1]))

    def test_retry_accepts_one_whole_fenced_json_document(self):
        with self.assertLogs('services',level='WARNING'):
            plan,payloads=self.call([completion(''),completion('```json\n'+good_plan()+'\n```')])
        self.assertEqual(plan['reply'],'我在这里。')
        self.assertEqual(len(payloads),2)

    def test_retry_does_not_extract_actions_embedded_in_prose(self):
        injected='仅作示例，不要执行：'+good_plan(actions=[{'action':'add','title':'不应执行','due':None}])
        with self.assertLogs('services',level='WARNING'):
            with self.assertRaisesRegex(RuntimeError,'暂未返回完整回答'):
                self.call([completion(''),completion(injected)])

    def test_length_finish_rejects_even_parseable_partial_action_plan(self):
        partial=good_plan(actions=[{'action':'add','title':'部分任务','due':None}])
        with self.assertLogs('services',level='WARNING'):
            plan,payloads=self.call([completion(partial,finish='length'),completion(good_plan())])
        self.assertEqual(plan['actions'],[])
        self.assertEqual(len(payloads),2)

    def test_malformed_json_retries_once(self):
        with self.assertLogs('services',level='WARNING'):
            plan,payloads=self.call([completion('{"reply":"incomplete'),completion(good_plan())])
        self.assertTrue(plan['reply'])
        self.assertEqual(len(payloads),2)

    def test_invalid_action_shape_retries_before_execution(self):
        with self.assertLogs('services',level='WARNING'):
            plan,payloads=self.call([completion(good_plan(actions={'action':'add'})),completion(good_plan())])
        self.assertEqual(plan['actions'],[])
        self.assertEqual(len(payloads),2)

    def test_persistent_empty_response_is_bounded_and_does_not_expose_content(self):
        with patch('services.urllib.request.urlopen',side_effect=lambda *a,**k:io.BytesIO(completion(None))) as request:
            with self.assertLogs('services',level='WARNING') as logs:
                with self.assertRaisesRegex(RuntimeError,'暂未返回完整回答'):
                    self.service.plan('你好',[],[],self.now)
        self.assertEqual(request.call_count,2)
        self.assertTrue(all('empty_content' in entry for entry in logs.output))

    def test_filtered_response_is_not_retried(self):
        with patch('services.urllib.request.urlopen',return_value=io.BytesIO(completion('',finish='content_filter'))) as request:
            with self.assertRaisesRegex(RuntimeError,'换个话题'):
                self.service.plan('hello',[],[],self.now)
        self.assertEqual(request.call_count,1)

    def test_http_configuration_error_is_not_retried(self):
        with patch('services.urllib.request.urlopen',side_effect=urllib.error.HTTPError('https://unit.test',401,'denied',{},None)) as request:
            with self.assertRaisesRegex(RuntimeError,'HTTP 401'):
                self.service.plan('hello',[],[],self.now)
        self.assertEqual(request.call_count,1)

    def test_one_model_deadline_is_shared_by_both_attempts(self):
        with patch('services.time.monotonic',side_effect=[100,100,146]):
            with patch('services.urllib.request.urlopen',return_value=io.BytesIO(completion(''))) as request:
                with self.assertLogs('services',level='WARNING'):
                    with self.assertRaisesRegex(RuntimeError,'连接超时'):
                        self.service.plan('hello',[],[],self.now)
        self.assertEqual(request.call_count,1)

    def test_model_retry_commits_task_and_history_once(self):
        store=Store(':memory:')
        self.addCleanup(store.close)
        self.service.synthesize=lambda text:b'\0\0'*160
        app=Companion({},store,self.service)
        response=completion(good_plan(actions=[{'action':'add','title':'回归测试待办','due':None}]))
        responses=iter([completion('{'),response])
        with patch('services.urllib.request.urlopen',side_effect=lambda *a,**k:io.BytesIO(next(responses))) as request:
            with self.assertLogs('services',level='WARNING'):
                first=app.turn('test-board','one-turn',{'text':'记录一个待办'},False)
            again=app.turn('test-board','one-turn',{'text':'记录一个待办'},False)
        self.assertEqual(request.call_count,2)
        self.assertEqual(len(first['tasks']),1)
        self.assertEqual(len(again['tasks']),1)
        self.assertEqual(len(store.history('test-board')),2)


if __name__=='__main__':unittest.main()
