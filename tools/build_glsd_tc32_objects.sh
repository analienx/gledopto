#!/usr/bin/env bash
set -euo pipefail

# Offline-only TLSR8258 compile harness for the GL-SD read-only extraction core
# and minimal stager application shell. OBJECTS ONLY: no OTA is generated or
# served and no hardware is accessed.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/firmware/wireless-dump-stager"
FIXTURE="$SRC/telink_fixture"

: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT to the tl_zigbee_sdk directory}"
TC32_CC="${TC32_CC:-tc32-elf-gcc}"
TC32_NM="${TC32_NM:-tc32-elf-nm}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd-tc32-objects}"
SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"

first_existing_dir() {
  local d
  for d in "$@"; do [[ -d "$d" ]] && { printf '%s\n' "$d"; return 0; }; done
  return 1
}
first_existing_file() {
  local f
  for f in "$@"; do [[ -f "$f" ]] && { printf '%s\n' "$f"; return 0; }; done
  return 1
}

SAMPLE_DIR="$(first_existing_dir "$SDK/apps/zigbee/sampleLight" "$SDK/apps/sampleLight")" || {
  echo "ERROR: no public Telink sampleLight directory" >&2; exit 2;
}
APP_COMMON_DIR="$(first_existing_dir "$SDK/apps/common")" || {
  echo "ERROR: no public Telink apps/common directory" >&2; exit 2;
}
ZB_API_H="$(first_existing_file "$SDK/stack/zigbee/zbapi/zb_api.h" "$SDK/zigbee/zbapi/zb_api.h")" || {
  echo "ERROR: no public Telink zb_api.h" >&2; exit 2;
}

for f in "$SDK/proj/tl_common.h" "$FIXTURE/app_cfg.h" "$FIXTURE/stack_cfg.h" \
         "$FIXTURE/version_cfg.h" "$APP_COMMON_DIR/comm_cfg.h" "$ZB_API_H" \
         "$SAMPLE_DIR/board_8258_dongle.h"; do
  [[ -f "$f" ]] || { echo "ERROR: required file missing: $f" >&2; exit 2; }
done
command -v "$TC32_CC" >/dev/null 2>&1 || { echo "ERROR: TC32 compiler not found: $TC32_CC" >&2; exit 2; }

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.o "$OUT_DIR"/build-manifest.txt

base_defines=(-DGLSD_TELINK_SDK -DMCU_CORE_8258=1 -DROUTER=1 -DMCU_STARTUP_8258=1)
telink_first_defines=(-D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__)

include_roots=("$SDK/proj" "$SDK/platform" "$APP_COMMON_DIR" "$SAMPLE_DIR")
[[ -d "$SDK/stack" ]] && include_roots+=("$SDK/stack")
[[ -d "$SDK/zigbee" ]] && include_roots+=("$SDK/zigbee")
# FIXTURE must precede the SDK sample so tl_common.h resolves our fail-closed app_cfg.h.
includes=(-I"$FIXTURE" -I"$SAMPLE_DIR" -I"$APP_COMMON_DIR" -I"$SDK/proj")
while IFS= read -r -d '' d; do includes+=("-I$d"); done < <(find "${include_roots[@]}" -type d -print0 | sort -zu)

cflags=(-std=gnu99 -Wall -Wextra -ffunction-sections -fdata-sections -fshort-enums -funsigned-char -Os)
sources=(
  glsd_stager_core.c
  glsd_stager_dispatch.c
  glsd_transport_adapter.c
  glsd_telink_sdk_adapter.c
  glsd_telink_stager_app.c
  glsd_telink_disabled_feature_glue.c
)

{
  echo "OFFLINE_ONLY=YES"
  echo "OBJECT_ONLY=YES"
  echo "PRODUCTION_BOARD_PROFILE=UNRESOLVED"
  echo "TELINK_FIXTURE_APP_CFG=$FIXTURE/app_cfg.h"
  echo "SDK_ROOT=$SDK"
  echo "SAMPLE_BOARD_HEADERS=$SAMPLE_DIR"
  echo "ZB_API_H=$ZB_API_H"
  git -C "$SDK" rev-parse HEAD 2>/dev/null | sed 's/^/SDK_GIT_HEAD=/' || true
  echo "COMPILER=$TC32_CC"
  "$TC32_CC" --version | head -n 1 | sed 's/^/COMPILER_VERSION=/'
  printf 'BASE_DEFINES='; printf '%q ' "${base_defines[@]}"; echo
  printf 'TELINK_FIRST_DEFINES='; printf '%q ' "${telink_first_defines[@]}"; echo
  printf 'CFLAGS='; printf '%q ' "${cflags[@]}"; echo
} | tee "$OUT_DIR/build-manifest.txt"

for name in "${sources[@]}"; do
  src="$SRC/$name"
  obj="$OUT_DIR/${name%.c}.o"
  defines=("${base_defines[@]}")
  case "$name" in
    glsd_telink_sdk_adapter.c|glsd_telink_stager_app.c|glsd_telink_disabled_feature_glue.c) defines+=("${telink_first_defines[@]}") ;;
  esac
  echo "[TC32] $name"
  "$TC32_CC" "${cflags[@]}" "${defines[@]}" "${includes[@]}" -I"$SRC" -c "$src" -o "$obj"
  sha256sum "$obj" | tee -a "$OUT_DIR/build-manifest.txt"
  if command -v "$TC32_NM" >/dev/null 2>&1; then
    echo "UNDEFINED_SYMBOLS $name" >> "$OUT_DIR/build-manifest.txt"
    "$TC32_NM" -u "$obj" >> "$OUT_DIR/build-manifest.txt" || true
  fi
done

echo "GLSD_TC32_OBJECT_COMPILE=PASS_6_OF_6" | tee -a "$OUT_DIR/build-manifest.txt"
echo "Objects and manifest: $OUT_DIR"
echo "STOP: this harness does not establish production geometry, board wiring, OTA, or deployment safety."
