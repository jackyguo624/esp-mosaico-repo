#!/bin/bash
set -e
cd "$(dirname "$0")"
python3 setup.py
if [ ! -x .venv/bin/python ]; then
    python3 -m venv .venv
fi
# Retry dependency installation after an interrupted first setup.
if ! .venv/bin/python -c 'from faster_whisper import WhisperModel' 2>/dev/null; then
    .venv/bin/python -m pip install -r requirements-asr.txt
fi
exec .venv/bin/python server.py --config config.env
