#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-/workspace}"
PAYLOAD_PATH="${MODOVL_PAYLOAD_PATH:-$ROOT_DIR/build/mod_overlay_payload.bin}"

if [[ ! -f "$PAYLOAD_PATH" ]]; then
  echo "[modovl] Payload not found: $PAYLOAD_PATH"
  echo "[modovl] Skipping modovl.bin pack for this build."
  echo "[modovl] Put the payload at /workspace/build/mod_overlay_payload.bin or set MODOVL_PAYLOAD_PATH."
  exit 0
fi

"$ROOT_DIR/tools/build_mod_overlay.sh" "$PAYLOAD_PATH"
