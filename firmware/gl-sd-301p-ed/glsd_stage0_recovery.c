#include "glsd_stage0_recovery.h"

#ifdef GLSD_TELINK_SDK

#include "drv_flash.h"
#include "drv_hw.h"
#include "ota.h"

#include <string.h>

/* Pinned TLSR8258 512-KiB layout from Telink V3.7.2.0. */
#define GLSD_BANK_A_BASE             0x00000u
#define GLSD_BANK_B_BASE             0x40000u
#define GLSD_APP_SLOT_SIZE           0x34000u
#define GLSD_FLASH_SECTOR_SIZE       0x1000u
#define GLSD_BOOT_FLAG_OFFSET        8u
#define GLSD_TELINK_START_WORD       0x544c4e4bu
#define GLSD_TELINK_DISABLED_WORD    0x544c4e00u
#define GLSD_TELINK_STAGED_WORD      0x544c4effu

/* Scratch lives at the tail of whichever bank Stage-0 actually occupies. */
#define GLSD_STAGE0_BACKUP_OFFSET    0x30000u
#define GLSD_STAGE0_JOURNAL_OFFSET   0x31000u

#define GLSD_STAGE0_JOURNAL_MAGIC    0x30425253u /* "SRB0" little-endian */
#define GLSD_STAGE0_JOURNAL_VERSION  2u
#define GLSD_CHUNK_SIZE              256u

typedef struct {
    u32 magic;
    u32 format_version;
    u32 self_base;
    u32 stock_base;
    u32 source_size;
    u32 source_crc;
    u32 backup_crc;
    u32 source_flag_word;
    u32 record_crc;
} glsd_stage0_journal_t;

static u8 g_sector[GLSD_FLASH_SECTOR_SIZE];
static u8 g_chunk[GLSD_CHUNK_SIZE];
static bool g_armed;
static u32 g_self_base;
static u32 g_stock_base;
static u32 g_backup_sector;
static u32 g_journal_sector;

static u32 glsd_read_u32(u32 address)
{
    u32 value = 0;
    flash_read(address, sizeof(value), (u8 *)&value);
    return value;
}

static u32 glsd_crc(const u8 *data, u32 length)
{
    return xcrc32((u8 *)data, length, 0xffffffffu);
}

static bool glsd_known_boot_word(u32 word)
{
    return word == GLSD_TELINK_START_WORD ||
           word == GLSD_TELINK_DISABLED_WORD ||
           word == GLSD_TELINK_STAGED_WORD;
}

static bool glsd_layout_init(void)
{
    u32 active = mcuBootAddrGet();

    if (active != GLSD_BANK_A_BASE && active != GLSD_BANK_B_BASE) {
        return false;
    }

    g_self_base = active;
    g_stock_base = (active == GLSD_BANK_A_BASE) ? GLSD_BANK_B_BASE : GLSD_BANK_A_BASE;
    g_backup_sector = g_self_base + GLSD_STAGE0_BACKUP_OFFSET;
    g_journal_sector = g_self_base + GLSD_STAGE0_JOURNAL_OFFSET;

    if (g_backup_sector + GLSD_FLASH_SECTOR_SIZE > g_self_base + GLSD_APP_SLOT_SIZE ||
        g_journal_sector + GLSD_FLASH_SECTOR_SIZE > g_self_base + GLSD_APP_SLOT_SIZE) {
        return false;
    }

    /* At initial entry Stage-0 must itself be a valid Telink image. */
    return glsd_read_u32(g_self_base + GLSD_BOOT_FLAG_OFFSET) == GLSD_TELINK_START_WORD;
}

static bool glsd_validate_stock_image(bool require_enabled,
                                      u32 expected_size,
                                      u32 expected_crc,
                                      u32 *out_size,
                                      u32 *out_crc)
{
    u32 image_size;
    u32 stored_crc;
    u32 current_crc = 0xffffffffu;
    u32 offset = 0;
    u32 remaining;
    u32 flag_word;

    flash_read(g_stock_base, GLSD_CHUNK_SIZE, g_chunk);
    memcpy(&image_size, g_chunk + 0x18u, sizeof(image_size));
    memcpy(&flag_word, g_chunk + GLSD_BOOT_FLAG_OFFSET, sizeof(flag_word));

    if (!glsd_known_boot_word(flag_word)) {
        return false;
    }
    if (require_enabled && flag_word != GLSD_TELINK_START_WORD) {
        return false;
    }
    if (image_size < GLSD_CHUNK_SIZE || image_size > GLSD_APP_SLOT_SIZE) {
        return false;
    }
    if (expected_size && image_size != expected_size) {
        return false;
    }

    flash_read(g_stock_base + image_size - sizeof(stored_crc),
               sizeof(stored_crc), (u8 *)&stored_crc);
    if (expected_crc && stored_crc != expected_crc) {
        return false;
    }

    remaining = image_size - sizeof(stored_crc);
    while (remaining) {
        u32 length = remaining > GLSD_CHUNK_SIZE ? GLSD_CHUNK_SIZE : remaining;
        flash_read(g_stock_base + offset, length, g_chunk);
        if (offset == 0u) {
            /* Telink validates CRC with only the one-byte boot marker normalized. */
            g_chunk[GLSD_BOOT_FLAG_OFFSET] = 0x4bu;
        }
        current_crc = xcrc32(g_chunk, length, current_crc);
        offset += length;
        remaining -= length;
        drv_wd_clear();
    }

    if (current_crc != stored_crc) {
        return false;
    }

    if (out_size) {
        *out_size = image_size;
    }
    if (out_crc) {
        *out_crc = stored_crc;
    }
    return true;
}

static bool glsd_verify_sector(u32 address, const u8 *expected)
{
    u32 offset;
    for (offset = 0; offset < GLSD_FLASH_SECTOR_SIZE; offset += GLSD_CHUNK_SIZE) {
        flash_read(address + offset, GLSD_CHUNK_SIZE, g_chunk);
        if (memcmp(g_chunk, expected + offset, GLSD_CHUNK_SIZE) != 0) {
            return false;
        }
        drv_wd_clear();
    }
    return true;
}

static bool glsd_program_sector(u32 address, const u8 *data)
{
    u32 offset;

    flash_unlock();
    flash_erase(address);
    for (offset = 0; offset < GLSD_FLASH_SECTOR_SIZE; offset += GLSD_CHUNK_SIZE) {
        if (!flash_writeWithCheck(address + offset, GLSD_CHUNK_SIZE,
                                  (u8 *)(data + offset))) {
            flash_lock();
            return false;
        }
        drv_wd_clear();
    }
    flash_lock();

    return glsd_verify_sector(address, data);
}

static u32 glsd_journal_record_crc(const glsd_stage0_journal_t *journal)
{
    return glsd_crc((const u8 *)journal,
                    (u32)(sizeof(*journal) - sizeof(journal->record_crc)));
}

static bool glsd_load_valid_journal(glsd_stage0_journal_t *journal)
{
    u32 offset;
    u32 crc = 0xffffffffu;

    flash_read(g_journal_sector, sizeof(*journal), (u8 *)journal);
    if (journal->magic != GLSD_STAGE0_JOURNAL_MAGIC ||
        journal->format_version != GLSD_STAGE0_JOURNAL_VERSION ||
        journal->self_base != g_self_base ||
        journal->stock_base != g_stock_base ||
        journal->source_size < GLSD_CHUNK_SIZE ||
        journal->source_size > GLSD_APP_SLOT_SIZE ||
        !glsd_known_boot_word(journal->source_flag_word) ||
        journal->record_crc != glsd_journal_record_crc(journal)) {
        return false;
    }

    for (offset = 0; offset < GLSD_FLASH_SECTOR_SIZE; offset += GLSD_CHUNK_SIZE) {
        flash_read(g_backup_sector + offset, GLSD_CHUNK_SIZE, g_chunk);
        crc = xcrc32(g_chunk, GLSD_CHUNK_SIZE, crc);
        drv_wd_clear();
    }
    return crc == journal->backup_crc;
}

static bool glsd_create_backup_and_journal(u32 source_size,
                                           u32 source_crc,
                                           u32 source_flag)
{
    glsd_stage0_journal_t journal;
    u32 offset;
    u32 backup_crc = 0xffffffffu;

    flash_read(g_stock_base, GLSD_FLASH_SECTOR_SIZE, g_sector);
    for (offset = 0; offset < GLSD_FLASH_SECTOR_SIZE; offset += GLSD_CHUNK_SIZE) {
        backup_crc = xcrc32(g_sector + offset, GLSD_CHUNK_SIZE, backup_crc);
    }

    if (!glsd_program_sector(g_backup_sector, g_sector)) {
        return false;
    }

    memset(&journal, 0xff, sizeof(journal));
    journal.magic = GLSD_STAGE0_JOURNAL_MAGIC;
    journal.format_version = GLSD_STAGE0_JOURNAL_VERSION;
    journal.self_base = g_self_base;
    journal.stock_base = g_stock_base;
    journal.source_size = source_size;
    journal.source_crc = source_crc;
    journal.backup_crc = backup_crc;
    journal.source_flag_word = source_flag;
    journal.record_crc = glsd_journal_record_crc(&journal);

    flash_unlock();
    flash_erase(g_journal_sector);
    if (!flash_writeWithCheck(g_journal_sector, sizeof(journal), (u8 *)&journal)) {
        flash_lock();
        return false;
    }
    flash_lock();

    memset(g_chunk, 0, sizeof(g_chunk));
    flash_read(g_journal_sector, sizeof(journal), g_chunk);
    return memcmp(g_chunk, &journal, sizeof(journal)) == 0;
}

static bool glsd_enable_stock_boot_flag(const glsd_stage0_journal_t *journal)
{
    u8 start = 0x4bu;

    if (glsd_read_u32(g_stock_base + GLSD_BOOT_FLAG_OFFSET) !=
        GLSD_TELINK_STAGED_WORD) {
        return false;
    }
    if (!glsd_validate_stock_image(false, journal->source_size,
                                   journal->source_crc, NULL, NULL)) {
        return false;
    }

    /* Atomic stock-boot commit: NOR-safe one-way 0xFF -> 0x4B. */
    flash_unlock();
    if (!flash_writeWithCheck(g_stock_base + GLSD_BOOT_FLAG_OFFSET, 1u, &start)) {
        flash_lock();
        return false;
    }
    flash_lock();

    return glsd_validate_stock_image(true, journal->source_size,
                                     journal->source_crc, NULL, NULL);
}

static bool glsd_restore_stock_sector(const glsd_stage0_journal_t *journal)
{
    u32 offset;
    u32 crc = 0xffffffffu;

    flash_read(g_backup_sector, GLSD_FLASH_SECTOR_SIZE, g_sector);
    for (offset = 0; offset < GLSD_FLASH_SECTOR_SIZE; offset += GLSD_CHUNK_SIZE) {
        crc = xcrc32(g_sector + offset, GLSD_CHUNK_SIZE, crc);
    }
    if (crc != journal->backup_crc) {
        return false;
    }

    /* During the full-sector rewrite stock remains deliberately non-bootable. */
    g_sector[GLSD_BOOT_FLAG_OFFSET] = 0xffu;
    if (!glsd_program_sector(g_stock_base, g_sector)) {
        return false;
    }

    if (glsd_read_u32(g_stock_base + GLSD_BOOT_FLAG_OFFSET) !=
        GLSD_TELINK_STAGED_WORD) {
        return false;
    }
    if (!glsd_validate_stock_image(false, journal->source_size,
                                   journal->source_crc, NULL, NULL)) {
        return false;
    }

    return glsd_enable_stock_boot_flag(journal);
}

static bool glsd_invalidate_stage0_bank(void)
{
    u8 zero = 0u;
    u32 word;

    if (glsd_read_u32(g_stock_base + GLSD_BOOT_FLAG_OFFSET) !=
        GLSD_TELINK_START_WORD) {
        return false;
    }
    if (glsd_read_u32(g_self_base + GLSD_BOOT_FLAG_OFFSET) !=
        GLSD_TELINK_START_WORD) {
        return false;
    }

    flash_unlock();
    if (!flash_writeWithCheck(g_self_base + GLSD_BOOT_FLAG_OFFSET, 1u, &zero)) {
        flash_lock();
        return false;
    }
    flash_lock();

    word = glsd_read_u32(g_self_base + GLSD_BOOT_FLAG_OFFSET);
    return word == GLSD_TELINK_DISABLED_WORD &&
           glsd_read_u32(g_stock_base + GLSD_BOOT_FLAG_OFFSET) ==
               GLSD_TELINK_START_WORD;
}

glsd_stage0_recovery_status_t glsd_stage0_recovery_prepare(void)
{
    glsd_stage0_journal_t journal;
    u32 source_size = 0;
    u32 source_crc = 0;
    u32 source_flag;
    bool journal_valid;

    g_armed = false;

#if defined(DUAL_MODE) && DUAL_MODE
    return GLSD_STAGE0_RECOVERY_ERR_BOOT_LAYOUT;
#endif

    if (!glsd_layout_init()) {
        return GLSD_STAGE0_RECOVERY_ERR_BOOT_LAYOUT;
    }

    journal_valid = glsd_load_valid_journal(&journal);
    if (!journal_valid) {
        source_flag = glsd_read_u32(g_stock_base + GLSD_BOOT_FLAG_OFFSET);
        if (!glsd_validate_stock_image(false, 0u, 0u,
                                       &source_size, &source_crc)) {
            return GLSD_STAGE0_RECOVERY_ERR_STOCK_INVALID;
        }
        if (!glsd_create_backup_and_journal(source_size, source_crc, source_flag)) {
            return GLSD_STAGE0_RECOVERY_ERR_BACKUP;
        }
        if (!glsd_load_valid_journal(&journal)) {
            return GLSD_STAGE0_RECOVERY_ERR_BACKUP;
        }
    }

    /* An interrupted earlier run may already have committed stock. */
    if (!glsd_validate_stock_image(true, journal.source_size,
                                   journal.source_crc, NULL, NULL)) {
        if (!glsd_restore_stock_sector(&journal)) {
            return GLSD_STAGE0_RECOVERY_ERR_RESTORE;
        }
    }

    /* Last destructive commit: verified stock is bootable before Stage-0 is
     * made non-bootable, independent of which physical bank holds either one. */
    if (!glsd_invalidate_stage0_bank()) {
        return GLSD_STAGE0_RECOVERY_ERR_SELF_INVALIDATE;
    }

    g_armed = true;
    return GLSD_STAGE0_RECOVERY_OK;
}

bool glsd_stage0_recovery_armed(void)
{
    return g_armed;
}

#endif /* GLSD_TELINK_SDK */
