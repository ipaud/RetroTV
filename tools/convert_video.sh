#!/usr/bin/env bash
# Converts every video in a folder to the RETROTV format, straight onto the microSD card.
#
# Usage:
#   tools/convert_video.sh <input_dir> <dest_path_on_sd> <sd_root>
#   tools/convert_video.sh ~/Videos/BolaDeDrac /retrotv/media/channel01 /Volumes/RETROTV
#   PISTA=1 tools/convert_video.sh ...   # pick the 2nd audio track (dubbed versions); default 0
#   RECORTE_4_3=1 tools/convert_video.sh ...  # 4:3 picture inside a 16:9 frame (black sides):
#                                           # keep the centre 4:3 and fill the screen, no bars
#   CALIDAD=4 tools/convert_video.sh ...  # JPEG quality, 2 (best) - 31; default 3
#   FPS=24 tools/convert_video.sh ...     # frame rate, 1-24 (the TV's limit); default 20
#   FILTROS="deblock,eq=gamma=1.1" ...    # extra ffmpeg filters, on the source before scaling (per series)
#   SOLO_VIDEO=1 tools/convert_video.sh ...  # re-encode the picture of episodes already there,
#                                           # keeping their .aac; the .idx is made again
#
# Output, one pair per episode with the same FAT-safe base name (lowercase, no accents):
#   <name>.mjpeg  raw MJPEG (JPEGs back to back), 320x240, 20 fps, yuvj420p, quality -q:v 3
#                 (on the board: q8 blocks dark areas; 4:4:4 chroma does not decode; at 24 fps
#                 drawing a frame on the 40 MHz panel takes 38 of its 41.7 ms and frames drop.
#                 2026-10-01: at 20 fps q3, q4 and q5 all play with the dither; q3 looked best:
#                 +3 dB PSNR over q5 for ~1/3 more bytes, ~370 MB per 23 min episode).
#                 4:3 fills the screen; 16:9 becomes 320x176 centred with black bars; anamorphic
#                 sources are un-squeezed first. The picture is never stretched.
#   <name>.aac    AAC-LC in ADTS, mono, 44.1 kHz, 32 kb/s, EBU R128 loudness -16 LUFS (loud enough
#                 for the 40x28 mm speaker, true peak -1.5 dBTP so nothing clips).
#   <name>.idx    where each second starts in both files (tools/make_index.py, needs python3), so
#                 the TV can tune in mid-episode: the channel is "already on air". It also tells the
#                 TV the frame rate; without it an episode plays at 24 fps.
# Inputs: mp4 mkv avi mov m4v webm (symlinks followed). Episodes already converted are skipped.
# Audio is converted first, so a wrong PISTA fails before minutes of video work. Everything is
# written as .part and renamed at the end: an interrupted run never leaves a broken episode.
set -euo pipefail

if [[ $# -ne 3 ]]; then
  sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
fi
input_dir="$1"
dest_on_sd="$2"
sd_root="$3"
track="${PISTA:-0}"
quality="${CALIDAD:-3}"
fps="${FPS:-20}"
video_only="${SOLO_VIDEO:-0}"

command -v ffmpeg >/dev/null || { echo "ffmpeg not found (brew install ffmpeg)"; exit 1; }
[[ -d "$input_dir" ]] || { echo "input folder not found: $input_dir"; exit 1; }
[[ -d "$sd_root" ]] || { echo "SD root not found: $sd_root (is the card mounted?)"; exit 1; }
[[ "$track" =~ ^[0-9]+$ ]] || { echo "PISTA must be a number, got: $track"; exit 1; }
[[ "$quality" =~ ^[0-9]+$ && $quality -ge 2 && $quality -le 31 ]] || { echo "CALIDAD must be 2-31, got: $quality"; exit 1; }
[[ "$fps" =~ ^[0-9]+$ && $fps -ge 1 && $fps -le 24 ]] || { echo "FPS must be 1-24, got: $fps"; exit 1; }

out_dir="${sd_root%/}/${dest_on_sd#/}"
mkdir -p "$out_dir"
script_dir="$(cd "$(dirname "$0")" && pwd)"

# Without python3 the episode still plays, just from its beginning instead of "on air".
index_episode() {
  if ! command -v python3 >/dev/null; then
    echo "warn     python3 not found: no .idx for $(basename "$1")"
    return 0
  fi
  python3 "$script_dir/make_index.py" "$1" --fps "$fps" || echo "warn     index failed for $(basename "$1")"
}

# FAT-safe name: accents stripped (perl NFKD; macOS iconv mangles them), lowercase,
# anything else becomes "_", at most 60 characters.
safe_name() {
  printf '%s' "$1" |
    perl -CS -MUnicode::Normalize -pe '$_ = NFKD($_); s/\p{Mn}//g' |
    tr '[:upper:]' '[:lower:]' |
    sed -E 's/[^a-z0-9._-]+/_/g; s/_+/_/g; s/^[._-]+//; s/[._-]+$//' |
    cut -c1-60
}

VIDEO_FILTER="scale='trunc(iw*sar/2)*2':ih,setsar=1,fps=$fps,"
[[ "${RECORTE_4_3:-0}" == 1 ]] && VIDEO_FILTER+="crop='min(iw,trunc(ih*4/3/2)*2)':ih,"
[[ -n "${FILTROS:-}" ]] && VIDEO_FILTER+="${FILTROS%,},"
VIDEO_FILTER+="scale=320:240:force_original_aspect_ratio=decrease,"
# Whole 16-pixel JPEG blocks, placed on block boundaries: a bar edge in the middle of a block
# lets JPEG noise from the picture bleed into the bar (coloured dashes on the TV).
VIDEO_FILTER+="crop=trunc(iw/16)*16:trunc(ih/16)*16,"
VIDEO_FILTER+="pad=320:240:trunc((320-iw)/32)*16:trunc((240-ih)/32)*16,setsar=1"
AUDIO_FILTER="loudnorm=I=-16:TP=-1.5:LRA=11"

parts=()
cleanup() { local p; for p in "${parts[@]:-}"; do if [[ -n "$p" ]]; then rm -f "$p"; fi; done; }
trap cleanup EXIT
trap 'echo; echo "interrupted: partial files removed"; exit 130' INT TERM

converted=0
skipped=0
failed=0
used=$'\n'  # names taken in this run, one per line (macOS ships bash 3.2: no associative arrays)

while IFS= read -r -d '' src; do
  file="$(basename "$src")"
  name="$(safe_name "${file%.*}")"
  [[ -n "$name" ]] || name="episode"
  if [[ "$used" == *$'\n'"$name"$'\n'* ]]; then  # two inputs with the same safe name
    n=2
    while [[ "$used" == *$'\n'"${name}_$n"$'\n'* ]]; do n=$((n + 1)); done
    name="${name}_$n"
  fi
  used+="$name"$'\n'
  base="$out_dir/$name"

  # A source with no audio at all (an intro, a silent clip) becomes video only: the TV plays it
  # on the wall clock. A missing PISTA in a file that has audio is still an error.
  has_audio=1
  [[ -z "$(ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 "$src")" ]] && has_audio=0

  if [[ "$video_only" == 1 && $has_audio == 1 && ! -f "$base.aac" ]]; then
    echo "missing  $name.aac: SOLO_VIDEO needs the episode already there (another name?)"
    failed=$((failed + 1))
    continue
  fi
  if [[ "$video_only" != 1 && -f "$base.mjpeg" && ( -f "$base.aac" || $has_audio == 0 ) ]]; then
    echo "skip     $name (already converted)"
    [[ -f "$base.idx" ]] || index_episode "$base.mjpeg"
    skipped=$((skipped + 1))
    continue
  fi

  echo "convert  $file -> $name"
  parts=("$base.aac.part" "$base.mjpeg.part")
  if [[ $has_audio == 1 && "$video_only" != 1 ]] && ! ffmpeg -nostdin -hide_banner -loglevel error -y -i "$src" \
      -map "0:a:$track" -vn -ac 1 -ar 44100 -af "$AUDIO_FILTER" -c:a aac -b:a 32k \
      -f adts "$base.aac.part"; then
    echo "FAILED   $name: no audio track $track (set PISTA=n to choose another)"
    cleanup
    failed=$((failed + 1))
    continue
  fi
  if ! ffmpeg -nostdin -hide_banner -loglevel error -y -i "$src" \
      -map 0:v:0 -an -vf "$VIDEO_FILTER" -pix_fmt yuvj420p -q:v "$quality" \
      -f mjpeg "$base.mjpeg.part"; then
    echo "FAILED   $name: video conversion error"
    cleanup
    failed=$((failed + 1))
    continue
  fi
  [[ $has_audio == 1 && "$video_only" != 1 ]] && mv "$base.aac.part" "$base.aac"
  mv "$base.mjpeg.part" "$base.mjpeg"
  parts=()
  if [[ $has_audio == 1 ]]; then
    echo "done     $name ($(du -h "$base.mjpeg" | cut -f1) video, $(du -h "$base.aac" | cut -f1) audio)"
  else
    echo "done     $name ($(du -h "$base.mjpeg" | cut -f1) video, NO AUDIO in the source)"
  fi
  index_episode "$base.mjpeg"
  converted=$((converted + 1))
done < <(find -L "$input_dir" -maxdepth 1 -type f ! -name '._*' \
           \( -iname '*.mp4' -o -iname '*.mkv' -o -iname '*.avi' -o -iname '*.mov' \
              -o -iname '*.m4v' -o -iname '*.webm' \) -print0 | sort -z)

echo "converted $converted, skipped $skipped, failed $failed -> $out_dir"
if [[ "$(uname)" == "Darwin" ]]; then
  echo "tip: dot_clean -m \"$out_dir\" removes the ._ files macOS leaves on FAT cards"
fi
[[ $failed -eq 0 ]]
