#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Geometry of the spot-lock blackbox flash region as pure named constants.
 *
 * This header is framework-agnostic on purpose: it carries only compile-time
 * numbers (partition name, sector size, record size, capacity) so the pure ring
 * and record codecs can be host-tested with no ESP-IDF dependency. The flash
 * HAL (Unit 3) is the only place that turns these numbers into real
 * esp_partition calls; nothing here includes esp_ or driver headers.
 *
 * The region is a raw sector ring. Records are a fixed BLACKBOX_RECORD_SIZE so a
 * monotonic sequence number maps directly to a slot (seq modulo capacity) and a
 * byte offset (slot * record size). RECORD_SIZE divides SECTOR_SIZE evenly, so a
 * record never straddles a 4 KB sector boundary and a sector erase never splits
 * one record.
 */

/* Partition name, matching the `spotlog` row appended to partitions.csv. */
#define BLACKBOX_PARTITION_NAME "spotlog"

/* Custom data subtype for the `spotlog` partition (application-defined range). */
#define BLACKBOX_PARTITION_SUBTYPE 0x40U

/* NOR flash erase granularity: one 4 KB sector. */
#define BLACKBOX_SECTOR_SIZE 4096U

/* Fixed on-flash size of one record (session header or sample). Chosen to
 * divide SECTOR_SIZE evenly (64 records per sector) so records never straddle a
 * sector boundary. */
#define BLACKBOX_RECORD_SIZE 64U

/* Total region size reserved on flash: 4 MiB (matches the partitions.csv size).
 * ~9 h of continuous 2 Hz logging; with adaptive-rate recording (dense on
 * events, sparse when idle) this spans many outings. Sits in otherwise-unused
 * flash (16 MiB chip, <3 MiB used elsewhere). */
#define BLACKBOX_REGION_SIZE 0x400000U

/* Records per erasable sector: 4096 / 64 = 64. */
#define BLACKBOX_RECORDS_PER_SECTOR (BLACKBOX_SECTOR_SIZE / BLACKBOX_RECORD_SIZE)

/* Total record capacity of the ring: 1 MiB / 64 = 16384 records. */
#define BLACKBOX_CAPACITY_RECORDS (BLACKBOX_REGION_SIZE / BLACKBOX_RECORD_SIZE)

/* Number of erasable sectors in the region: 1 MiB / 4 KiB = 256. */
#define BLACKBOX_SECTOR_COUNT (BLACKBOX_REGION_SIZE / BLACKBOX_SECTOR_SIZE)

#ifdef __cplusplus
}
#endif
