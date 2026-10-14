#!/usr/bin/env python3
"""Check the Git index for private runtime files; never prints secret values."""
import re
import subprocess
from pathlib import PurePosixPath

entries=subprocess.check_output(['git','ls-files','--stage','-z']).split(b'\0')
failures=[]
count=0
templates={'config.example.env','.env.example'}
for entry in entries:
    if not entry:continue
    metadata,raw_path=entry.split(b'\t',1)
    mode,object_id,stage=metadata.decode().split()
    if mode=='160000':continue  # Upstream dependency, pinned separately.
    name=raw_path.decode()
    path=PurePosixPath(name)
    private=(path.name not in templates and
             (path.name.endswith('.env') or path.name.startswith('.env.') or path.name.startswith('config.env')))
    private=private or any(part in ('.venv','__pycache__','.codex-runs','.mosaico-state') for part in path.parts)
    private=private or ('server' in path.parts and 'data' in path.parts)
    private=private or bool(re.search(r'\.(sqlite3?|db)(-(wal|shm))?$|\.(log|wav|aiff|pcm)$',path.name))
    if private:failures.append((name,'private runtime file'));continue
    blob=subprocess.check_output(['git','cat-file','blob',object_id])
    count+=1
    if re.search(rb'\bsk-[A-Za-z0-9]{20,}\b',blob):
        failures.append((name,'possible API key'))
    if path.name in templates:
        for line in blob.decode().splitlines():
            if line.strip().startswith('#'):continue
            key,separator,value=line.partition('=')
            if separator and key.strip()=='DEEPSEEK_API_KEY' and value.strip():
                failures.append((name,'template API key must be empty'))
            if separator and key.strip()=='DEVICE_TOKEN' and value.strip()!='replace-with-a-long-random-token':
                failures.append((name,'template connection token must be a placeholder'))
if failures:
    for name,reason in failures:print(f'FAIL: {name}: {reason}')
    raise SystemExit(1)
print(f'PASS: {count} tracked files; ENV templates only, no private runtime files or API key patterns.')
