#!/usr/bin/env python3
"""Check the Git index for private runtime files; never prints secret values."""
import re
import subprocess
from pathlib import Path, PurePosixPath

root = Path(__file__).resolve().parents[1]
local_secrets = []
config = root / 'tuantuan/server/config.env'
if config.is_file():
    for line in config.read_text().splitlines():
        key, separator, value = line.partition('=')
        key, value = key.strip(), value.strip().strip('\"\x27')
        if (separator and key in {'DEEPSEEK_API_KEY', 'DEVICE_TOKEN', 'WIFI_PASSWORD'}
                and value and value != 'replace-with-a-long-random-token'):
            local_secrets.append((key, value.encode()))

patterns = [
    (rb'\bsk-[A-Za-z0-9]{20,}\b', 'possible API key'),
    (rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b', 'possible GitHub token'),
    (rb'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----', 'private key'),
    (rb'/Users/[A-Za-z0-9_.-]+/', 'personal filesystem path'),
    (rb'4553502d49524953[0-9a-fA-F]{12,}', 'real device identifier'),
]

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
    private=private or any(part in ('.venv','__pycache__','.codex-runs','.mosaico-state',
                                   '.tools','managed_components') or part.startswith('build')
                          for part in path.parts[:-1])
    private=private or ('server' in path.parts and 'data' in path.parts)
    private=private or bool(re.search(r'\.(sqlite3?|db)(-(wal|shm))?$|\.(log|wav|aiff|pcm|irisfw|bin|elf|map|pem|key)$',path.name))
    private=private or (path.parts[:2] == ('tuantuan', 'docs') and
                       (path.name.endswith('-tests.txt') or path.name == 'model-continuous-check.txt'
                        or path.name.startswith('device-')))
    if private:failures.append((name,'private runtime file'));continue
    blob=subprocess.check_output(['git','cat-file','blob',object_id])
    count+=1
    for pattern, reason in patterns:
        if re.search(pattern, blob):
            failures.append((name, reason))
    for key, value in local_secrets:
        if value in blob:
            failures.append((name, 'matches private local ' + key))
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
print(f'PASS: {count} tracked files; no private runtime/build files, secret patterns or local credential matches.')
