#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/ordinary_overlay"
SRC="$ROOT_DIR/source/ports/Ordinary/ordinary_overlay/ordinary_overlay.cpp"
LDSCRIPT="$ROOT_DIR/tools/ordinary_overlay.ld"
ELF="$BUILD_DIR/ordinary_overlay.elf"
OBJ="$BUILD_DIR/ordinary_overlay.o"
MAP="$BUILD_DIR/ordinary_overlay.map"
PAYLOAD="$ROOT_DIR/build/ordinary_overlay_payload.bin"
OUT_BIN="$ROOT_DIR/nitrofs/z_new/mod/ordinaryovl.bin"
OUT_COMPAT="$ROOT_DIR/nitrofs/z_new/mod/modovl.bin"
ENV_FILE="$ROOT_DIR/build/ordinary_overlay_payload.env"

CXX="${ARM_NONE_EABI_CXX:-arm-none-eabi-g++}"
OBJCOPY="${ARM_NONE_EABI_OBJCOPY:-arm-none-eabi-objcopy}"
NM="${ARM_NONE_EABI_NM:-arm-none-eabi-nm}"
SIZE="${ARM_NONE_EABI_SIZE:-arm-none-eabi-size}"

mkdir -p "$BUILD_DIR"

"$CXX" \
  -c "$SRC" \
  -I"$ROOT_DIR/source" \
  -std=c++20 \
  -mcpu=arm946e-s \
  -marm \
  -mthumb-interwork \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-threadsafe-statics \
  -fno-unwind-tables \
  -fno-asynchronous-unwind-tables \
  -fno-builtin \
  -fdata-sections \
  -ffunction-sections \
  -Os \
  -o "$OBJ"

"$CXX" \
  -nostdlib \
  -Wl,-T,"$LDSCRIPT" \
  -Wl,-Map,"$MAP" \
  -Wl,--gc-sections \
  -o "$ELF" \
  "$OBJ"

"$OBJCOPY" -O binary "$ELF" "$PAYLOAD"

entry_addr_hex="$("$NM" -g "$ELF" | awk '/ OrdinaryOverlay_Entry$/{print $1}')"
exports_addr_hex="$("$NM" -g "$ELF" | awk '/ g_ordinary_exports$/{print $1}')"
if [[ -z "$entry_addr_hex" || -z "$exports_addr_hex" ]]; then
  echo "Failed to resolve required overlay symbols from ELF." >&2
  exit 1
fi

payload_base=0x02800020
entry_offset=$((16#$entry_addr_hex - payload_base))
exports_offset=$((16#$exports_addr_hex - payload_base))
bss_size="$("$SIZE" -A "$ELF" | awk '$1==".bss"{print $2+0}')"

python3 "$ROOT_DIR/tools/pack_mod_overlay.py" \
  --payload "$PAYLOAD" \
  --out "$OUT_BIN" \
  --entry-offset "$entry_offset" \
  --exports-offset "$exports_offset" \
  --bss-size "$bss_size" \
  --abi-version 1

# Keep compatibility with current runtime default path.
cp "$OUT_BIN" "$OUT_COMPAT"

cat > "$ENV_FILE" <<EOF
MODOVL_ENTRY_OFFSET=$entry_offset
MODOVL_EXPORTS_OFFSET=$exports_offset
MODOVL_BSS_SIZE=$bss_size
EOF

echo "Built ordinary overlay payload:"
echo "  payload: $PAYLOAD"
echo "  packed : $OUT_BIN"
echo "  compat : $OUT_COMPAT"
echo "  env    : $ENV_FILE"
printf "  entry_offset=0x%X exports_offset=0x%X bss_size=%u\n" \
  "$entry_offset" "$exports_offset" "$bss_size"
