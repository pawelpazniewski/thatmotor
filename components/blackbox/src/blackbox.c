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

/* Scan the whole region once and fold it into the resume seeds: where to place
 * the write cursor and which session id to continue from. Keeps the raw reads in
 * the HAL and the seeding decision in the pure core. */
static blackbox_status resume_from_flash(void)
{
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);

    uint8_t record[BLACKBOX_RECORD_SIZE];
    for (uint32_t slot = 0U; slot < BLACKBOX_CAPACITY_RECORDS; ++slot) {
        uint32_t offset = slot * BLACKBOX_RECORD_SIZE;
        esp_err_t read =
            esp_partition_read(s_part, offset, record, sizeof(record));
        if (read != ESP_OK) {
            return map_err(read);
        }
        bool is_valid;
        bool is_header;
        uint32_t session_seq;
        scan_slot(record, &is_valid, &is_header, &session_seq);
        blackbox_resume_scan_slot(&scan, slot, is_valid, is_header, session_seq);
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

blackbox_status blackbox_read_all(blackbox_record_cb cb, void *ctx)
{
    if (cb == NULL) {
        return BLACKBOX_ERR_ARG;
    }
    if (s_part == NULL) {
        return BLACKBOX_ERR_STATE;
    }

    uint8_t record[BLACKBOX_RECORD_SIZE];
    for (uint32_t slot = 0U; slot < BLACKBOX_CAPACITY_RECORDS; ++slot) {
        uint32_t offset = slot * BLACKBOX_RECORD_SIZE;
        esp_err_t read = esp_partition_read(s_part, offset, record,
                                            sizeof(record));
        if (read != ESP_OK) {
            return map_err(read);
        }
        cb(record, sizeof(record), ctx);
    }
    return BLACKBOX_OK;
}
