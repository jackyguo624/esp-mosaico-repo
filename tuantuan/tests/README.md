# Tuantuan regression checks

Run service checks from `tuantuan/server/` with `.venv/bin/python -m unittest discover -s tests -v`.
The optional ASR dependencies enable the NumPy WAV-sample check; the other tests use the standard library.

After the pinned IDF build resolves managed components, run host parsing checks from the Repo root:

```sh
mkdir -p .codex-runs/host-checks
cc -std=c11 -Wall -Wextra -Werror -I tuantuan/main \
  -I tuantuan/managed_components/espressif__cjson/cJSON \
  tuantuan/tests/settings_test.c tuantuan/main/companion_settings.c \
  tuantuan/managed_components/espressif__cjson/cJSON/cJSON.c -lm \
  -o .codex-runs/host-checks/settings-test
.codex-runs/host-checks/settings-test
cc -std=c11 -O2 -I tuantuan/main -I tuantuan/components/quirc/vendor \
  -I tuantuan/managed_components/espressif__cjson/cJSON \
  tuantuan/tests/qr_decode_test.c tuantuan/main/companion_settings.c \
  tuantuan/managed_components/espressif__cjson/cJSON/cJSON.c \
  tuantuan/components/quirc/vendor/quirc.c tuantuan/components/quirc/vendor/identify.c \
  tuantuan/components/quirc/vendor/decode.c tuantuan/components/quirc/vendor/version_db.c \
  -lm -o .codex-runs/host-checks/qr-test
tuantuan/server/.venv/bin/python tuantuan/tests/qr_frames.py \
  --decoder .codex-runs/host-checks/qr-test --output .codex-runs/host-checks/frames
```

Build the shared native backend with the official `mosaico.py project sim --project tuantuan` workflow.
Stop the preview before rebuilding its directory. `simulator_interactions.py` starts the matching
simulator with its public input API and runs the native executable, storing logs and screenshots:

```sh
python3 tuantuan/tests/simulator_interactions.py --sim /absolute/path/to/pinned/sim \
  --build "$PWD/tuantuan/build-sim" \
  --component "$PWD/tuantuan/managed_components/espressif__esp-gsp" \
  --output "$PWD/.codex-runs/ui-acceptance"
```

Inspect the screenshots as well as the command assertions. Hardware validation is still required
for camera opening/frame formats, microphone, speaker, Wi-Fi and successful device configuration.
All QR test credentials are explicit dummy values; never substitute a real key in test fixtures.
