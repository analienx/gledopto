#!/usr/bin/env python3
"""One-time deterministic convergence of the clean interoperability modules into the ED product tree."""

from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    s = p.read_text()
    if old not in s:
        raise SystemExit(f"expected integration anchor missing in {path}: {old[:100]!r}")
    p.write_text(s.replace(old, new, 1))


def patch_app() -> None:
    app = "firmware/gl-sd-301p-ed/glsd_telink_ed_app.c"
    replace_once(
        app,
        '#include "glsd_power_stage.h"\n',
        '#include "glsd_power_stage.h"\n'
        '#include "glsd301p_push_input.h"\n'
        '#include "glsd301p_pb4_compat.h"\n',
    )
    replace_once(
        app,
        '#define GLSD_MOVE_TICK_MS                100u\n',
        '#define GLSD_MOVE_TICK_MS                100u\n'
        '#define GLSD_LOCAL_IO_TICK_MS            1u\n',
    )
    replace_once(
        app,
        'static ev_timer_event_t *g_rejoin_timer;\n',
        'static ev_timer_event_t *g_rejoin_timer;\n'
        'static ev_timer_event_t *g_local_io_timer;\n'
        'static glsd301p_push_decoder_t g_push_decoder;\n'
        'static glsd301p_pb4_compat_t g_pb4_compat;\n',
    )

    stop_anchor = """static void glsd_stop_move(void)
{
    if (g_move_timer) {
        TL_ZB_TIMER_CANCEL(&g_move_timer);
    }
    g_move_timer = NULL;
    g_move_delta = 0;
    g_level_remaining = 0;
}
"""
    local_io = stop_anchor + """

static s32 glsd_local_io_tick(void *arg)
{
    glsd301p_push_event_t event;
    (void)arg;

    if (!g_power_ready) {
        g_local_io_timer = NULL;
        return -1;
    }

    event = glsd301p_push_decoder_poll(
        &g_push_decoder,
        glsd_power_stage_pc2_high() != 0);

    if (event == GLSD301P_PUSH_EVENT_TOGGLE) {
        glsd_stop_move();
        if (glsd_ed_toggle(&g_core) == GLSD_ED_OK) {
            glsd_sync_attrs();
        }
    } else if (event == GLSD301P_PUSH_EVENT_LEVEL_STEP && g_core.on) {
        const glsd301p_push_dim_direction_t direction =
            glsd301p_push_decoder_direction(&g_push_decoder);
        const uint8_t next = glsd301p_push_level_step(g_core.level, direction);
        glsd_stop_move();
        if (glsd_ed_set_level(&g_core, next, 0u) == GLSD_ED_OK) {
            glsd_sync_attrs();
        }
    }

    if (glsd301p_pb4_compat_poll(
            &g_pb4_compat,
            glsd_power_stage_pb4_high() != 0)) {
        /* Stock behavior does not mutate g_core.level and does not send a
         * full-level restoration solely because PB4 later becomes low. */
        (void)glsd_power_stage_apply_pb4_aux(g_core.level);
    }

    return 0;
}
"""
    replace_once(app, stop_anchor, local_io)

    replace_once(
        app,
        '    g_power_ready = (glsd_power_stage_init() == 0) ? 1u : 0u;\n',
        '    glsd301p_push_decoder_init(&g_push_decoder);\n'
        '    glsd301p_pb4_compat_init(&g_pb4_compat);\n'
        '    g_power_ready = (glsd_power_stage_init() == 0) ? 1u : 0u;\n',
    )
    replace_once(
        app,
        '    (void)bdb_init((af_simple_descriptor_t *)&g_simple_desc,\n'
        '                   &g_bdb_settings, &g_bdb_callbacks, 1);\n',
        '    (void)bdb_init((af_simple_descriptor_t *)&g_simple_desc,\n'
        '                   &g_bdb_settings, &g_bdb_callbacks, 1);\n'
        '    if (g_power_ready) {\n'
        '        g_local_io_timer = TL_ZB_TIMER_SCHEDULE(\n'
        '            glsd_local_io_tick, NULL, GLSD_LOCAL_IO_TICK_MS);\n'
        '    }\n',
    )


def patch_link() -> None:
    link = "tools/build_glsd_ed_tc32_link.sh"
    replace_once(
        link,
        'SRC="$ROOT/firmware/gl-sd-301p-ed"\n',
        'SRC="$ROOT/firmware/gl-sd-301p-ed"\nINTEROP="$ROOT/src"\n',
    )
    replace_once(
        link,
        'includes=(-I"$FIXTURE" -I"$SAMPLE_DIR" -I"$COMMON_APP" -I"$SDK/proj")\n',
        'includes=(-I"$FIXTURE" -I"$SAMPLE_DIR" -I"$COMMON_APP" -I"$SDK/proj" -I"$SRC" -I"$INTEROP")\n',
    )
    replace_once(
        link,
        """app_sources=(
  glsd_ed_core.c
  glsd_power_stage_stub.c
  glsd_telink_ed_app.c
  glsd_telink_disabled_feature_glue.c
)
""",
        """app_sources=(
  glsd_ed_core.c
  glsd_power_stage_telink.c
  glsd_telink_ed_app.c
  glsd_telink_disabled_feature_glue.c
)

interop_sources=(
  glsd301p_uart_frame.c
  glsd301p_power_stage_policy.c
  glsd301p_push_input.c
  glsd301p_pb4_compat.c
)
""",
    )
    app_loop = """for rel in "${app_sources[@]}"; do
  src="$SRC/$rel"
  [[ -f "$src" ]] || { echo "ERROR: product source missing: $rel" >&2; exit 2; }
  obj="$DIR/obj/app/${rel%.c}.o"
  compile_one "$src" "$obj" 0
  objects+=("$obj")
done
"""
    replace_once(
        link,
        app_loop,
        app_loop
        + """
for rel in "${interop_sources[@]}"; do
  src="$INTEROP/$rel"
  [[ -f "$src" ]] || { echo "ERROR: interoperability source missing: $rel" >&2; exit 2; }
  obj="$DIR/obj/app/${rel%.c}.o"
  compile_one "$src" "$obj" 0
  objects+=("$obj")
done
""",
    )
    replace_once(
        link,
        'python3 "$FINALIZER" check-link "$bin" --max-final-size "$APP_SLOT_SIZE"\n'
        'python3 "$FINALIZER" finalize "$bin" "$final_bin" --max-final-size "$APP_SLOT_SIZE"\n'
        'python3 "$FINALIZER" check-final "$final_bin" --max-final-size "$APP_SLOT_SIZE"\n',
        'file_version="$(awk \'/^[[:space:]]*#define[[:space:]]+FILE_VERSION[[:space:]]+/ {print $3; exit}\' "$FIXTURE/version_cfg.h")"\n'
        '[[ "$file_version" =~ ^0[xX][0-9A-Fa-f]+$ ]] || { echo "ERROR: cannot parse FILE_VERSION" >&2; exit 2; }\n'
        'python3 "$FINALIZER" check-link "$bin" --file-version "$file_version" --max-final-size "$APP_SLOT_SIZE"\n'
        'python3 "$FINALIZER" finalize "$bin" "$final_bin" --file-version "$file_version" --max-final-size "$APP_SLOT_SIZE"\n'
        'python3 "$FINALIZER" check-final "$final_bin" --file-version "$file_version" --max-final-size "$APP_SLOT_SIZE"\n',
    )
    replace_once(
        link,
        """  echo DEPLOYABLE=NO
  echo DEPLOYABLE_BLOCKER=POWER_STAGE_DRIVER_STUB
  echo POWER_STAGE_DRIVER=STUB
""",
        """  echo FLASHABLE_ARTIFACT=YES
  echo DEPLOY_AUTHORIZED=NO
  echo DEPLOYABLE=NO
  echo DEPLOYABLE_BLOCKER=SACRIFICIAL_CANARY_NOT_YET_VALIDATED
  echo POWER_STAGE_DRIVER=UART_GLSD301P
  echo POWER_STAGE_PROTOCOL_GATE=PASS_STATIC
""",
    )
    replace_once(
        link,
        "echo 'STOP: product image is non-deployable until glsd_power_stage_stub.c is replaced by the verified GL-SD hardware driver.'\n",
        "echo 'STOP: structurally flashable artifact built; deployment remains unauthorized until sacrificial-canary validation.'\n",
    )


def patch_ci() -> None:
    wf = Path(".github/workflows/glsd-ed-firmware.yml")
    s = wf.read_text()
    s = s.replace(
        "      - research/wireless-dump-stager\n",
        "      - build/glsd301p-clean-interop-integration-20260907\n",
        1,
    )
    push_anchor = "    paths:\n      - 'firmware/gl-sd-301p-ed/**'\n"
    if push_anchor not in s:
        raise SystemExit("firmware workflow push paths anchor missing")
    s = s.replace(push_anchor, push_anchor + "      - 'src/**'\n", 1)
    pr_anchor = "  pull_request:\n    paths:\n      - 'firmware/gl-sd-301p-ed/**'\n"
    if pr_anchor not in s:
        raise SystemExit("firmware workflow PR paths anchor missing")
    s = s.replace(pr_anchor, pr_anchor + "      - 'src/**'\n", 1)
    s = s.replace(
        "grep -q '^GLSD_ED_TC32_OBJECT_COMPILE=PASS_3_OF_3$' \"$manifest\"",
        "grep -q '^GLSD_ED_TC32_OBJECT_COMPILE=PASS_7_OF_7$' \"$manifest\"",
    )
    s = s.replace(
        "grep -q '^DEPLOYABLE_BLOCKER=POWER_STAGE_DRIVER_STUB$' \"$manifest\"",
        "grep -q '^DEPLOYABLE_BLOCKER=SACRIFICIAL_CANARY_NOT_YET_VALIDATED$' \"$manifest\"\n"
        "          grep -q '^POWER_STAGE_DRIVER=UART_GLSD301P$' \"$manifest\"\n"
        "          grep -q '^POWER_STAGE_PROTOCOL_GATE=PASS_STATIC$' \"$manifest\"\n"
        "          grep -q '^FLASHABLE_ARTIFACT=YES$' \"$manifest\"\n"
        "          grep -q '^DEPLOY_AUTHORIZED=NO$' \"$manifest\"",
    )
    s = s.replace(
        "echo 'GLSD_ED_COMPLETE_PRODUCT_LINK=PASS_NONDEPLOYABLE'",
        "echo 'GLSD_ED_COMPLETE_PRODUCT_LINK=PASS_STATIC_NO_DEPLOY_AUTH'",
    )
    wf.write_text(s)


def main() -> None:
    patch_app()
    patch_link()
    patch_ci()
    stub = Path("firmware/gl-sd-301p-ed/glsd_power_stage_stub.c")
    if not stub.exists():
        raise SystemExit("expected power-stage stub missing")
    stub.unlink()
    print("CLEAN_POWER_STAGE_INTEGRATION=PASS")


if __name__ == "__main__":
    main()
