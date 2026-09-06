#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/firmware/gl-sd-301p-ed"
FIXTURE="$SRC/telink_fixture"

: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT to the tl_zigbee_sdk directory}"
TC32_CC="${TC32_CC:-tc32-elf-gcc}"
TC32_NM="${TC32_NM:-tc32-elf-nm}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd-ed-tc32-objects}"
SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"

first_existing_dir() {
  local d
  for d in "$@"; do [[ -d "$d" ]] && { printf '%s\n' "$d"; return 0; }; done
  return 1
}

SAMPLE_DIR="$(first_existing_dir "$SDK/apps/zigbee/sampleLight" "$SDK/apps/sampleLight")" || {
  echo "ERROR: no Telink sampleLight directory" >&2; exit 2;
}
APP_COMMON_DIR="$(first_existing_dir "$SDK/apps/common")" || {
  echo "ERROR: no Telink apps/common directory" >&2; exit 2;
}

for f in "$SDK/proj/tl_common.h" "$FIXTURE/app_cfg.h" "$FIXTURE/stack_cfg.h" \
         "$FIXTURE/version_cfg.h" "$APP_COMMON_DIR/comm_cfg.h" \
         "$SAMPLE_DIR/board_8258_dongle.h"; do
  [[ -f "$f" ]] || { echo "ERROR: required file missing: $f" >&2; exit 2; }
done
command -v "$TC32_CC" >/dev/null 2>&1 || { echo "ERROR: TC32 compiler not found: $TC32_CC" >&2; exit 2; }

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.o "$OUT_DIR"/build-manifest.txt "$OUT_DIR"/role_assert.c

base_defines=(
  -DGLSD_TELINK_SDK
  -DMCU_CORE_8258=1
  -DEND_DEVICE=1
  -DMCU_STARTUP_8258=1
)
telink_first_defines=(-D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__)

include_roots=("$SDK/proj" "$SDK/platform" "$APP_COMMON_DIR" "$SAMPLE_DIR")
[[ -d "$SDK/stack" ]] && include_roots+=("$SDK/stack")
[[ -d "$SDK/zigbee" ]] && include_roots+=("$SDK/zigbee")
includes=(-I"$FIXTURE" -I"$SAMPLE_DIR" -I"$APP_COMMON_DIR" -I"$SDK/proj")
while IFS= read -r -d '' d; do includes+=("-I$d"); done < <(find "${include_roots[@]}" -type d -print0 | sort -zu)

cflags=(-std=gnu99 -Wall -Wextra -ffunction-sections -fdata-sections -fshort-enums -funsigned-char -Os)
sources=(
  glsd_ed_core.c
  glsd_power_stage_stub.c
  glsd_telink_ed_app.c
)

{
  echo "PRODUCT_FIRMWARE=GL-SD-301P-ED"
  echo "OBJECT_ONLY=YES"
  echo "DEPLOYABLE=NO_POWER_STAGE_STUB"
  echo "ZIGBEE_ROLE=END_DEVICE"
  echo "ZB_MAC_RX_ON_WHEN_IDLE=1"
  echo "PM_ENABLE=0"
  echo "ENDPOINT=11"
  echo "SDK_ROOT=$SDK"
  git -C "$SDK" rev-parse HEAD 2>/dev/null | sed 's/^/SDK_GIT_HEAD=/' || true
  echo "COMPILER=$TC32_CC"
  "$TC32_CC" --version | head -n 1 | sed 's/^/COMPILER_VERSION=/'
  printf 'BASE_DEFINES='; printf '%q ' "${base_defines[@]}"; echo
} | tee "$OUT_DIR/build-manifest.txt"

for name in "${sources[@]}"; do
  src="$SRC/$name"
  obj="$OUT_DIR/${name%.c}.o"
  defines=("${base_defines[@]}")
  if [[ "$name" == glsd_telink_ed_app.c ]]; then
    defines+=("${telink_first_defines[@]}")
  fi
  echo "[TC32] $name"
  "$TC32_CC" "${cflags[@]}" "${defines[@]}" "${includes[@]}" -I"$SRC" -c "$src" -o "$obj"
  sha256sum "$obj" | tee -a "$OUT_DIR/build-manifest.txt"
  if command -v "$TC32_NM" >/dev/null 2>&1; then
    echo "UNDEFINED_SYMBOLS $name" >> "$OUT_DIR/build-manifest.txt"
    "$TC32_NM" -u "$obj" >> "$OUT_DIR/build-manifest.txt" || true
  fi
done

# Compile a dedicated translation unit through the exact same pinned SDK headers.
# This proves the effective build configuration, instead of relying on source-path
# greps that vary between SDK packaging revisions.
cat > "$OUT_DIR/role_assert.c" <<'EOF'
#include "app_cfg.h"
#if !defined(ZB_ED_ROLE) || (ZB_ED_ROLE != 1)
#error "GL-SD product firmware must compile as Zigbee End Device"
#endif
#if defined(ZB_ROUTER_ROLE) && (ZB_ROUTER_ROLE != 0)
#error "GL-SD product firmware must not compile as Router"
#endif
#if !defined(ZB_MAC_RX_ON_WHEN_IDLE) || (ZB_MAC_RX_ON_WHEN_IDLE != 1)
#error "GL-SD mains End Device must keep MAC RX on while idle"
#endif
#if PM_ENABLE != 0
#error "GL-SD mains End Device must not enter PM sleep"
#endif
int glsd_ed_role_assert_translation_unit(void) { return 0; }
EOF

"$TC32_CC" "${cflags[@]}" "${base_defines[@]}" "${telink_first_defines[@]}" \
  "${includes[@]}" -I"$SRC" -c "$OUT_DIR/role_assert.c" -o "$OUT_DIR/role_assert.o"
echo "GLSD_ED_EFFECTIVE_ROLE_ASSERT=PASS" | tee -a "$OUT_DIR/build-manifest.txt"

# Fail if the application object itself reaches router-only primitives. End-device
# joining/rejoining and standard OTA client calls are expected.
if command -v "$TC32_NM" >/dev/null 2>&1; then
  ! "$TC32_NM" -u "$OUT_DIR/glsd_telink_ed_app.o" | grep -E '(^| )((zb_nwkFormation|bdb_networkFormationStart|zb_setPermitJoin))$'
fi

echo "GLSD_ED_TC32_OBJECT_COMPILE=PASS_3_OF_3" | tee -a "$OUT_DIR/build-manifest.txt"
echo "Objects and manifest: $OUT_DIR"
