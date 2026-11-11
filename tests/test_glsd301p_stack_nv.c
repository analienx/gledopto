#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "glsd301p_stack_nv.h"

/* Fake flash: per-module "holds data" state plus one marker item that lives
 * in module 0 (erasing module 0 removes it). */
#define MODULES 7u

static bool g_dirty[MODULES];
static bool g_marker_present;
static uint8_t g_marker_store[GLSD301P_STACK_NV_MARKER_SIZE];
static unsigned g_erase_calls[MODULES];
static unsigned g_erase_order[MODULES * 4u];
static unsigned g_erase_total;
static unsigned g_marker_writes;
static int g_erase_fail_index;      /* erase reports failure */
static int g_erase_noop_index;      /* erase "succeeds" but leaves data */
static bool g_marker_write_fails;
static bool g_marker_write_corrupts;

static void reset_fake(void)
{
    memset(g_dirty, 0, sizeof(g_dirty));
    g_marker_present = false;
    memset(g_marker_store, 0, sizeof(g_marker_store));
    memset(g_erase_calls, 0, sizeof(g_erase_calls));
    memset(g_erase_order, 0, sizeof(g_erase_order));
    g_erase_total = 0u;
    g_marker_writes = 0u;
    g_erase_fail_index = -1;
    g_erase_noop_index = -1;
    g_marker_write_fails = false;
    g_marker_write_corrupts = false;
}

static bool fake_marker_read(uint8_t out[GLSD301P_STACK_NV_MARKER_SIZE])
{
    if (!g_marker_present) {
        return false;
    }
    memcpy(out, g_marker_store, sizeof(g_marker_store));
    return true;
}

static bool fake_marker_write(
    const uint8_t marker[GLSD301P_STACK_NV_MARKER_SIZE])
{
    g_marker_writes++;
    if (g_marker_write_fails) {
        return false;
    }
    memcpy(g_marker_store, marker, sizeof(g_marker_store));
    if (g_marker_write_corrupts) {
        g_marker_store[7] ^= 0xFFu;
    }
    g_marker_present = true;
    g_dirty[0] = true;
    return true;
}

static bool fake_module_erase(uint8_t index)
{
    assert(index < MODULES);
    g_erase_calls[index]++;
    g_erase_order[g_erase_total++] = index;
    if ((int)index == g_erase_fail_index) {
        return false;
    }
    if ((int)index == g_erase_noop_index) {
        return true;
    }
    g_dirty[index] = false;
    if (index == 0u) {
        g_marker_present = false;
    }
    return true;
}

static bool fake_module_blank(uint8_t index)
{
    assert(index < MODULES);
    return !g_dirty[index];
}

static const glsd301p_stack_nv_ops_t g_ops = {
    fake_marker_read,
    fake_marker_write,
    fake_module_erase,
    fake_module_blank,
};

static void make_vendor_leftovers(void)
{
    unsigned i;

    for (i = 0u; i < MODULES; i++) {
        g_dirty[i] = true;
    }
}

static void expect_all_blank_except_marker(void)
{
    unsigned i;

    for (i = 1u; i < MODULES; i++) {
        assert(!g_dirty[i]);
    }
}

static void test_vendor_leftovers_are_wiped_once(void)
{
    uint8_t expected[GLSD301P_STACK_NV_MARKER_SIZE];
    unsigned i;

    reset_fake();
    make_vendor_leftovers();
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_WIPED);
    expect_all_blank_except_marker();
    for (i = 0u; i < MODULES; i++) {
        assert(g_erase_calls[i] == 1u);
    }
    /* The marker's module is the commit point and goes first. */
    assert(g_erase_order[0] == 0u);
    glsd301p_stack_nv_marker(expected);
    assert(g_marker_present);
    assert(memcmp(g_marker_store, expected, sizeof(expected)) == 0);

    /* Second boot: stack state written since then must survive. */
    g_dirty[1] = true;
    g_dirty[6] = true;
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_CLEAN);
    assert(g_dirty[1] && g_dirty[6]);
    for (i = 0u; i < MODULES; i++) {
        assert(g_erase_calls[i] == 1u);
    }
    assert(g_marker_writes == 1u);
}

static void test_foreign_marker_is_not_trusted(void)
{
    reset_fake();
    make_vendor_leftovers();
    g_marker_present = true;
    memcpy(g_marker_store, "VENDOR!!", sizeof(g_marker_store));
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_WIPED);
    expect_all_blank_except_marker();
}

static void test_force_wipes_storage_that_already_matches(void)
{
    reset_fake();
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_WIPED);
    g_dirty[3] = true;
    g_dirty[5] = true;
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, true) ==
           GLSD301P_STACK_NV_WIPED);
    expect_all_blank_except_marker();
    assert(g_marker_present);
    assert(g_marker_writes == 2u);
}

static void test_failed_erase_writes_no_marker_and_retries(void)
{
    unsigned i;

    reset_fake();
    make_vendor_leftovers();
    g_erase_fail_index = 4;
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_ERASE_FAILED);
    assert(!g_marker_present);
    assert(g_marker_writes == 0u);
    /* Every module was still attempted. */
    for (i = 0u; i < MODULES; i++) {
        assert(g_erase_calls[i] == 1u);
    }
    /* Next boot, flash healthy again: full wipe, then marker. */
    g_erase_fail_index = -1;
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_WIPED);
    expect_all_blank_except_marker();
}

static void test_silent_erase_failure_is_caught_by_blank_check(void)
{
    reset_fake();
    make_vendor_leftovers();
    g_erase_noop_index = 2; /* e.g. write-protected region */
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_ERASE_FAILED);
    assert(!g_marker_present);
    assert(g_marker_writes == 0u);
}

static void test_marker_failure_repeats_wipe_next_boot(void)
{
    reset_fake();
    make_vendor_leftovers();
    g_marker_write_fails = true;
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_MARKER_FAILED);
    assert(!g_marker_present);

    reset_fake();
    make_vendor_leftovers();
    g_marker_write_corrupts = true;
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_MARKER_FAILED);
    /* A corrupt marker never reads as clean. */
    g_marker_write_corrupts = false;
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_WIPED);
}

static void test_interrupted_wipe_is_repeated(void)
{
    reset_fake();
    /* Power was cut after the marker's module was erased: leftovers in
     * the other modules, no marker. */
    make_vendor_leftovers();
    g_dirty[0] = false;
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, MODULES, false) ==
           GLSD301P_STACK_NV_WIPED);
    expect_all_blank_except_marker();
}

static void test_bad_arguments(void)
{
    glsd301p_stack_nv_ops_t ops = g_ops;

    reset_fake();
    assert(glsd301p_stack_nv_ensure_clean(NULL, MODULES, false) ==
           GLSD301P_STACK_NV_BAD_ARGS);
    assert(glsd301p_stack_nv_ensure_clean(&g_ops, 0u, false) ==
           GLSD301P_STACK_NV_BAD_ARGS);
    assert(glsd301p_stack_nv_ensure_clean(
               &g_ops, GLSD301P_STACK_NV_MAX_MODULES + 1u, false) ==
           GLSD301P_STACK_NV_BAD_ARGS);
    ops.module_blank = NULL;
    assert(glsd301p_stack_nv_ensure_clean(&ops, MODULES, false) ==
           GLSD301P_STACK_NV_BAD_ARGS);
    assert(g_erase_total == 0u);
    glsd301p_stack_nv_marker(NULL);
}

int main(void)
{
    test_vendor_leftovers_are_wiped_once();
    test_foreign_marker_is_not_trusted();
    test_force_wipes_storage_that_already_matches();
    test_failed_erase_writes_no_marker_and_retries();
    test_silent_erase_failure_is_caught_by_blank_check();
    test_marker_failure_repeats_wipe_next_boot();
    test_interrupted_wipe_is_repeated();
    test_bad_arguments();
    return 0;
}
