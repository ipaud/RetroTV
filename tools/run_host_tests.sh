#!/usr/bin/env bash
# Builds and runs the pure C++ tests on this computer, then the make_index.py self-test (and on macOS,
# RetroTV Importar's).
# No board needed.
#
# Usage: tools/run_host_tests.sh
#   CXX=g++-16 tools/run_host_tests.sh   # another compiler (default: c++)
#   SANITIZE=0 tools/run_host_tests.sh   # toolchains without ASan/UBSan
#   (Homebrew GCC on macOS also needs SDKROOT=$(xcrun --show-sdk-path) to find the C headers.)
#
# Warnings: our code (src/, include/, test/) builds with -Wall -Wextra -Werror. Third-party
# headers (ArduinoJson) come in through -isystem, so GCC and Clang treat them as system
# headers. GCC still reports two false positives it finds inside ArduinoJson after inlining;
# test/third_party.h, forced first into every file, ignores exactly those around the library.
# Checked with clang and GCC 16, with and without sanitizers.
set -euo pipefail

cd "$(dirname "$0")/.."
json_src=".pio/libdeps/pautv/ArduinoJson/src"
if [[ ! -f "$json_src/ArduinoJson.h" ]]; then
  echo "fetching ArduinoJson (pio pkg install)..."
  pio pkg install -e pautv >/dev/null
fi

sanitize=()
if [[ "${SANITIZE:-1}" != "0" ]]; then
  sanitize=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

out_dir="$(mktemp -d)"
trap 'rm -rf "$out_dir"' EXIT

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -g -O1 ${sanitize[@]+"${sanitize[@]}"} \
  -I include -I src -isystem "$json_src" -include test/third_party.h \
  test/*.cpp src/channels/ChannelManager.cpp src/network/RemoteProtocol.cpp src/network/WifiNetworks.cpp src/web/RemoteApi.cpp src/web/ConfigApi.cpp -o "$out_dir/host_tests"

"$out_dir/host_tests"
python3 tools/make_index.py --self-test
python3 tools/make_dist.py --self-test
if [[ "$(uname)" == "Darwin" ]] && command -v swiftc >/dev/null; then
  importer/build.sh
  importer/build/RetroTVImporter --self-test
fi
