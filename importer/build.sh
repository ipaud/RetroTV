#!/usr/bin/env bash
# Builds RetroTV Importar.
#   importer/build.sh        importer/build/RetroTVImporter (universal; what tools/run_host_tests.sh tests)
#   importer/build.sh app    also importer/build/RetroTV Importar.app, with its ffmpeg (build_ffmpeg.sh)
# Needs Xcode or the Command Line Tools (swiftc). The app runs on macOS 12 and later, Apple Silicon and Intel.
# Signed ad hoc only (no Apple developer account): the first time, the Mac asks to allow it in Privacy & Security.
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p build
for arch in arm64 x86_64; do
  swiftc -O -swift-version 6 -target "$arch-apple-macos12" Sources/*.swift -o "build/RetroTVImporter-$arch"
done
lipo -create build/RetroTVImporter-arm64 build/RetroTVImporter-x86_64 -output build/RetroTVImporter
rm build/RetroTVImporter-arm64 build/RetroTVImporter-x86_64
[[ "${1:-}" == "app" ]] || exit 0

./build_ffmpeg.sh >/dev/null
app="build/RetroTV Importar.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources/ffmpeg"
cp build/RetroTVImporter build/ffmpeg/ffmpeg build/ffmpeg/ffprobe "$app/Contents/MacOS/"
cp ../tools/convert_video.sh "$app/Contents/Resources/"
cp build/ffmpeg/COPYING.LGPLv2.1 build/ffmpeg/SOURCE.txt "$app/Contents/Resources/ffmpeg/"

# Icon: the centre of docs/img/logo-crt.jpg.
iconset="build/AppIcon.iconset"
rm -rf "$iconset"
mkdir -p "$iconset"
sips -s format png -c 480 480 ../docs/img/logo-crt.jpg --out build/icon.png >/dev/null
for size in 16 32 128 256 512; do
  sips -z $size $size build/icon.png --out "$iconset/icon_${size}x${size}.png" >/dev/null
  sips -z $((size * 2)) $((size * 2)) build/icon.png --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/AppIcon.icns"

version="$(git -C .. describe --always --dirty 2>/dev/null || echo dev)"
cat >"$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>es</string>
  <key>CFBundleDisplayName</key><string>RetroTV Importar</string>
  <key>CFBundleExecutable</key><string>RetroTVImporter</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundleIdentifier</key><string>io.github.ipaud.retrotv-importar</string>
  <key>CFBundleName</key><string>RetroTV Importar</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.video</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key><string>Capítulos</string>
      <key>CFBundleTypeRole</key><string>Viewer</string>
      <key>LSHandlerRank</key><string>None</string>
      <key>LSItemContentTypes</key>
      <array><string>public.folder</string><string>public.movie</string><string>public.data</string></array>
    </dict>
  </array>
  <key>NSRemovableVolumesUsageDescription</key><string>Para guardar los vídeos convertidos en la tarjeta de la tele.</string>
  <key>NSDownloadsFolderUsageDescription</key><string>Para leer los vídeos que quieres convertir.</string>
  <key>NSDesktopFolderUsageDescription</key><string>Para leer los vídeos que quieres convertir.</string>
  <key>NSDocumentsFolderUsageDescription</key><string>Para leer los vídeos que quieres convertir.</string>
</dict>
</plist>
EOF

codesign --force --sign - "$app/Contents/MacOS/ffmpeg" "$app/Contents/MacOS/ffprobe"
codesign --force --sign - "$app"
codesign --verify --strict "$app"
du -sh "$app"
