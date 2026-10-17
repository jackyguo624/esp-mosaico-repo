#!/usr/bin/env python3
"""Exercise the shared native UI through the official simulator input API."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

p=argparse.ArgumentParser();p.add_argument('--sim',type=Path,required=True);p.add_argument('--build',type=Path,required=True)
p.add_argument('--component',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--scenario',choices=['normal','empty','offline','failure']);args=p.parse_args()
sys.path.insert(0,str(args.component/'tools/sim_bridge'))
from rpc_client import connect
args.output.mkdir(parents=True,exist_ok=True)
assets=args.build/'companion_backend_assets'
for scenario in ([args.scenario] if args.scenario else ['normal','empty','offline','failure']):
    ready=args.output/(scenario+'-ready.json');ready.unlink(missing_ok=True)
    with (args.output/(scenario+'-host.log')).open('w') as hostlog,(args.output/(scenario+'-backend.log')).open('w+') as backendlog:
        host=subprocess.Popen([str(args.sim),'--bundle',str(assets/'app.gspb'),'--api-json',str(assets/'bundle_gsp.api.json'),
            '--headless','--frames','0','--backend-enable','--backend-required','--backend-idle-timeout','30',
            '--api-listen','tcp://127.0.0.1:0','--ready-file',str(ready)],stdout=hostlog,stderr=hostlog)
        backend=None
        try:
            deadline=time.monotonic()+15
            while not ready.exists():
                assert host.poll() is None and time.monotonic()<deadline,'simulator startup failed'
                time.sleep(.05)
            info=json.loads(ready.read_text());env=dict(os.environ,TUANTUAN_SIM_SCENARIO=scenario)
            backend=subprocess.Popen([str(args.build/'companion_backend'),'--endpoint',info['backend']],env=env,stdout=backendlog,stderr=backendlog)
            with connect(port=int(info['api'].rsplit(':',1)[1])) as api:
                def call(method,params=None):
                    result=api.call(method,params);assert 'error' not in result,result;return result
                def tap(name,delay=.3):call('tap_object',{'name':name});time.sleep(delay)
                def shot(name):call('screenshot',{'path':str((args.output/(scenario+'-'+name+'.png')).resolve()),'format':'png'})
                time.sleep(1.5);shot('home')
                if scenario=='normal':
                    tap('talk');time.sleep(.2);shot('recording');tap('talk');time.sleep(.3);shot('thinking');time.sleep(5)
                    tap('tasks');tap('next');shot('last-page');tap('previous');tap('task_0');tap('back_detail');shot('back-to-list')
                    tap('task_0');tap('complete',.1);tap('complete',1.1);shot('completed')
                    tap('back_tasks');tap('settings');tap('touch_setup');tap('edit_password')
                    for x in [36,82,127]:call('tap',{'x':x,'y':217});time.sleep(.18)
                    time.sleep(.3);shot('masked');tap('setup_reveal');shot('revealed')
                    call('tap',{'x':418,'y':440});time.sleep(.3);shot('committed');tap('setup_back');tap('scan_setup',2.5);shot('qr-confirm')
                    # Scanning does not save. Only a separate explicit UI press applies it.
                    before=Path(backendlog.name).read_text();assert 'settings_save' not in before
                    tap('setup_save',.8);shot('qr-saved');tap('setup_back');tap('back_settings')
                elif scenario=='empty':
                    tap('tasks');shot('tasks');tap('task_0');shot('empty-row-tap');tap('back_tasks')
                elif scenario=='offline':
                    tap('talk');shot('connection-help');tap('touch_setup');tap('setup_mode');tap('setup_save');shot('key-offline');tap('setup_back')
                else:
                    tap('tasks');tap('task_0');tap('complete',1.2);shot('action-error');tap('back_detail');tap('back_tasks')
                    tap('settings');tap('scan_setup',2.5);tap('setup_save',.8);shot('save-error')
                call('quit')
            backend.wait(timeout=8);host.wait(timeout=8)
            logs=Path(backendlog.name).read_text()
            if scenario=='normal':assert logs.count('companion_command=1 task=1')==1 and 'tasks=4' in logs and logs.count('settings_save kind=2')==1,logs
            elif scenario=='failure':assert 'tasks=5' in logs and 'settings_save kind=2' in logs,logs
            elif scenario=='offline':assert 'settings_save' not in logs and 'companion_command=0' not in logs,logs
            elif scenario=='empty':assert 'tasks=0' in logs,logs
            print(scenario,'interaction checks passed',flush=True)
        finally:
            for proc in [backend,host]:
                if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=8)
