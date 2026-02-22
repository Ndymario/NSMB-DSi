#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAYLOAD_INPUT="${1:-build/mod_overlay_payload.bin}"

if [[ "$PAYLOAD_INPUT" = /* ]]; then
  case "$PAYLOAD_INPUT" in
    "$ROOT_DIR"/*) PAYLOAD_IN_CONTAINER="/workspace/${PAYLOAD_INPUT#"$ROOT_DIR"/}" ;;
    *)
      echo "Payload path must be inside the workspace: $ROOT_DIR" >&2
      exit 1
      ;;
  esac
else
  PAYLOAD_IN_CONTAINER="/workspace/${PAYLOAD_INPUT#./}"
fi

cd "$ROOT_DIR"
DOCKER_TTY_FLAG=""
if [[ -t 0 && -t 1 ]]; then
  DOCKER_TTY_FLAG="-it"
fi

DOCKER_ENV_ARGS=(
  -e MODOVL_PAYLOAD_PATH="$PAYLOAD_IN_CONTAINER"
)

if [[ -n "${MODOVL_ENTRY_OFFSET:-}" ]]; then
  DOCKER_ENV_ARGS+=(-e MODOVL_ENTRY_OFFSET="$MODOVL_ENTRY_OFFSET")
fi
if [[ -n "${MODOVL_EXPORTS_OFFSET:-}" ]]; then
  DOCKER_ENV_ARGS+=(-e MODOVL_EXPORTS_OFFSET="$MODOVL_EXPORTS_OFFSET")
fi
if [[ -n "${MODOVL_BSS_SIZE:-}" ]]; then
  DOCKER_ENV_ARGS+=(-e MODOVL_BSS_SIZE="$MODOVL_BSS_SIZE")
fi
if [[ -n "${MODOVL_ABI_VERSION:-}" ]]; then
  DOCKER_ENV_ARGS+=(-e MODOVL_ABI_VERSION="$MODOVL_ABI_VERSION")
fi

docker run --rm ${DOCKER_TTY_FLAG:+$DOCKER_TTY_FLAG} \
  "${DOCKER_ENV_ARGS[@]}" \
  -v ./source:/app/source \
  -v ./nitrofs:/app/nitrofs \
  -v ./ncpatcher.json:/app/ncpatcher.json \
  -v "$PWD:/workspace" \
  -v nsmb-data:/data \
  nsmb-docker
