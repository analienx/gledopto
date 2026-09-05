#!/usr/bin/env bash
set -euo pipefail

# Offline-only TLSR8258 compile harness for the GL-SD read-only extraction core.
#
# This script compiles OBJECTS ONLY. It does not link an application, build a
# Zigbee OTA container, access Zigbee, or serve anything to a device.
#
# The Telink sampleLight app_cfg.h is intentionally used only to reproduce the
# SDK's public MCU/header/toolchain mechanics for MCU_CORE_8258. Its dongle
# BOARD selection is NOT a GL-SD-301P board definition and MUST NOT be used as
# deployment evidence or as permission to initialize GPIO/PWM/power-stage I/O.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/firmware/wireless-dump-stager"

: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT to the tl_zigbee_sdk directory}"
TC32_CC="${TC32_CC:-tc32-elf-gcc}"
TC32_NM="${TC32_NM:-tc32-elf-nm}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd-tc32-objects}"

SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"
APP_CFG_DIR="$SDK/apps/zigbee/sampleLight"

required=(
  "$SDK/proj/tl_common.h"
  "$APP_CFG_DIR/app_cfg.h"
  "$SDK/stack/zigbee/zbapi/zb_api.h"
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

# Telink's legacy TC32 environment uses these guards around its size_t surface.
# Batch-5 independently reproduced the clash without them. Keep them confined
# to this target mechanics harness rather than polluting portable source.
defines=(
  -DGLSD_TELINK_SDK
  -DMCU_CORE_8258=1
  -D_SIZE_T
  -D_SIZE_T_
  -D__SIZE_T
  -D__SIZE_T__
)

# Header layout varies slightly across public Telink revisions. Instead of
# embedding an Eclipse-only include list, use every SDK/application directory
# that actually contains headers. This is deterministic for a pinned checkout
# and avoids silently substituting host headers for Telink headers.
includes=(-I"$APP_CFG_DIR" -I"$SDK/proj")
while IFS= read -r -d '' d; do
  includes+=("-I$d")
done < <(find "$SDK/proj" "$SDK/platform" "$SDK/stack" -type d -print0 | sort -z)

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
  if git -C "$SDK" rev-parse HEAD >/dev/null 2>&1; then
    echo "SDK_GIT_HEAD=$(git -C "$SDK" rev-parse HEAD)"
    echo "SDK_GIT_DIRTY=$(git -C "$SDK" status --porcelain | wc -l | tr -d ' ')"
  else
    echo "SDK_GIT_HEAD=NON_GIT_CHECKOUT"
  fi
  echo "COMPILER=$TC32_CC"
  "$TC32_CC" --version | head -n 1 | sed 's/^/COMPILER_VERSION=/'
  printf 'DEFINES='; printf '%q ' "${defines[@]}"; echo
  printf 'CFLAGS='; printf '%q ' "${cflags[@]}"; echo
} | tee "$OUT_DIR/build-manifest.txt"

for name in "${sources[@]}"; do
  src="$SRC/$name"
  obj="$OUT_DIR/${name%.c}.o"
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
