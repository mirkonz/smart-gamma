#!/usr/bin/env bash
set -euo pipefail

DEST_ROOT="${HOME}/Library/Application Support/obs-studio/plugins/smart-gamma.plugin"
SOURCE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

BUNDLE_SRC="${SOURCE_ROOT}/dist/local/smart-gamma.plugin"
if [[ -d "${BUNDLE_SRC}" ]]; then
  mkdir -p "$(dirname "${DEST_ROOT}")"
  ln -sfn "${BUNDLE_SRC}" "${DEST_ROOT}"
  echo "Linked bundle -> ${DEST_ROOT}"
  exit 0
fi

BIN_SRC="${SOURCE_ROOT}/dist/local/lib/obs-plugins/smart-gamma.so"
DATA_SRC="${SOURCE_ROOT}/dist/local/share/obs/obs-plugins/smart-gamma"
BUNDLE_BIN="${DEST_ROOT}/Contents/MacOS"
BUNDLE_RES="${DEST_ROOT}/Contents/Resources"

if [[ ! -f "${BIN_SRC}" ]]; then
  echo "Missing ${BIN_SRC}; run cmake --install first" >&2
  exit 1
fi

mkdir -p "${BUNDLE_BIN}" "${BUNDLE_RES%/*}"
ln -sfn "${BIN_SRC}" "${BUNDLE_BIN}/smart-gamma"
ln -sfn "${DATA_SRC}" "${BUNDLE_RES}"

echo "Linked binary -> ${BUNDLE_BIN}"
echo "Linked data   -> ${BUNDLE_RES}"
