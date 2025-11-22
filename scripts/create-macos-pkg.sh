#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <install-root> <output-pkg>" >&2
  exit 1
fi

INSTALL_ROOT="$1"
OUTPUT_PKG="$2"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_SRC="${INSTALL_ROOT}/lib/obs-plugins/smart-gamma.so"
DATA_SRC="${INSTALL_ROOT}/share/obs/obs-plugins/smart-gamma"
PKG_IDENTIFIER="${PKG_IDENTIFIER:-com.mirkonz.obs-smart-gamma}"

if [[ ! -f "$BIN_SRC" ]]; then
  echo "Missing plugin binary at $BIN_SRC" >&2
  exit 1
fi

if [[ ! -d "$DATA_SRC" ]]; then
  echo "Missing plugin resources at $DATA_SRC" >&2
  exit 1
fi

if command -v python3 >/dev/null 2>&1; then
  PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
  PYTHON_BIN="python"
else
  echo "python3 or python is required to extract the project version" >&2
  exit 1
fi

PROJECT_VERSION="$(
  cd "$PROJECT_ROOT" && "$PYTHON_BIN" - <<'PY'
import pathlib
import re
cmake = pathlib.Path("CMakeLists.txt").read_text()
match = re.search(r'project\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', cmake, re.IGNORECASE)
if not match:
    raise SystemExit("Unable to determine project version from CMakeLists.txt")
print(match.group(1), end="")
PY
)"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

PAYLOAD_ROOT="${WORK_DIR}/pkgroot/Library/Application Support/obs-studio/plugins"
BUNDLE_ROOT="${PAYLOAD_ROOT}/smart-gamma.plugin"
CONTENTS_DIR="${BUNDLE_ROOT}/Contents"
BIN_DEST="${CONTENTS_DIR}/MacOS"
RES_DEST="${CONTENTS_DIR}/Resources"

mkdir -p "$BIN_DEST" "$RES_DEST"

echo "Staging Smart Gamma bundle at $BUNDLE_ROOT"
cp "$BIN_SRC" "${BIN_DEST}/smart-gamma"
cp -R "${DATA_SRC}/." "$RES_DEST/"

INFO_PLIST="${CONTENTS_DIR}/Info.plist"
cat >"$INFO_PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>English</string>
  <key>CFBundleExecutable</key>
  <string>smart-gamma</string>
  <key>CFBundleIdentifier</key>
  <string>${PKG_IDENTIFIER}.plugin</string>
  <key>CFBundleName</key>
  <string>Smart Gamma</string>
  <key>CFBundlePackageType</key>
  <string>BNDL</string>
  <key>CFBundleShortVersionString</key>
  <string>${PROJECT_VERSION}</string>
  <key>CFBundleVersion</key>
  <string>${PROJECT_VERSION}</string>
</dict>
</plist>
EOF

mkdir -p "$(dirname "$OUTPUT_PKG")"
COMPONENT_PKG="${WORK_DIR}/smart-gamma-component.pkg"
pkgbuild \
  --root "${WORK_DIR}/pkgroot" \
  --identifier "$PKG_IDENTIFIER" \
  --version "$PROJECT_VERSION" \
  --install-location "/" \
  "$COMPONENT_PKG"

COMPONENT_NAME="$(basename "$COMPONENT_PKG")"
DISTRIBUTION="${WORK_DIR}/Distribution.xml"
cat >"$DISTRIBUTION" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Smart Gamma</title>
  <options customize="never" require-scripts="false"/>
  <domains enable_anywhere="false" enable_localSystem="false" enable_currentUserHome="true"/>
  <choices-outline>
    <line choice="smartgamma"/>
  </choices-outline>
  <choice id="smartgamma" visible="false" title="Smart Gamma">
    <pkg-ref id="${PKG_IDENTIFIER}"/>
  </choice>
  <pkg-ref id="${PKG_IDENTIFIER}" version="${PROJECT_VERSION}" onConclusion="none">${COMPONENT_NAME}</pkg-ref>
</installer-gui-script>
EOF

productbuild \
  --distribution "$DISTRIBUTION" \
  --package-path "$WORK_DIR" \
  "$OUTPUT_PKG"

echo "Created PKG (installs into current user's Library): $OUTPUT_PKG"
