#!/usr/bin/env bash
# Build AppImage for Gridex Linux.
# Requires: linuxdeploy + linuxdeploy-plugin-qt in PATH.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${LINUX_ROOT}/build}"
APPDIR="${APPDIR:-${LINUX_ROOT}/AppDir}"
OUT_DIR="${OUT_DIR:-${LINUX_ROOT}/dist}"

command -v linuxdeploy >/dev/null || { echo "linuxdeploy not found"; exit 1; }
command -v linuxdeploy-plugin-qt >/dev/null || { echo "linuxdeploy-plugin-qt not found"; exit 1; }

mkdir -p "${BUILD_DIR}" "${OUT_DIR}"
rm -rf "${APPDIR}"

cmake -S "${LINUX_ROOT}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel
DESTDIR="${APPDIR}" cmake --install "${BUILD_DIR}" --prefix /usr

install -Dm644 "${SCRIPT_DIR}/gridex.desktop" "${APPDIR}/usr/share/applications/gridex.desktop"
install -Dm755 "${SCRIPT_DIR}/AppRun" "${APPDIR}/AppRun"

# Icon: use placeholder if bundled icon absent.
ICON_SRC="${SCRIPT_DIR}/gridex.png"
if [ ! -f "${ICON_SRC}" ]; then
    ICON_SRC="${LINUX_ROOT}/../logo.png"
fi
install -Dm644 "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/512x512/apps/gridex.png"

cd "${OUT_DIR}"
linuxdeploy --appdir "${APPDIR}" --plugin qt --output appimage \
    --desktop-file "${APPDIR}/usr/share/applications/gridex.desktop" \
    --icon-file "${APPDIR}/usr/share/icons/hicolor/512x512/apps/gridex.png"

echo "AppImage produced under: ${OUT_DIR}"
