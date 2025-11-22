#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <source-dir> <output-dmg>" >&2
  exit 1
fi

SOURCE_DIR="$1"
OUTPUT_DMG="$2"
VOL_NAME="Smart Gamma"

if [[ ! -d "$SOURCE_DIR" ]]; then
  echo "Source directory $SOURCE_DIR not found" >&2
  exit 1
fi

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

rsync -a "$SOURCE_DIR/" "$TEMP_DIR/"

hdiutil create -ov -volname "$VOL_NAME" -srcfolder "$TEMP_DIR" -ov -format UDZO "$OUTPUT_DMG"
echo "Created DMG: $OUTPUT_DMG"
