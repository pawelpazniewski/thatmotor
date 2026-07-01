#include "blackbox.h"

#include "blackbox_region.h"
#include "blackbox_ring.h"
#include "esp_partition.h"

/* Partition handle and the monotonic write cursor, owned by this HAL. Single
 * writer = the recorder task, so no lock is needed here. */
static const esp_partition_t *s_part;
static uint32_t s_seq;

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

blackbox_status blackbox_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      ESP_PARTITION_SUBTYPE_ANY,
                                      BLACKBOX_PARTITION_NAME);
    if (s_part == NULL) {
        return BLACKBOX_ERR_NOT_FOUND;
    }
    s_seq = 0U;
    return BLACKBOX_OK;
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
