#!/usr/bin/env bash
# Zips the project source (excluding build output and VCS data) into
# GarageDoorController.zip in this folder, replacing any previous archive.
set -euo pipefail

cd "$(dirname "$0")"

OUT="GarageDoorController.zip"
rm -f "$OUT"

zip -r "$OUT" . \
  -x ".git/*" \
  -x "*/.pio/*" ".pio/*" \
  -x "*/.DS_Store" ".DS_Store" \
  -x "include/secrets.h" \
  -x "*.zip"

echo "Created $(pwd)/$OUT"
