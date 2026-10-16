#!/usr/bin/env bash
# Runs the pinned ncpatcher from .deps/ (see tools/bootstrap.sh) against this project.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NCP="$ROOT/.deps/ncpatcher/bin/ncpatcher"
if [[ ! -x "$NCP" ]]; then
	echo "error: $NCP not found; run tools/bootstrap.sh first" >&2
	exit 1
fi
exec "$NCP" -C "$ROOT" "$@"
