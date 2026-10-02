#!/usr/bin/env bash
# Generates a 100 % original demo clip in the RETROTV format (no third-party content).
#
# Usage:
#   tools/make_demo_clip.sh <sd_root> [seconds]
#   tools/make_demo_clip.sh /Volumes/RETROTV      -> /Volumes/RETROTV/retrotv/media/demo/demo.{mjpeg,aac}
#
# Picture: ffmpeg's testsrc pattern with its running counter. Sound: a quiet 220 Hz hum.
# Every whole second the screen flashes white for 0.1 s AND a 1 kHz beep plays: if flash and
# beep coincide on the TV, audio and video are in sync.
set -euo pipefail

if [[ $# -lt 1 ]]; then
  sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
fi
sd_root="$1"
seconds="${2:-30}"
command -v ffmpeg >/dev/null || { echo "ffmpeg not found (brew install ffmpeg)"; exit 1; }
[[ -d "$sd_root" ]] || { echo "SD root not found: $sd_root"; exit 1; }
[[ "$seconds" =~ ^[0-9]+$ && "$seconds" -gt 0 ]] || { echo "seconds must be a positive number"; exit 1; }

out="${sd_root%/}/retrotv/media/demo"
mkdir -p "$out"
trap 'rm -f "$out/demo.aac.part" "$out/demo.mjpeg.part"' EXIT

ffmpeg -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i "aevalsrc='0.15*sin(2*PI*220*t)+0.6*sin(2*PI*1000*t)*lt(mod(t,1),0.1)':s=44100:d=$seconds" \
  -ac 1 -ar 44100 -c:a aac -b:a 32k -f adts "$out/demo.aac.part"

ffmpeg -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc=size=320x240:rate=24:duration=$seconds" \
  -vf "drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable='lt(mod(t,1),0.1)'" \
  -pix_fmt yuvj420p -q:v 8 -f mjpeg "$out/demo.mjpeg.part"

mv "$out/demo.aac.part" "$out/demo.aac"
mv "$out/demo.mjpeg.part" "$out/demo.mjpeg"
if command -v python3 >/dev/null; then
  python3 "$(dirname "$0")/make_index.py" --force "$out/demo.mjpeg"
fi
echo "demo clip: $out/demo.mjpeg ($(du -h "$out/demo.mjpeg" | cut -f1)) + demo.aac ($(du -h "$out/demo.aac" | cut -f1)), ${seconds} s"
