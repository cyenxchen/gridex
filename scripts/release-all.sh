#!/bin/bash
# release-all.sh — Build, sign, and package DMGs for both architectures.
#
# Usage:
#   ./scripts/release-all.sh
#
# Produces:
#   dist/Gridex-<version>-arm64.dmg     (Apple Silicon)
#   dist/Gridex-<version>-x86_64.dmg    (Intel)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo ""
echo "╔═══════════════════════════════════════════╗"
echo "║      Gridex — Multi-Arch Release          ║"
echo "╚═══════════════════════════════════════════╝"

echo ""
echo "━━━ Building for Apple Silicon (arm64) ━━━"
echo ""
ARCH=arm64 "$SCRIPT_DIR/release.sh"

echo ""
echo "━━━ Building for Intel (x86_64) ━━━"
echo ""
ARCH=x86_64 "$SCRIPT_DIR/release.sh"

echo ""
echo "╔═══════════════════════════════════════════╗"
echo "║  ✓ All builds complete                    ║"
echo "╚═══════════════════════════════════════════╝"
echo ""
echo "Artifacts:"
ls -lh "$(cd "$SCRIPT_DIR/.." && pwd)/dist/"*.dmg 2>/dev/null || true
