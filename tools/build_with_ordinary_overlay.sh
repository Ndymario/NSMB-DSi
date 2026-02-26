#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$ROOT_DIR/tools/build_ordinary_overlay_payload.sh"
source "$ROOT_DIR/build/ordinary_overlay_payload.env"

MODOVL_ENTRY_OFFSET="$MODOVL_ENTRY_OFFSET" \
MODOVL_EXPORTS_OFFSET="$MODOVL_EXPORTS_OFFSET" \
MODOVL_BSS_SIZE="$MODOVL_BSS_SIZE" \
  "$ROOT_DIR/tools/build_with_overlay.sh" "build/ordinary_overlay_payload.bin"

