import contextlib
import io
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from unittest.mock import patch
from configuration import qr_payload,save_model_key,validate_key
from server import Companion,BusyError,handler_for,load_config
from store import Store
from test_companion import FakeServices

KEY='sk-dummy-test-credential-only-123456'
class ConfigurationTests(unittest.TestCase):
    def test_atomic_update_preserves_other_settings_and_restricts_permissions(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'config.env';p.write_text('# local\nDEVICE_TOKEN=unchanged\nDEEPSEEK_API_KEY=old\nDEEPSEEK_API_KEY=duplicate\n')
            save_model_key(p,KEY)
            self.assertEqual(load_config(p)['DEEPSEEK_API_KEY'],KEY)
            self.assertIn('DEVICE_TOKEN=unchanged',p.read_text())
            self.assertEqual(p.read_text().count('DEEPSEEK_API_KEY='),1)
            self.assertEqual(p.stat().st_mode&0o777,0o600)
            self.assertEqual(list(Path(tmp).glob('.config-*')),[])
    def test_bad_key_does_not_modify_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'config.env';p.write_text('preserved')
            for key in ['',None,'a'*256,'sk-short','a'*20+'\n','a'*20+' ']:
                with self.assertRaises(ValueError):save_model_key(p,key)
                self.assertEqual(p.read_text(),'preserved')
    def test_two_qr_kinds_and_byte_limits(self):
        c={'DEEPSEEK_API_KEY':KEY,'DEVICE_TOKEN':'dummy-device-token'}
        q=json.loads(qr_payload(c,'api-key'));self.assertEqual(q['key'],KEY);self.assertNotIn('token',q)
        q=json.loads(qr_payload(c,'connection','家中网络','12345678','http://192.168.1.10:8765'))
        self.assertEqual(q['ssid'],'家中网络');self.assertNotIn('key',q)
        with self.assertRaises(ValueError):qr_payload(c,'connection','家'*11,'12345678','http://localhost:8765')
        with self.assertRaises(ValueError):qr_payload(c,'connection','Home','short','http://localhost:8765')

class KeyEndpointTests(unittest.TestCase):
    def test_auth_confirmation_busy_and_secret_free_response(self):
        with tempfile.TemporaryDirectory() as tmp,patch.dict(os.environ,{},clear=True):
            p=Path(tmp)/'config.env';p.write_text('DEVICE_TOKEN=dummy-device-token\n')
            c={'DEVICE_TOKEN':'dummy-device-token'};store=Store(':memory:')
            app=Companion(c,store,FakeServices(),config_path=p)
            with contextlib.redirect_stdout(io.StringIO()) as logs:
                server=ThreadingHTTPServer(('127.0.0.1',0),handler_for(app))
                t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
                def send(token):
                    return urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{server.server_port}/v1/config/key',
                        data=json.dumps({'key':KEY}).encode(),headers={'Authorization':'Bearer '+token,'X-Device-Id':'test-board'}))
                try:
                    with self.assertRaises(urllib.error.HTTPError) as caught:send('wrong')
                    self.assertEqual(caught.exception.code,401);self.assertNotIn(KEY,p.read_text())
                    app.turn_lock.acquire()
                    with self.assertRaises(urllib.error.HTTPError) as caught:send('dummy-device-token')
                    self.assertEqual(caught.exception.code,503);app.turn_lock.release()
                    self.assertNotIn(KEY,p.read_text())
                    with send('dummy-device-token') as response:body=response.read().decode()
                    self.assertNotIn(KEY,body);self.assertEqual(load_config(p)['DEEPSEEK_API_KEY'],KEY)
                    self.assertEqual(c['DEEPSEEK_API_KEY'],KEY)
                    os.environ['DEEPSEEK_API_KEY']='managed-elsewhere'
                    with self.assertRaises(RuntimeError):app.configure_key({'key':KEY})
                finally:
                    server.shutdown();server.server_close();t.join();store.close()
            self.assertNotIn(KEY,logs.getvalue())
