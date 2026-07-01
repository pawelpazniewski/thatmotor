#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) resume decision for the spot-lock blackbox.
 *
 * The write cursor and the session id counter live only in RAM, so a reboot or
 * a battery brownout between outings would otherwise restart both at zero: the
 * post-reboot session would reuse session_seq=1 and overwrite the ring from slot
 * 0, colliding with the still-valid records of the previous outing. read_all
 * then streams two different outings under one session id, interleaved in the
 * wrong order.
 *
 * On init the flash HAL scans the region physically (slot 0 upwards), classifies
 * each slot with the pure record codec, and folds the result through this module
 * to recover where to resume: the next session id (highest header id seen + 1 on
 * the next START) and the next write cursor (the slot right after the last valid
 * record, so new writes never land on top of old ones).
 *
 * No ESP-IDF dependency (no esp_ or driver includes), so the fold + decision are
 * host-testable with no hardware. The HAL does the raw reads and classification;
 * the seeding logic lives here.
 *
 * The fold is streaming (one slot at a time) on purpose: the region holds
 * BLACKBOX_CAPACITY_RECORDS slots, far too many to buffer as decoded structs.
 */

/**
 * Running scan aggregate, updated one slot at a time in ascending slot order.
 * Callers treat it as opaque state between blackbox_resume_scan_init and
 * blackbox_resume_decide; the fields are exposed only so it can live on the
 * stack.
 */
typedef struct {
    uint32_t highest_session_seq; /* max header session_seq seen (0 if none) */
    bool has_header;              /* any valid session header seen */
    uint32_t last_valid_slot;     /* highest slot holding a valid record */
    bool has_valid;               /* any valid (non-empty) record seen */
} blackbox_resume_scan;

/**
 * Resume seeds recovered from a full region scan: the values the recorder loads
 * into its RAM counters so recording continues monotonically after a reboot.
 */
typedef struct {
    uint32_t session_seq; /* seed for the session id counter: the next START
                           * yields session_seq + 1 (highest seen + 1) */
    uint32_t cursor;      /* seed for the write cursor: slot after the last
                           * valid record (0 when the region is empty) */
} blackbox_resume_seed;

/**
 * Reset a scan aggregate to the empty-region state (no records, cursor 0).
 *
 * @param scan  Aggregate to initialise (must be non-NULL).
 */
void blackbox_resume_scan_init(blackbox_resume_scan *scan);

/**
 * Fold one physical slot into the scan. Call once per slot in ascending order.
 *
 * @param scan         Aggregate to update (must be non-NULL).
 * @param slot         Physical slot index of this record.
 * @param is_valid     True if the slot decoded as a valid record (not erased /
 *                     corrupt); an invalid slot advances nothing.
 * @param is_header    True if the valid record is a session header.
 * @param session_seq  Header session id (used only when is_valid && is_header).
 */
void blackbox_resume_scan_slot(blackbox_resume_scan *scan, uint32_t slot,
                               bool is_valid, bool is_header,
                               uint32_t session_seq);

/**
 * Decide the resume seeds from a completed scan. An empty region yields
 * {session_seq=0, cursor=0} (a fresh start); otherwise the session seed is the
 * highest header id seen and the cursor is the slot after the last valid record.
 *
 * @param scan  Completed scan aggregate (must be non-NULL).
 * @return The seeds to load into the recorder's RAM counters.
 */
blackbox_resume_seed blackbox_resume_decide(const blackbox_resume_scan *scan);

#ifdef __cplusplus
}
#endif
