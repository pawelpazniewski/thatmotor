#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) index math for the spot-lock blackbox sector ring.
 *
 * The recorder assigns every record a monotonic 32-bit sequence number `seq`.
 * A record lives in slot `seq mod capacity`, so once `seq` reaches `capacity`
 * the ring wraps and the newest record overwrites the oldest — a fixed-size
 * circular log. All arithmetic is unsigned/modular so it is wrap-safe both at
 * the capacity boundary and at the u32 boundary of `seq` itself.
 *
 * No ESP-IDF dependency (no esp_ or driver includes): the flash HAL (Unit 3)
 * turns these slot indices and offsets into esp_partition erase/write calls.
 */

/**
 * Slot index for a sequence number: seq modulo capacity. Wrap-safe by
 * construction — seq beyond capacity reuses (overwrites) an earlier slot.
 *
 * @param seq       Monotonic record sequence number.
 * @param capacity  Ring capacity in records (must be > 0).
 * @return Slot index in [0, capacity).
 */
uint32_t blackbox_ring_slot(uint32_t seq, uint32_t capacity);

/**
 * Byte offset of a record within the region: slot * record_size.
 *
 * @param seq          Monotonic record sequence number.
 * @param capacity     Ring capacity in records (must be > 0).
 * @param record_size  Fixed on-flash record size in bytes.
 * @return Byte offset from the start of the region.
 */
uint32_t blackbox_ring_offset(uint32_t seq, uint32_t capacity,
                              uint32_t record_size);

/**
 * Whether the sector holding this record must be erased before the record is
 * written. NOR flash cannot rewrite in place, so a sector is erased when its
 * first record is about to be written, i.e. when the slot is the first slot of
 * a sector.
 *
 * @param seq                 Monotonic record sequence number.
 * @param capacity            Ring capacity in records (must be > 0).
 * @param records_per_sector  Records that fit in one erasable sector (> 0).
 * @return true when this write starts a new sector (erase first).
 */
bool blackbox_ring_needs_erase(uint32_t seq, uint32_t capacity,
                               uint32_t records_per_sector);

/**
 * Wrap-safe "is `a` newer than `b`" for two sequence numbers. Uses signed
 * modular distance so that a seq just past UINT32_MAX correctly ranks newer
 * than one just before it (a naive `a > b` gets this backwards at the wrap).
 *
 * @param a  Candidate newer sequence number.
 * @param b  Reference sequence number.
 * @return true when `a` follows `b` in monotonic order.
 */
bool blackbox_ring_seq_after(uint32_t a, uint32_t b);

/**
 * Oldest still-live sequence number given the newest one and how many records
 * are live. With `count` records ending at `newest`, the oldest is
 * `newest - count + 1` computed with wrap-safe modular subtraction.
 *
 * @param newest_seq  Sequence number of the newest record.
 * @param count       Number of live records (1..capacity).
 * @return Sequence number of the oldest live record.
 */
uint32_t blackbox_ring_oldest_seq(uint32_t newest_seq, uint32_t count);

#ifdef __cplusplus
}
#endif
