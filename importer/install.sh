#!/usr/bin/env bash
# Puts RetroTV Importar on a card (docs/IMPORTER.md): builds it, then copies it to the card's root without
# ._ files nor the download mark, so it opens there without asking.
#
# Usage: importer/install.sh /Volumes/RETROTV
set -euo pipefail

card="${1:-}"
[[ -n "$card" && -d "$card" ]] || { echo "usage: importer/install.sh /Volumes/<card>   (card not found: ${card:-none})"; exit 2; }
cd "$(dirname "$0")"
./build.sh app >/dev/null
rm -rf "${card%/}/RetroTV Importar.app"
ditto --norsrc --noextattr --noqtn "build/RetroTV Importar.app" "${card%/}/RetroTV Importar.app"
echo "RetroTV Importar.app -> ${card%/}/"
