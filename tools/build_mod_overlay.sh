#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAYLOAD_PATH="${1:-$ROOT_DIR/build/mod_overlay_payload.bin}"
OUTPUT_PATH="$ROOT_DIR/nitrofs/z_new/mod/modovl.bin"

if [[ ! -f "$PAYLOAD_PATH" ]]; then
  echo "Missing payload: $PAYLOAD_PATH" >&2
  echo "Build your raw overlay payload first, then rerun this script." >&2
  exit 1
fi

python3 "$ROOT_DIR/tools/pack_mod_overlay.py" \
  --payload "$PAYLOAD_PATH" \
  --out "$OUTPUT_PATH" \
  --entry-offset "${MODOVL_ENTRY_OFFSET:-0}" \
  --exports-offset "${MODOVL_EXPORTS_OFFSET:-0}" \
  --bss-size "${MODOVL_BSS_SIZE:-0}" \
  --abi-version "${MODOVL_ABI_VERSION:-1}"

echo "Packed mod overlay: $OUTPUT_PATH"

