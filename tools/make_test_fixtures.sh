#!/usr/bin/env bash
# Writes the synthetic episodes the on-board tests use (tools/device_tests.py) into
# <sd_root>/retrotv/test/. 100 % generated with ffmpeg: no third-party content.
#
# Usage: tools/make_test_fixtures.sh <sd_root>      e.g. /Volumes/RETROTV
#
#   transition/  two indexed 6 s episodes: the second must follow the first, from 0:00
#   stale/       an episode whose .idx belongs to another clip: the TV must notice
#   noindex/     an episode without .idx: V0.1 behaviour, from the beginning
#   eof/         video longer than audio, and audio longer than video: both end cleanly
#   empty/       a 0-byte .mjpeg: NO SIGNAL "FILE ERROR", never a restart loop
#
# Do not run tools/make_index.py over retrotv/test: it would repair the stale fixture.
set -euo pipefail

if [[ $# -ne 1 || ! -d "$1" ]]; then
  sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
fi
command -v ffmpeg >/dev/null || { echo "ffmpeg not found"; exit 1; }
command -v python3 >/dev/null || { echo "python3 not found"; exit 1; }
tools="$(cd "$(dirname "$0")" && pwd)"
root="${1%/}/retrotv/test"
rm -rf "$root"
mkdir -p "$root"/{transition,stale,noindex,eof,empty}
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

ff() { ffmpeg -nostdin -hide_banner -loglevel error -y "$@"; }
# video <out.mjpeg> <lavfi source, may carry its own options> <seconds>
video() {
  local sep="="
  [[ "$2" == *=* ]] && sep=":"
  ff -f lavfi -i "$2${sep}size=320x240:rate=24:duration=$3" -pix_fmt yuvj420p -q:v 8 -f mjpeg "$1"
}
# audio <out.aac> <hz> <seconds>
audio() { ff -f lavfi -i "sine=frequency=$2:sample_rate=44100:duration=$3" -ac 1 -c:a aac -b:a 32k -f adts "$1"; }
index() { python3 "$tools/make_index.py" --force "$1" >/dev/null; }

video "$root/transition/ep_01.mjpeg" testsrc2 6
audio "$root/transition/ep_01.aac" 440 6
video "$root/transition/ep_02.mjpeg" smptebars 6
audio "$root/transition/ep_02.aac" 660 6
index "$root/transition/ep_01.mjpeg"
index "$root/transition/ep_02.mjpeg"

# Stale: the index of a clip with much smaller frames, next to a busy clip of the same length.
video "$root/stale/ep.mjpeg" testsrc2 20
audio "$root/stale/ep.aac" 440 20
video "$scratch/other.mjpeg" color=c=black 20
audio "$scratch/other.aac" 440 20
index "$scratch/other.mjpeg"
cp "$scratch/other.idx" "$root/stale/ep.idx"

video "$root/noindex/ep.mjpeg" testsrc2 6
audio "$root/noindex/ep.aac" 440 6

video "$root/eof/a_video_longer.mjpeg" testsrc2 6
audio "$root/eof/a_video_longer.aac" 440 3
video "$root/eof/b_audio_longer.mjpeg" smptebars 4
audio "$root/eof/b_audio_longer.aac" 660 8
index "$root/eof/a_video_longer.mjpeg"
index "$root/eof/b_audio_longer.mjpeg"

: > "$root/empty/ep.mjpeg"

echo "test fixtures written to $root ($(du -sh "$root" | cut -f1))"
