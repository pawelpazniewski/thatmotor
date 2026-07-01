#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Flash HAL for the spot-lock blackbox: a thin adapter over esp_partition on the
 * `spotlog` partition. It turns the pure ring index math (blackbox_ring) and the
 * pure record codec (blackbox_record) into real erase/write/read calls on the
 * raw sector ring. It owns nothing but the partition handle and the monotonic
 * write cursor; all encoding/decoding and slot arithmetic stay in the pure core.
 *
 * Mirrors the nvs_store adapter style: map esp_err_t onto a domain status enum,
 * keep the decision logic out of the HAL.
 *
 * Records are appended with a monotonic sequence number that starts at 0 on
 * blackbox_init and advances per append, wrapping the ring (newest overwrites
 * oldest) via the pure blackbox_ring math. The cursor is in RAM: a reboot
 * restarts the ring at slot 0. Reads of prior data before overwrite still work.
 */

/** Domain status for blackbox flash operations (maps esp_err_t). */
typedef enum {
    BLACKBOX_OK = 0,          /* operation succeeded */
    BLACKBOX_ERR_NOT_FOUND,   /* `spotlog` partition not present in the table */
    BLACKBOX_ERR_STATE,       /* called before a successful blackbox_init */
    BLACKBOX_ERR_ARG,         /* NULL / wrong-length argument */
    BLACKBOX_ERR_IO,          /* underlying flash erase/write/read failed */
} blackbox_status;

/**
 * Callback invoked once per physical slot by blackbox_read_all, in ascending
 * slot order. Receives the raw BLACKBOX_RECORD_SIZE bytes; the caller classifies
 * / decodes them with the pure codec (the HAL stays codec-agnostic).
 *
 * @param record  Raw slot bytes (BLACKBOX_RECORD_SIZE long).
 * @param len     Number of bytes (always BLACKBOX_RECORD_SIZE).
 * @param ctx     Opaque caller context passed through from blackbox_read_all.
 */
typedef void (*blackbox_record_cb)(const uint8_t *record, size_t len, void *ctx);

/**
 * Locate the `spotlog` partition and reset the write cursor to slot 0. Must be
 * called once before append/read.
 *
 * @return BLACKBOX_OK, or BLACKBOX_ERR_NOT_FOUND if the partition is missing.
 */
blackbox_status blackbox_init(void);

/**
 * Append one fixed-size record to the ring: erase the sector first if this write
 * starts a new sector (NOR cannot rewrite in place), write the record at its
 * slot offset, then advance the cursor. Wrap-safe: past capacity the newest
 * overwrites the oldest.
 *
 * @param record  Encoded record bytes (must be non-NULL).
 * @param len     Must equal BLACKBOX_RECORD_SIZE.
 * @return BLACKBOX_OK, or ERR_ARG / ERR_STATE / ERR_IO.
 */
blackbox_status blackbox_append(const uint8_t *record, size_t len);

/**
 * Read every physical slot of the region in ascending order, invoking `cb` with
 * the raw bytes of each. Ordering by session/sequence is a decode-time concern
 * for the caller; the HAL only streams raw slots.
 *
 * @param cb   Per-slot callback (must be non-NULL).
 * @param ctx  Opaque context forwarded to `cb`.
 * @return BLACKBOX_OK, or ERR_ARG / ERR_STATE / ERR_IO.
 */
blackbox_status blackbox_read_all(blackbox_record_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif
