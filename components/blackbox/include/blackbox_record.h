#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) codec for spot-lock blackbox records. Two record
 * kinds share one fixed BLACKBOX_RECORD_SIZE slot so the ring can address them
 * by a single monotonic sequence number:
 *
 *   - a session header (written on the OFF -> non-OFF edge), carrying the
 *     session id, target position and the active regulator settings, and
 *   - a sample (written while spot-lock runs), carrying the per-cycle telemetry.
 *
 * On-flash layout of every record (deterministic, little-endian, zero-padded):
 *
 *   [ magic  : u16 ]  BLACKBOX_RECORD_MAGIC, distinguishes a written record
 *                     from an erased 0xFF slot (magic can never be 0xFFFF)
 *   [ schema : u8  ]  BLACKBOX_RECORD_SCHEMA
 *   [ type   : u8  ]  blackbox_record_type
 *   [ payload...   ]  type-specific fields in declared order
 *   [ zero pad...  ]  up to BLACKBOX_RECORD_SIZE - 4
 *   [ crc32  : u32 ]  IEEE 802.3 CRC32 over every preceding byte (incl. pad)
 *
 * No ESP-IDF dependency (no esp_ or driver includes), so the whole codec is
 * host-testable. Mirrors the settings blob_codec style (versioned + CRC) and the
 * resolve_provenance failure taxonomy (empty / corrupt / schema / valid).
 */

/* Wire magic: chosen so an erased flash slot (0xFFFF) is never mistaken for a
 * record, and a zeroed slot (0x0000) is not either. */
#define BLACKBOX_RECORD_MAGIC 0xB10CU

/* Wire schema version of the record layout. Bump on any field/layout change. */
#define BLACKBOX_RECORD_SCHEMA 1U

/* Sample flag bits packed into the sample `flags` byte. */
#define BLACKBOX_FLAG_GPS_FIX 0x01U
#define BLACKBOX_FLAG_IMU_OK 0x02U

/** Record discriminator stored in the `type` framing byte. */
typedef enum {
    BLACKBOX_TYPE_SAMPLE = 1, /* per-cycle spot-lock telemetry sample */
    BLACKBOX_TYPE_HEADER = 2, /* session header (id + target + settings) */
} blackbox_record_type;

/**
 * Decode / classify outcome. Distinguishes the failure modes so the reader can
 * tell an erased slot (EMPTY, ring boundary) from corruption (CRC) without
 * confusing either for valid data.
 */
typedef enum {
    BLACKBOX_REC_OK = 0,          /* valid record, *out written */
    BLACKBOX_REC_EMPTY = 1,       /* erased slot (all 0xFF) -> not data */
    BLACKBOX_REC_ERR_MAGIC = 2,   /* magic mismatch (garbage / partial write) */
    BLACKBOX_REC_ERR_CRC = 3,     /* trailing CRC32 mismatch (corrupt) */
    BLACKBOX_REC_ERR_SCHEMA = 4,  /* schema != BLACKBOX_RECORD_SCHEMA */
    BLACKBOX_REC_ERR_TYPE = 5,    /* type byte not the one requested */
    BLACKBOX_REC_ERR_LENGTH = 6,  /* buffer length != BLACKBOX_RECORD_SIZE */
    BLACKBOX_REC_ERR_ARG = 7,     /* NULL pointer argument */
} blackbox_record_result;

/** Per-cycle spot-lock telemetry sample. */
typedef struct {
    uint32_t t_ms;             /* milliseconds since session start */
    uint8_t substate;          /* spot_lock_substate (0=off,1=active,2=paused) */
    uint16_t err_m;            /* position error to target, metres */
    uint16_t bearing_deg10;    /* bearing to target, degrees * 10 */
    uint16_t heading_deg10;    /* boat heading, degrees * 10 */
    uint16_t servo_us;         /* commanded servo pulse, microseconds */
    uint16_t esc_us;           /* commanded ESC pulse, microseconds */
    uint16_t ch1_us;           /* steering stick raw pulse, microseconds */
    uint16_t ch2_us;           /* throttle stick raw pulse, microseconds */
    int32_t lat_e7;            /* latitude, degrees * 1e7 (negative for S) */
    int32_t lon_e7;            /* longitude, degrees * 1e7 (negative for W) */
    uint8_t sats;              /* satellites used in the fix */
    uint16_t speed_cms;        /* ground speed, cm/s */
    bool gps_fix;              /* GPS has a usable fix (packed into flags) */
    bool imu_ok;               /* fresh IMU heading is flowing (packed) */
} blackbox_sample;

/** Session header: id, hold target and the active regulator settings. */
typedef struct {
    uint32_t session_seq;      /* monotonic session id */
    int32_t target_lat_e7;     /* hold target latitude, degrees * 1e7 */
    int32_t target_lon_e7;     /* hold target longitude, degrees * 1e7 */
    uint16_t deadband_m;       /* spot_lock_deadband_m at session start */
    uint16_t max_throttle_pct; /* spot_lock_max_throttle_pct */
    uint16_t throttle_gain;    /* spot_lock_throttle_gain */
    uint16_t servo_gain;       /* spot_lock_servo_gain */
    uint32_t start_ms;         /* wall/uptime clock at session start, ms */
} blackbox_session_header;

/**
 * IEEE 802.3 (zlib) CRC32 of a byte range. Reflected, poly 0xEDB88320, init and
 * final XOR 0xFFFFFFFF. Exposed so tests can re-stamp a mutated record.
 */
uint32_t blackbox_record_crc32(const uint8_t *data, size_t len);

/**
 * Serialise a sample into a BLACKBOX_RECORD_SIZE buffer (framed, padded, CRC).
 *
 * @param sample   Source sample (must be non-NULL).
 * @param out      Destination buffer (must be non-NULL).
 * @param out_len  Capacity of out; must be >= BLACKBOX_RECORD_SIZE.
 * @return BLACKBOX_REC_OK, or ERR_ARG / ERR_LENGTH.
 */
blackbox_record_result blackbox_record_encode_sample(
    const blackbox_sample *sample, uint8_t *out, size_t out_len);

/**
 * Serialise a session header into a BLACKBOX_RECORD_SIZE buffer.
 *
 * @param header   Source header (must be non-NULL).
 * @param out      Destination buffer (must be non-NULL).
 * @param out_len  Capacity of out; must be >= BLACKBOX_RECORD_SIZE.
 * @return BLACKBOX_REC_OK, or ERR_ARG / ERR_LENGTH.
 */
blackbox_record_result blackbox_record_encode_header(
    const blackbox_session_header *header, uint8_t *out, size_t out_len);

/**
 * Classify a slot: validate framing (length, empty, magic, CRC, schema) and
 * report the record type. Does not decode the payload.
 *
 * @param buf       Slot bytes (must be non-NULL).
 * @param len       Number of bytes (must be BLACKBOX_RECORD_SIZE).
 * @param out_type  Written with the record type on BLACKBOX_REC_OK.
 * @return BLACKBOX_REC_OK, BLACKBOX_REC_EMPTY, or a framing error.
 */
blackbox_record_result blackbox_record_classify(const uint8_t *buf, size_t len,
                                                blackbox_record_type *out_type);

/**
 * Validate framing and decode a sample. Rejects with ERR_TYPE if the slot is a
 * header. *out is written only on BLACKBOX_REC_OK.
 */
blackbox_record_result blackbox_record_decode_sample(const uint8_t *buf,
                                                     size_t len,
                                                     blackbox_sample *out);

/**
 * Validate framing and decode a session header. Rejects with ERR_TYPE if the
 * slot is a sample. *out is written only on BLACKBOX_REC_OK.
 */
blackbox_record_result blackbox_record_decode_header(
    const uint8_t *buf, size_t len, blackbox_session_header *out);

#ifdef __cplusplus
}
#endif
