#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/ordinary_overlay"
SRC="$ROOT_DIR/source/ports/Ordinary/ordinary_overlay/ordinary_overlay.cpp"
LDSCRIPT="$ROOT_DIR/tools/ordinary_overlay.ld"
OBJ="$BUILD_DIR/ordinary_overlay.o"
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

build_variant() {
  local variant="$1"
  local load_addr="$2"
  local payload="$3"
  local packed="$4"
  local compat="${5:-}"

  local elf="$BUILD_DIR/ordinary_overlay_${variant}.elf"
  local map="$BUILD_DIR/ordinary_overlay_${variant}.map"
  local variant_ldscript="$BUILD_DIR/ordinary_overlay_${variant}.ld"

  sed "s/__ORDINARY_OVERLAY_LOAD_ADDR__/$load_addr/g" "$LDSCRIPT" > "$variant_ldscript"

  "$CXX" \
    -nostdlib \
    -Wl,-T,"$variant_ldscript" \
    -Wl,-Map,"$map" \
    -Wl,--gc-sections \
    -o "$elf" \
    "$OBJ"

  "$OBJCOPY" -O binary "$elf" "$payload"

  local entry_addr_hex
  local exports_addr_hex
  entry_addr_hex="$("$NM" -g "$elf" | awk '/ OrdinaryOverlay_Entry$/{print $1}')"
  exports_addr_hex="$("$NM" -g "$elf" | awk '/ g_ordinary_exports$/{print $1}')"
  if [[ -z "$entry_addr_hex" || -z "$exports_addr_hex" ]]; then
    echo "Failed to resolve required overlay symbols from ELF for $variant." >&2
    exit 1
  fi

  local payload_base
  payload_base=$((load_addr))
  local entry_offset
  local exports_offset
  local bss_size
  entry_offset=$((16#$entry_addr_hex - payload_base))
  exports_offset=$((16#$exports_addr_hex - payload_base))
  bss_size="$("$SIZE" -A "$elf" | awk '$1==".bss"{print $2+0}')"

  python3 "$ROOT_DIR/tools/pack_mod_overlay.py" \
    --payload "$payload" \
    --out "$packed" \
    --entry-offset "$entry_offset" \
    --exports-offset "$exports_offset" \
    --bss-size "$bss_size" \
    --abi-version 1

  if [[ -n "$compat" ]]; then
    cp "$packed" "$compat"
  fi

  local variant_prefix
  case "$variant" in
    dsi) variant_prefix="DSI" ;;
    expansion) variant_prefix="EXPANSION" ;;
    *)
      echo "Unknown overlay variant: $variant" >&2
      exit 1
      ;;
  esac

  printf -v "${variant_prefix}_ENTRY_OFFSET_HEX" '0x%X' "$entry_offset"
  printf -v "${variant_prefix}_EXPORTS_OFFSET_HEX" '0x%X' "$exports_offset"
  printf -v "${variant_prefix}_BSS_SIZE" '%u' "$bss_size"
}

DSI_PAYLOAD="$ROOT_DIR/build/ordinary_overlay_payload.bin"
DSI_PACKED="$ROOT_DIR/nitrofs/z_new/mod/ordinaryovl.bin"
DSI_COMPAT="$ROOT_DIR/nitrofs/z_new/mod/modovl.bin"
EXP_PAYLOAD="$ROOT_DIR/build/ordinary_overlay_payload_expansion.bin"
EXP_PACKED="$ROOT_DIR/nitrofs/z_new/mod/ordinaryovl_expansion.bin"

build_variant "dsi" 0x02800020 "$DSI_PAYLOAD" "$DSI_PACKED" "$DSI_COMPAT"
build_variant "expansion" 0x09000020 "$EXP_PAYLOAD" "$EXP_PACKED"

cat > "$ENV_FILE" <<EOF
MODOVL_ENTRY_OFFSET=${DSI_ENTRY_OFFSET_HEX}
MODOVL_EXPORTS_OFFSET=${DSI_EXPORTS_OFFSET_HEX}
MODOVL_BSS_SIZE=${DSI_BSS_SIZE}
DSI_MODOVL_ENTRY_OFFSET=${DSI_ENTRY_OFFSET_HEX}
DSI_MODOVL_EXPORTS_OFFSET=${DSI_EXPORTS_OFFSET_HEX}
DSI_MODOVL_BSS_SIZE=${DSI_BSS_SIZE}
EXPANSION_MODOVL_ENTRY_OFFSET=${EXPANSION_ENTRY_OFFSET_HEX}
EXPANSION_MODOVL_EXPORTS_OFFSET=${EXPANSION_EXPORTS_OFFSET_HEX}
EXPANSION_MODOVL_BSS_SIZE=${EXPANSION_BSS_SIZE}
EOF

echo "Built ordinary overlay payloads:"
printf "  dsi payload      : %s\n" "$DSI_PAYLOAD"
printf "  dsi packed       : %s\n" "$DSI_PACKED"
printf "  dsi compat       : %s\n" "$DSI_COMPAT"
printf "  expansion payload: %s\n" "$EXP_PAYLOAD"
printf "  expansion packed : %s\n" "$EXP_PACKED"
printf "  env              : %s\n" "$ENV_FILE"
printf "  dsi entry_offset=%s exports_offset=%s bss_size=%u\n" \
  "$DSI_ENTRY_OFFSET_HEX" "$DSI_EXPORTS_OFFSET_HEX" "$DSI_BSS_SIZE"
printf "  expansion entry_offset=%s exports_offset=%s bss_size=%u\n" \
  "$EXPANSION_ENTRY_OFFSET_HEX" "$EXPANSION_EXPORTS_OFFSET_HEX" "$EXPANSION_BSS_SIZE"
