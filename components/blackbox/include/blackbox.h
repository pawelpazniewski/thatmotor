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
 * Records are appended with a monotonic sequence number that advances per append,
 * wrapping the ring (newest overwrites oldest) via the pure blackbox_ring math.
 * The cursor lives in RAM, so blackbox_init scans the region on start and resumes
 * it (and the session id counter) past the last valid record (blackbox_resume):
 * a reboot or brownout between outings keeps recording monotonic, without reusing
 * a session id or overwriting the previous outing from slot 0.
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
 * Locate the `spotlog` partition and resume the write cursor past the last valid
 * record on flash (blackbox_resume), so a reboot continues the ring instead of
 * restarting it at slot 0. Also recovers the highest existing session id (see
 * blackbox_resume_session_seq). Must be called once before append/read.
 *
 * @return BLACKBOX_OK, BLACKBOX_ERR_NOT_FOUND if the partition is missing, or
 *         BLACKBOX_ERR_IO if the resume scan could not read the region.
 */
blackbox_status blackbox_init(void);

/**
 * Highest session id found on flash by the last blackbox_init scan (0 when the
 * region held no session header). The recorder seeds its session counter from
 * this so the next session id is highest + 1 — never a reused / colliding id
 * after a reboot. Valid only after blackbox_init.
 *
 * @return Highest existing session_seq, or 0 if none / before init.
 */
uint32_t blackbox_resume_session_seq(void);

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
