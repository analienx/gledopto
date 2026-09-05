#!/usr/bin/env bash
set -euo pipefail

# Offline-only TLSR8258 compile harness for the GL-SD read-only extraction core.
#
# This script compiles OBJECTS ONLY. It does not link an application, build a
# Zigbee OTA container, access Zigbee, or serve anything to a device.
#
# The Telink sampleLight app_cfg.h is intentionally used only to reproduce the
# SDK's public MCU/header/toolchain mechanics for MCU_CORE_8258. Its sample
# BOARD selection is NOT a GL-SD-301P board definition and MUST NOT be used as
# deployment evidence or as permission to initialize GPIO/PWM/power-stage I/O.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/firmware/wireless-dump-stager"

: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT to the tl_zigbee_sdk directory}"
TC32_CC="${TC32_CC:-tc32-elf-gcc}"
TC32_NM="${TC32_NM:-tc32-elf-nm}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd-tc32-objects}"

SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"

first_existing_dir() {
  local d
  for d in "$@"; do
    if [[ -d "$d" ]]; then
      printf '%s\n' "$d"
      return 0
    fi
  done
  return 1
}

first_existing_file() {
  local f
  for f in "$@"; do
    if [[ -f "$f" ]]; then
      printf '%s\n' "$f"
      return 0
    fi
  done
  return 1
}

APP_CFG_DIR="$(first_existing_dir \
  "$SDK/apps/zigbee/sampleLight" \
  "$SDK/apps/sampleLight")" || {
    echo "ERROR: no public Telink sampleLight app directory found" >&2
    exit 2
  }
APP_COMMON_DIR="$(first_existing_dir "$SDK/apps/common")" || {
  echo "ERROR: no public Telink apps/common directory found" >&2
  exit 2
}
ZB_API_H="$(first_existing_file \
  "$SDK/stack/zigbee/zbapi/zb_api.h" \
  "$SDK/zigbee/zbapi/zb_api.h")" || {
    echo "ERROR: no public Telink zb_api.h found" >&2
    exit 2
  }

required=(
  "$SDK/proj/tl_common.h"
  "$APP_CFG_DIR/app_cfg.h"
  "$APP_COMMON_DIR/comm_cfg.h"
  "$ZB_API_H"
)
for f in "${required[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "ERROR: required Telink SDK file not found: $f" >&2
    exit 2
  fi
done

if ! command -v "$TC32_CC" >/dev/null 2>&1; then
  echo "ERROR: TC32 compiler not found: $TC32_CC" >&2
  exit 2
fi

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.o "$OUT_DIR"/build-manifest.txt

# Common target identity. Do NOT globally predefine the legacy size_t guards:
# the portable core includes <stddef.h> directly and needs the compiler's own
# size_t typedef. The guards are required only for the adapter translation unit,
# where Telink headers establish their legacy type surface before our headers.
base_defines=(
  -DGLSD_TELINK_SDK
  -DMCU_CORE_8258=1
)
adapter_defines=(
  -D_SIZE_T
  -D_SIZE_T_
  -D__SIZE_T
  -D__SIZE_T__
)

# Public Telink generations use either stack/zigbee or zigbee as the Zigbee
# source root. Add whichever roots are present, recursively, together with the
# public app/proj/platform headers. This remains deterministic for a pinned SDK
# checkout while avoiding Eclipse-only include-path assumptions.
include_roots=("$SDK/proj" "$SDK/platform" "$APP_COMMON_DIR" "$APP_CFG_DIR")
[[ -d "$SDK/stack" ]] && include_roots+=("$SDK/stack")
[[ -d "$SDK/zigbee" ]] && include_roots+=("$SDK/zigbee")

includes=(-I"$APP_CFG_DIR" -I"$APP_COMMON_DIR" -I"$SDK/proj")
while IFS= read -r -d '' d; do
  includes+=("-I$d")
done < <(find "${include_roots[@]}" -type d -print0 | sort -zu)

cflags=(
  -std=gnu99
  -Wall
  -Wextra
  -ffunction-sections
  -fdata-sections
  -Os
)

sources=(
  glsd_stager_core.c
  glsd_stager_dispatch.c
  glsd_transport_adapter.c
  glsd_telink_sdk_adapter.c
)

{
  echo "OFFLINE_ONLY=YES"
  echo "OBJECT_ONLY=YES"
  echo "PRODUCTION_BOARD_PROFILE=UNRESOLVED"
  echo "SAMPLELIGHT_BOARD_PROFILE_IS_MECHANICS_ONLY=YES"
  echo "SDK_ROOT=$SDK"
  echo "APP_CFG_DIR=$APP_CFG_DIR"
  echo "ZB_API_H=$ZB_API_H"
  if git -C "$SDK" rev-parse HEAD >/dev/null 2>&1; then
    echo "SDK_GIT_HEAD=$(git -C "$SDK" rev-parse HEAD)"
    echo "SDK_GIT_DIRTY=$(git -C "$SDK" status --porcelain | wc -l | tr -d ' ')"
  else
    echo "SDK_GIT_HEAD=NON_GIT_CHECKOUT"
  fi
  echo "COMPILER=$TC32_CC"
  "$TC32_CC" --version | head -n 1 | sed 's/^/COMPILER_VERSION=/'
  printf 'BASE_DEFINES='; printf '%q ' "${base_defines[@]}"; echo
  printf 'ADAPTER_ONLY_DEFINES='; printf '%q ' "${adapter_defines[@]}"; echo
  printf 'CFLAGS='; printf '%q ' "${cflags[@]}"; echo
} | tee "$OUT_DIR/build-manifest.txt"

for name in "${sources[@]}"; do
  src="$SRC/$name"
  obj="$OUT_DIR/${name%.c}.o"
  defines=("${base_defines[@]}")
  if [[ "$name" == "glsd_telink_sdk_adapter.c" ]]; then
    defines+=("${adapter_defines[@]}")
  fi
  echo "[TC32] $name"
  "$TC32_CC" "${cflags[@]}" "${defines[@]}" "${includes[@]}" -I"$SRC" -c "$src" -o "$obj"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$obj" | tee -a "$OUT_DIR/build-manifest.txt"
  fi
  if command -v "$TC32_NM" >/dev/null 2>&1; then
    echo "UNDEFINED_SYMBOLS $name" >> "$OUT_DIR/build-manifest.txt"
    "$TC32_NM" -u "$obj" >> "$OUT_DIR/build-manifest.txt" || true
  fi
done

echo "GLSD_TC32_OBJECT_COMPILE=PASS_4_OF_4" | tee -a "$OUT_DIR/build-manifest.txt"
echo "Objects and manifest: $OUT_DIR"
echo "STOP: this harness does not establish a production board, link, OTA, or deployment gate."
