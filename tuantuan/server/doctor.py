#!/usr/bin/env python3
"""Read-only readiness check; never prints keys or connection tokens."""
import importlib.util
import shutil
from pathlib import Path
from server import load_config
config=load_config(Path(__file__).with_name('config.env'))
checks={
 '配置文件':Path(__file__).with_name('config.env').exists(),
 '连接口令':len(config.get('DEVICE_TOKEN',''))>=16 and config.get('DEVICE_TOKEN')!='replace-with-a-long-random-token',
 'DeepSeek Key':bool(config.get('DEEPSEEK_API_KEY')),
 '本地 ASR 依赖':bool(importlib.util.find_spec('faster_whisper')),
 'TTS 程序':bool(shutil.which('say') and shutil.which('afconvert')) if config.get('TTS_ENGINE','say')=='say' else bool(shutil.which(config.get('PIPER_BIN','piper')) and Path(config.get('PIPER_MODEL','')).is_file()),
}
for label,ready in checks.items():print(f'{label}：'+('就绪' if ready else '待配置'))
raise SystemExit(0 if all(checks.values()) else 1)
