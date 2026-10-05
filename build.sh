#!/usr/bin/env bash
# Host tests, then the Cardputer ADV firmware. Copies the app image and a
# merged full-flash image into releases/ with SHA256 checksums.
set -euo pipefail
cd "$(dirname "$0")"
python3 -m pip install --user --quiet platformio 'uv>=0.1.0'
export PATH="${HOME}/.local/bin:${PATH}"

python3 tools/gen_samples.py
python3 tools/gen_sd_examples.py
bash tools/run_tests.sh

pio run -e cardputer-adv

BUILD=".pio/build/cardputer-adv"
mkdir -p releases
cp "${BUILD}/firmware.bin" releases/mothdeck-cardputer-adv.bin

if [[ -f "${BUILD}/firmware.factory.bin" ]]; then
  cp "${BUILD}/firmware.factory.bin" releases/mothdeck-cardputer-adv-full.bin
else
python3 - <<'PY'
import json, os, subprocess
home = os.environ.get("HOME", "")
candidates = []
pkg = os.path.join(home, ".platformio", "packages")
for root, dirs, files in os.walk(pkg):
    if "esptool.py" in files and "tool-esptoolpy" in root:
        candidates.append(os.path.join(root, "esptool.py"))
if not candidates:
    raise SystemExit("esptool.py was not installed by PlatformIO")
esptool = sorted(candidates)[-1]
build = ".pio/build/cardputer-adv"
args = json.load(open(os.path.join(build, "flasher_args.json")))
flash = args.get("flash_settings", {})
mode = flash.get("flash_mode", "dio")
freq = flash.get("flash_freq", "80m")
size = flash.get("flash_size", "8MB")
pairs = []
for offset, path in args.get("flash_files", {}).items():
    src = path if os.path.isabs(path) else os.path.join(build, os.path.basename(path))
    if not os.path.exists(src):
        src = path
    if not os.path.exists(src):
        raise SystemExit("missing flash file %s" % path)
    pairs.append((offset, src))
out = "releases/mothdeck-cardputer-adv-full.bin"
cmd = [
    "python3", esptool, "--chip", "esp32s3", "merge_bin",
    "-o", out,
    "--flash_mode", mode,
    "--flash_freq", freq,
    "--flash_size", size,
]
for offset, src in pairs:
    cmd.extend([offset, src])
print(" ".join(cmd), flush=True)
subprocess.check_call(cmd)
print("wrote", out)
PY
fi

(
  cd releases
  sha256sum mothdeck-cardputer-adv.bin mothdeck-cardputer-adv-full.bin > SHA256SUMS
)
echo "release images are in releases/"
