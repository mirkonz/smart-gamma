#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <build-dir> <platform-tag> [output-dir]" >&2
  exit 1
fi

BUILD_DIR="$1"
PLATFORM="$2"
OUTPUT_DIR="${3:-dist}"
INSTALL_ROOT="${OUTPUT_DIR}/${PLATFORM}"
ARTIFACT_NAME="smart-gamma-${PLATFORM}.zip"
ARTIFACT_PATH="${OUTPUT_DIR}/${ARTIFACT_NAME}"

mkdir -p "${OUTPUT_DIR}"

CONFIG_ARGS=()
if [[ -n "${BUILD_CONFIG:-}" ]]; then
  CONFIG_ARGS=(--config "${BUILD_CONFIG}")
fi

cmake --install "${BUILD_DIR}" "${CONFIG_ARGS[@]}" --prefix "${INSTALL_ROOT}" >/dev/null

pushd "${OUTPUT_DIR}" >/dev/null
cmake -E tar cf "${ARTIFACT_NAME}" --format=zip "${PLATFORM}"
popd >/dev/null

echo "Created ${OUTPUT_DIR}/${ARTIFACT_NAME}"
