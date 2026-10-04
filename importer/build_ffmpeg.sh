#!/usr/bin/env bash
# Builds the ffmpeg and ffprobe RetroTV Importar carries, into importer/build/ffmpeg/:
#   - only what tools/convert_video.sh needs: the usual video and audio decoders of mp4/mov/m4v, mkv/webm
#     and avi files; the MJPEG and AAC encoders; scale/crop/pad/fps/aresample/loudnorm. Not AV1 (that would
#     need libdav1d) nor the GPL filters convert_video.sh's FILTROS can take (hqdn3d).
#   - LGPL: without --enable-gpl, configure refuses every GPL part. COPYING.LGPLv2.1 and SOURCE.txt (where
#     the source is and how it was built) go with the binaries.
#   - static, no Homebrew libraries (--disable-autodetect): only what every Mac has (libSystem, libz).
#   - one universal binary each, Apple Silicon + Intel, macOS 12 and later.
# Needs Xcode or the Command Line Tools, and nasm for the Intel half (brew install nasm). ~5 min.
#
# Usage: importer/build_ffmpeg.sh        (does nothing if build/ffmpeg is already this version)
set -euo pipefail

VERSION=9.0.2
# Release tarball, signed by the FFmpeg release key FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8
# (signature checked 2026-10-04 when this hash was pinned).
SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
URL="https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
MACOS_MIN=12.0

cd "$(dirname "$0")"
out=build/ffmpeg
if [[ -f "$out/SOURCE.txt" ]] && grep -q "^ffmpeg $VERSION$" "$out/SOURCE.txt"; then
  echo "ffmpeg $VERSION already built in importer/$out"
  exit 0
fi
command -v nasm >/dev/null || { echo "nasm not found (brew install nasm): the Intel half needs it"; exit 1; }

tarball="build/ffmpeg-$VERSION.tar.xz"
mkdir -p build
[[ -f "$tarball" ]] || curl -sSfL -o "$tarball" "$URL"
echo "$SHA256  $tarball" | shasum -a 256 -c --quiet - || { echo "checksum mismatch: $tarball"; rm -f "$tarball"; exit 1; }

options=(
  --disable-everything --disable-autodetect --disable-network --disable-doc --disable-ffplay
  --disable-avdevice --disable-debug --enable-static --disable-shared --enable-zlib
  --enable-protocol=file,pipe
  --enable-demuxer=mov,matroska,avi
  --enable-decoder=h264,hevc,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,h263,mpeg1video,mpeg2video,vp8,vp9,vc1,wmv3,mjpeg,prores,theora
  --enable-decoder=aac,ac3,ac3_fixed,eac3,mp3,mp3float,mp2,mp2float,opus,vorbis,flac,alac,dca
  --enable-decoder=pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_u8,pcm_alaw,pcm_mulaw
  --enable-parser=h264,hevc,mpeg4video,mpegvideo,h263,vp8,vp9,vc1,mjpeg,aac,ac3,mpegaudio,opus,vorbis,flac,dca
  --enable-encoder=mjpeg,aac
  --enable-muxer=mjpeg,adts
  --enable-filter=buffer,buffersink,abuffer,abuffersink,null,anull,format,aformat
  --enable-filter=scale,setsar,fps,crop,pad,aresample,loudnorm
)

build_arch() {
  local arch="$1" src="build/ffmpeg-$VERSION-$1"
  rm -rf "$src"
  mkdir -p "$src"
  tar -xf "$tarball" -C "$src" --strip-components 1
  local cross=()
  [[ "$arch" != "$(uname -m)" ]] && cross=(--enable-cross-compile --target-os=darwin)
  (
    cd "$src"
    ./configure --arch="$arch" --cc=clang ${cross[@]+"${cross[@]}"} \
      --extra-cflags="-arch $arch -mmacosx-version-min=$MACOS_MIN" \
      --extra-ldflags="-arch $arch -mmacosx-version-min=$MACOS_MIN" \
      "${options[@]}" >configure.log
    make -j"$(sysctl -n hw.ncpu)" ffmpeg ffprobe >make.log 2>&1
  ) || { echo "build failed for $arch: see importer/$src/configure.log, make.log"; exit 1; }
}

build_arch arm64
build_arch x86_64

rm -rf "$out"
mkdir -p "$out"
for tool in ffmpeg ffprobe; do
  lipo -create "build/ffmpeg-$VERSION-arm64/$tool" "build/ffmpeg-$VERSION-x86_64/$tool" -output "$out/$tool"
  strip "$out/$tool"
  codesign --force --sign - "$out/$tool"  # ad hoc: Apple Silicon runs nothing unsigned
done
cp "build/ffmpeg-$VERSION-arm64/COPYING.LGPLv2.1" "$out/"
{
  echo "ffmpeg $VERSION"
  echo "Source: $URL"
  echo "SHA-256: $SHA256"
  echo "Built by importer/build_ffmpeg.sh (RetroTV), LGPL-2.1-or-later (COPYING.LGPLv2.1), configured with:"
  printf '  %s\n' "${options[@]}"
} >"$out/SOURCE.txt"

"$out/ffmpeg" -hide_banner -L | head -3
ls -l "$out"
