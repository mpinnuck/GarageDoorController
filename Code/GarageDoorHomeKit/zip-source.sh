#!/usr/bin/env bash
# Zips the project source (excluding build output and VCS data) into
# GarageDoorHomeKit.zip in this folder, replacing any previous archive.
set -euo pipefail

cd "$(dirname "$0")"

OUT="GarageDoorHomeKit.zip"
rm -f "$OUT"

zip -r "$OUT" . \
  -x ".git/*" \
  -x "*/.pio/*" ".pio/*" \
  -x "*/.DS_Store" ".DS_Store" \
  -x "*.zip"

echo "Created $(pwd)/$OUT"
