#!/usr/bin/env bash
# Install the workspace's exact SDK revision; preserve other SDK checkouts.
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
sdk_path="${ESP_MOSAICO_IDF_PATH:-$HOME/esp/esp-idf-mosaico}"
sdk_python="${ESP_MOSAICO_PYTHON:-python3}"
revision="7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe"
setup_log="$repo_root/.codex-runs/environment-setup/raw.log"
mkdir -p "$(dirname "$setup_log")"

if [ "${1:-}" = '--python-env' ]; then
    test "$(git -C "$sdk_path" rev-parse HEAD)" = "$revision"
    test -z "$(git -C "$sdk_path" status --porcelain)"
    exec "$sdk_python" "$sdk_path/tools/idf_tools.py" install-python-env --features core
fi

install_sdk() {
    "$sdk_python" -c 'import sys; assert sys.version_info >= (3, 10), "Python 3.10+ required"'
    git --version
    command -v cmake
    if [ -e "$sdk_path" ]; then
        actual="$(git -C "$sdk_path" rev-parse HEAD)"
        if [ "$actual" != "$revision" ]; then
            printf 'Existing SDK has a different revision: %s\nChoose a separate ESP_MOSAICO_IDF_PATH.\n' "$sdk_path"
            return 1
        fi
    else
        # Compare both official sources without changing global Git configuration.
        source_url="$("$sdk_python" - <<'PY'
import subprocess
import sys
import time
sources = ['https://github.com/espressif/esp-idf.git',
           'https://git.espressif.com.cn/espressif/esp-idf.git']
results = []
for source in sources:
    started = time.monotonic()
    try:
        result = subprocess.run(['git', 'ls-remote', source, 'HEAD'],
                                capture_output=True, timeout=45)
        elapsed = time.monotonic() - started
        print(f'{source}: {elapsed:.2f}s, success={result.returncode == 0}', file=sys.stderr)
        if result.returncode == 0:
            results.append((elapsed, source))
    except subprocess.TimeoutExpired:
        print(f'{source}: timeout', file=sys.stderr)
if not results:
    raise SystemExit('Both official SDK sources are unavailable')
results.sort()
selected = results[0][1]
github = next((elapsed for elapsed, source in results if source == sources[0]), None)
if github is not None and github <= results[0][0] * 1.2:
    selected = sources[0]
print(selected)
PY
)"
        mkdir -p "$(dirname "$sdk_path")"
        git clone --no-checkout --filter=blob:none "$source_url" "$sdk_path"
        git -C "$sdk_path" fetch --depth 1 origin "$revision"
        git -C "$sdk_path" checkout --detach "$revision"
    fi
    test "$(git -C "$sdk_path" rev-parse HEAD)" = "$revision"
    sdk_origin="$(git -C "$sdk_path" remote get-url origin)"
    if [[ "$sdk_origin" == https://git.espressif.com.cn/* ]]; then
        if ! git -C "$sdk_path" -c url.https://git.espressif.com.cn/.insteadOf=https://github.com/ submodule update --init --recursive; then
            # Preserve successful downloads and retry missing pinned objects at upstream.
            git -C "$sdk_path" -c url.https://github.com/.insteadOf=https://git.espressif.com.cn/ submodule update --init --recursive
        fi
        export IDF_GITHUB_ASSETS="${IDF_GITHUB_ASSETS:-dl.espressif.cn/github_assets}"
    else
        git -C "$sdk_path" submodule update --init --recursive
    fi
    test -z "$(git -C "$sdk_path" status --porcelain)"
    export PATH="$(dirname "$(command -v "$sdk_python")"):$PATH"
    "$sdk_path/install.sh" esp32s31
    export IDF_PATH="$sdk_path"
    . "$sdk_path/export.sh"
    python "$repo_root/firmware-workspace/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py" --project "$repo_root/tuantuan" doctor
}

if [ "${1:-}" = '--install' ]; then
    # Run as a separate shell so errexit also applies inside install_sdk.
    install_sdk
    exit 0
fi

if bash "$0" --install >"$setup_log" 2>&1; then
    printf 'Pinned SDK installed: %s\nRevision: %s\nLog: %s\n' "$sdk_path" "$revision" "$setup_log"
else
    status=$?
    printf 'SDK setup failed (%s). Log: %s\n' "$status" "$setup_log" >&2
    exit "$status"
fi
