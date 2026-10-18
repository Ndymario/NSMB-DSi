#!/usr/bin/env bash
# Sets up everything a build needs, pinned, inside .deps/ (gitignored):
#   - NCPatcher v2, built from source
#   - NSMB-Code-Reference (headers, symbols, overlay region catalog)
#   - Nitro SDK / Nitro System headers converted for GCC (you supply the originals)
#   - optionally, stock melonDS and the headless test harness (tools/harness)
# and writes .ncpatcher.env so `ncpatcher` finds them without touching your shell profile.
#
# Usage: tools/bootstrap.sh --nitro <dir containing nitro.h and nnsys.h> [--harness]
set -euo pipefail

NCPATCHER_REPO=https://github.com/TheGameratorT/NCPatcher.git
NCPATCHER_REV=9f830757fac7b5034c76ada981cc5e34b2519c3e
NSMBREF_REPO=https://github.com/MammaMiaTeam/NSMB-Code-Reference.git
NSMBREF_REV=ac823910c51fca86145f6cf136728fa1d5ee6bc7
TEMPLATE_REPO=https://github.com/MammaMiaTeam/NSMB-Code-Template.git
TEMPLATE_REV=77f8d9739a5180806bcc3e94ba2328cb625a4235
MELONDS_REPO=https://github.com/melonDS-emu/melonDS.git
MELONDS_REV=b86390e4428bf38ce4c1ce0e9ca446d6d25955e8 # tag 1.1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/.deps"
NITRO_SRC=""
HARNESS=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		--nitro) NITRO_SRC="$2"; shift 2 ;;
		--harness) HARNESS=1; shift ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done

checkout() { # repo rev dir
	if [[ ! -d "$3/.git" ]]; then
		git clone -q "$1" "$3"
	fi
	git -C "$3" fetch -q origin "$2" 2>/dev/null || git -C "$3" fetch -q origin
	git -C "$3" -c advice.detachedHead=false checkout -q "$2"
}

mkdir -p "$DEPS"

echo "==> NCPatcher @ ${NCPATCHER_REV:0:7}"
checkout "$NCPATCHER_REPO" "$NCPATCHER_REV" "$DEPS/NCPatcher"
cmake -S "$DEPS/NCPatcher" -B "$DEPS/NCPatcher/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$DEPS/NCPatcher/build" -j >/dev/null
cmake --install "$DEPS/NCPatcher/build" --prefix "$DEPS/ncpatcher" >/dev/null

echo "==> NSMB-Code-Reference @ ${NSMBREF_REV:0:7}"
checkout "$NSMBREF_REPO" "$NSMBREF_REV" "$DEPS/NSMB-Code-Reference"

if [[ ! -f "$DEPS/nitrosdk/include/nitro.h" ]]; then
	if [[ -z "$NITRO_SRC" ]]; then
		echo "error: converted Nitro SDK headers are missing; rerun with --nitro <dir with nitro.h and nnsys.h>" >&2
		exit 1
	fi
	echo "==> Converting Nitro SDK headers from $NITRO_SRC"
	checkout "$TEMPLATE_REPO" "$TEMPLATE_REV" "$DEPS/NSMB-Code-Template"
	mkdir -p "$DEPS/nitrosdk/include"
	cp -R "$NITRO_SRC/." "$DEPS/nitrosdk/include/"
	(cd "$DEPS/nitrosdk" && python3 "$DEPS/NSMB-Code-Template/convert_sdk.py" >/dev/null)
fi

if [[ $HARNESS == 1 ]]; then
	echo "==> melonDS @ ${MELONDS_REV:0:7} + harness"
	checkout "$MELONDS_REPO" "$MELONDS_REV" "$DEPS/melonDS"
	cmake -S "$ROOT/tools/harness" -B "$DEPS/harness-build" -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build "$DEPS/harness-build" -j >/dev/null
fi

cat > "$ROOT/.ncpatcher.env" <<EOF
# Written by tools/bootstrap.sh. Overrides the shell environment for this project only.
NSMBREF_ROOT=$DEPS/NSMB-Code-Reference
NSMB_NITRO_ROOT=$DEPS/nitrosdk
EOF

echo
echo "Done. ncpatcher: $DEPS/ncpatcher/bin/ncpatcher"
echo "Add it to PATH, or run tools/ncp.sh which uses it directly."
