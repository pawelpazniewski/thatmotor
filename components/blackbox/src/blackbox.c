#include "blackbox.h"

#include "blackbox_record.h"
#include "blackbox_region.h"
#include "blackbox_resume.h"
#include "blackbox_ring.h"
#include "esp_partition.h"

/* Partition handle and the monotonic write cursor, owned by this HAL. Single
 * writer = the recorder task, so no lock is needed here. */
static const esp_partition_t *s_part;
static uint32_t s_seq;

/* Highest session id recovered by the last init scan; the recorder seeds its
 * session counter from this so a post-reboot session never reuses an id. */
static uint32_t s_resume_session_seq;

/* Sector-sized scratch for full-region scans (resume + dump). Reading a whole
 * 4 KB sector per esp_partition_read (64 records) instead of one 64-byte record
 * cuts a 4 MiB scan from ~65k reads to ~1k -- keeps boot latency low. Static (not
 * stack) because the main task stack is small; safe because the two scanners
 * (init resume, on-demand dump) never run concurrently (single-writer HAL). */
static uint8_t s_scan_sector[BLACKBOX_SECTOR_SIZE];

/* Map a raw esp_partition error onto the domain status. Bad-argument/size
 * failures are argument errors; everything else is treated as flash I/O. */
static blackbox_status map_err(esp_err_t err)
{
    if (err == ESP_OK) {
        return BLACKBOX_OK;
    }
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE) {
        return BLACKBOX_ERR_ARG;
    }
    return BLACKBOX_ERR_IO;
}

/* Classify one raw slot for the resume scan: is it a valid record, is it a
 * header, and (if a header) what session id does it carry. Decoding stays in the
 * pure codec; this only forwards what the fold needs. */
static void scan_slot(const uint8_t *record, bool *is_valid, bool *is_header,
                      uint32_t *session_seq)
{
    blackbox_record_type type;
    blackbox_record_result classified =
        blackbox_record_classify(record, BLACKBOX_RECORD_SIZE, &type);
    *is_valid = (classified == BLACKBOX_REC_OK);
    *is_header = *is_valid && (type == BLACKBOX_TYPE_HEADER);
    *session_seq = 0U;
    if (!*is_header) {
        return;
    }
    blackbox_session_header header;
    if (blackbox_record_decode_header(record, BLACKBOX_RECORD_SIZE, &header) ==
        BLACKBOX_REC_OK) {
        *session_seq = header.session_seq;
    }
}

/* Per-slot visitor for a full-region scan. Receives the ascending slot index and
 * a pointer to that slot's BLACKBOX_RECORD_SIZE bytes inside the sector buffer. */
typedef void (*blackbox_slot_fn)(uint32_t slot, const uint8_t *record, void *ctx);

/* Walk every slot of the region in ascending order, one sector-sized read at a
 * time, invoking fn per slot. The single point where the whole region is read. */
static blackbox_status scan_region(blackbox_slot_fn fn, void *ctx)
{
    for (uint32_t sector = 0U; sector < BLACKBOX_SECTOR_COUNT; ++sector) {
        uint32_t base = sector * BLACKBOX_SECTOR_SIZE;
        esp_err_t read = esp_partition_read(s_part, base, s_scan_sector,
                                            BLACKBOX_SECTOR_SIZE);
        if (read != ESP_OK) {
            return map_err(read);
        }
        for (uint32_t i = 0U; i < BLACKBOX_RECORDS_PER_SECTOR; ++i) {
            uint32_t slot = sector * BLACKBOX_RECORDS_PER_SECTOR + i;
            fn(slot, s_scan_sector + i * BLACKBOX_RECORD_SIZE, ctx);
        }
    }
    return BLACKBOX_OK;
}

/* scan_region visitor: fold one slot into the resume scan aggregate. */
static void resume_fold_slot(uint32_t slot, const uint8_t *record, void *ctx)
{
    blackbox_resume_scan *scan = (blackbox_resume_scan *)ctx;
    bool is_valid;
    bool is_header;
    uint32_t session_seq;
    scan_slot(record, &is_valid, &is_header, &session_seq);
    blackbox_resume_scan_slot(scan, slot, is_valid, is_header, session_seq);
}

/* Scan the whole region once and fold it into the resume seeds: where to place
 * the write cursor and which session id to continue from. Keeps the raw reads in
 * the HAL and the seeding decision in the pure core. */
static blackbox_status resume_from_flash(void)
{
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);

    blackbox_status status = scan_region(resume_fold_slot, &scan);
    if (status != BLACKBOX_OK) {
        return status;
    }

    blackbox_resume_seed seed = blackbox_resume_decide(&scan);
    s_seq = seed.cursor;
    s_resume_session_seq = seed.session_seq;
    return BLACKBOX_OK;
}

blackbox_status blackbox_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      ESP_PARTITION_SUBTYPE_ANY,
                                      BLACKBOX_PARTITION_NAME);
    if (s_part == NULL) {
        return BLACKBOX_ERR_NOT_FOUND;
    }
    return resume_from_flash();
}

uint32_t blackbox_resume_session_seq(void)
{
    return s_resume_session_seq;
}

blackbox_status blackbox_append(const uint8_t *record, size_t len)
{
    if (record == NULL || len != BLACKBOX_RECORD_SIZE) {
        return BLACKBOX_ERR_ARG;
    }
    if (s_part == NULL) {
        return BLACKBOX_ERR_STATE;
    }

    uint32_t offset = blackbox_ring_offset(s_seq, BLACKBOX_CAPACITY_RECORDS,
                                           BLACKBOX_RECORD_SIZE);

    /* Erase the sector when this write starts a new one; needs_erase is true
     * only at a sector's first slot, so `offset` is already sector-aligned. */
    if (blackbox_ring_needs_erase(s_seq, BLACKBOX_CAPACITY_RECORDS,
                                  BLACKBOX_RECORDS_PER_SECTOR)) {
        esp_err_t erased =
            esp_partition_erase_range(s_part, offset, BLACKBOX_SECTOR_SIZE);
        if (erased != ESP_OK) {
            return map_err(erased);
        }
    }

    esp_err_t written = esp_partition_write(s_part, offset, record, len);
    if (written != ESP_OK) {
        return map_err(written);
    }

    s_seq++;
    return BLACKBOX_OK;
}

/* scan_region visitor: forward each slot's raw bytes to the caller's callback. */
typedef struct {
    blackbox_record_cb cb;
    void *ctx;
} read_all_ctx;

static void read_all_slot(uint32_t slot, const uint8_t *record, void *ctx)
{
    (void)slot;
    read_all_ctx *rc = (read_all_ctx *)ctx;
    rc->cb(record, BLACKBOX_RECORD_SIZE, rc->ctx);
}

blackbox_status blackbox_read_all(blackbox_record_cb cb, void *ctx)
{
    if (cb == NULL) {
        return BLACKBOX_ERR_ARG;
    }
    if (s_part == NULL) {
        return BLACKBOX_ERR_STATE;
    }

    read_all_ctx rc = {.cb = cb, .ctx = ctx};
    return scan_region(read_all_slot, &rc);
}
