#!/bin/bash
# release.sh — Full release pipeline in one command:
#   build → sign → notarize .app → package DMG → sign → notarize DMG
#
# Usage:
#   ./scripts/release.sh                      # Build for host arch
#   ARCH=x86_64 ./scripts/release.sh          # Cross-compile for Intel
#
# Env:
#   ARCH             arm64 | x86_64 (default: host uname -m)
#   SIGN_IDENTITY, NOTARY_PROFILE, NOTARIZE — same as build-app.sh / sign-notarize.sh
#
# Produces: dist/Gridex-<version>-<arch>.dmg (notarized, stapled, ready to ship)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_DIR"

VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" macos/Resources/Info.plist)
export ARCH="${ARCH:-$(uname -m)}"

echo "═══════════════════════════════════════════"
echo "  Gridex Release Pipeline"
echo "  Version: $VERSION"
echo "  Arch:    $ARCH"
echo "═══════════════════════════════════════════"

# 1. Build + sign + notarize the .app
"$SCRIPT_DIR/build-app.sh" release

# 2. Package + sign + notarize the DMG
"$SCRIPT_DIR/make-dmg.sh"

echo ""
echo "═══════════════════════════════════════════"
echo "  ✓ Release artifacts:"
ls -lh "$PROJECT_DIR/dist/"*.dmg 2>/dev/null || true
echo "═══════════════════════════════════════════"
echo ""
echo "Next:"
echo "  • Upload DMG to R2:    wrangler r2 object put gridex-downloads/releases/Gridex-${VERSION}-${ARCH}.dmg --file dist/Gridex-${VERSION}-${ARCH}.dmg"
echo "  • Update landing env:  NEXT_PUBLIC_DOWNLOAD_MAC_ARM64=https://downloads.gridex.app/releases/Gridex-${VERSION}-${ARCH}.dmg"
