#!/usr/bin/env bash
# Starts RETROTV Server. The first run creates .venv, installs requirements.txt, writes
# config/channels.json from the example and makes the synthetic demo clip (no third-party
# content).
#
#   server/run.sh           127.0.0.1:8080, this computer only (curl, tests)
#   server/run.sh --lan     0.0.0.0:8080 + announces retrotv-server.local, so the TV can reach it.
#                           Home network only: never forward this port to the Internet.
#   PAUTV_PORT=9000 server/run.sh --lan
set -euo pipefail
cd "$(dirname "$0")"

python="${PYTHON:-python3}"
if [[ ! -x .venv/bin/python ]]; then
  "$python" -m venv .venv
fi
.venv/bin/pip install -q -r requirements.txt

[[ -f config/channels.json ]] || cp config/channels.example.json config/channels.json

if [[ ! -f media/demo/demo.mjpeg ]]; then
  echo "making the demo clip (30 s)..."
  scratch="$(mktemp -d)"
  trap 'rm -rf "$scratch"' EXIT
  ../tools/make_demo_clip.sh "$scratch" 30 >/dev/null
  mkdir -p media/demo
  mv "$scratch"/retrotv/media/demo/demo.mjpeg "$scratch"/retrotv/media/demo/demo.aac media/demo/
  "$python" ../tools/make_index.py media/demo/demo.mjpeg
fi

host=127.0.0.1
announce=0
if [[ "${1:-}" == "--lan" ]]; then
  host=0.0.0.0
  announce=1
fi
export PAUTV_ANNOUNCE="$announce"
export PAUTV_PORT="${PAUTV_PORT:-8080}"
# Streams never end on their own: on Ctrl+C, give them 1 s and then cut them (the TV sees the
# connection close and shows NO SIGNAL) instead of waiting forever for the TV to hang up.
exec .venv/bin/uvicorn app.main:create_app --factory --host "$host" --port "$PAUTV_PORT" \
  --timeout-graceful-shutdown 1
