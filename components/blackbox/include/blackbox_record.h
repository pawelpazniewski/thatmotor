#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) codec for spot-lock blackbox records. Three record
 * kinds share one fixed BLACKBOX_RECORD_SIZE slot so the ring can address them
 * by a single monotonic sequence number:
 *
 *   - a session header (written on the OFF -> non-OFF edge), carrying the
 *     session id, target position and the active regulator settings,
 *   - a sample (written while spot-lock runs), carrying the per-cycle telemetry,
 *     and
 *   - an attempt (written on a CH3 entry edge that did NOT start a session),
 *     carrying which entry gate(s) were open/closed. A successful entry is
 *     already visible as a header+sample pair, so this exists purely to make a
 *     REJECTED entry visible too -- previously a failed CH3 press left no trace
 *     at all.
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
 *
 * BLACKBOX_TYPE_ATTEMPT is a new type value, not a layout change to HEADER or
 * SAMPLE, so it does NOT bump BLACKBOX_RECORD_SCHEMA: existing sessions already
 * on flash stay decodable after this firmware update. Only bump schema for a
 * layout change to an EXISTING type.
 */

/* Wire magic: chosen so an erased flash slot (0xFFFF) is never mistaken for a
 * record, and a zeroed slot (0x0000) is not either. */
#define BLACKBOX_RECORD_MAGIC 0xB10CU

/* Wire schema version of the record layout. Bump on any field/layout change.
 * v2 (rich logging): adds sm_state, source, end_reason, arm_reason, ch3/ch4,
 * imu_calib and extra flag bits to the sample; the CSV reader depends on it. */
#define BLACKBOX_RECORD_SCHEMA 2U

/* Sample flag bits packed into the sample `flags` byte. */
#define BLACKBOX_FLAG_GPS_FIX 0x01U
#define BLACKBOX_FLAG_IMU_OK 0x02U
#define BLACKBOX_FLAG_RC_VALID 0x04U   /* debounced RC validity this cycle */
#define BLACKBOX_FLAG_GPS_FRESH 0x08U  /* GPS freshness window still open */
#define BLACKBOX_FLAG_LINK_FRESH 0x10U /* app/comms link fresh (goto watchdog) */
#define BLACKBOX_FLAG_GOTO_OWNS 0x20U  /* SRC_GOTO owns the target this cycle */
#define BLACKBOX_FLAG_ARRIVED 0x40U    /* within the deadband (arrived) */

/* Attempt flag bits packed into the attempt `flags` byte. */
#define BLACKBOX_ATTEMPT_FLAG_OK 0x01U             /* entered HOLD */
#define BLACKBOX_ATTEMPT_FLAG_ARMED 0x02U          /* control state was ARMED */
#define BLACKBOX_ATTEMPT_FLAG_STICKS_NEUTRAL 0x04U /* both sticks near neutral */
#define BLACKBOX_ATTEMPT_FLAG_GPS_FRESH 0x08U      /* GPS freshness window open */
#define BLACKBOX_ATTEMPT_FLAG_GPS_FIX 0x10U        /* GPS had a usable fix */

/** Record discriminator stored in the `type` framing byte. */
typedef enum {
    BLACKBOX_TYPE_SAMPLE = 1,   /* per-cycle spot-lock telemetry sample */
    BLACKBOX_TYPE_HEADER = 2,   /* session header (id + target + settings) */
    BLACKBOX_TYPE_ATTEMPT = 3,  /* CH3 entry attempt that did not start a session */
} blackbox_record_type;

/**
 * Why the recorded session left non-OFF, stored in a sample's `end_reason`.
 * BLACKBOX_END_NONE on every normal in-session sample; a specific reason is set
 * on the tail samples the recorder writes just after spot-lock/goto drops to
 * OFF, so a drift after the operator takes over is attributable in the log.
 */
typedef enum {
    BLACKBOX_END_NONE = 0,        /* normal in-session sample (still holding) */
    BLACKBOX_END_DISARM = 1,      /* operator disarmed */
    BLACKBOX_END_STICK = 2,       /* manual stick override (either stick moved) */
    BLACKBOX_END_CH3 = 3,         /* CH3 preempt / spot-lock switch off */
    BLACKBOX_END_GOTO_CANCEL = 4, /* app cancelled goto */
    BLACKBOX_END_FAILSAFE = 5,    /* RC-loss failsafe */
    BLACKBOX_END_OTHER = 6,       /* dropped to OFF for an undetermined reason */
} blackbox_end_reason;

/** Which source owned the hold target for this sample. */
typedef enum {
    BLACKBOX_SRC_NONE = 0, /* nothing engaged */
    BLACKBOX_SRC_HOLD = 1, /* CH3 "here and now" hold (RC-owned) */
    BLACKBOX_SRC_GOTO = 2, /* app goto target */
} blackbox_source;

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
    uint8_t sm_state;          /* control state (sm_state enum: DISARMED/ARMED/…) */
    uint8_t source;            /* blackbox_source: who owns the target */
    uint8_t end_reason;        /* blackbox_end_reason; NONE while the session runs */
    uint8_t arm_reason;        /* sm_arm_reason: why arming is blocked */
    uint16_t err_m;            /* position error to target, metres */
    uint16_t bearing_deg10;    /* bearing to target, degrees * 10 */
    uint16_t heading_deg10;    /* boat heading, degrees * 10 */
    uint16_t servo_us;         /* commanded servo pulse, microseconds */
    uint16_t esc_us;           /* commanded ESC pulse, microseconds */
    uint16_t ch1_us;           /* steering stick raw pulse, microseconds */
    uint16_t ch2_us;           /* throttle stick raw pulse, microseconds */
    uint16_t ch3_us;           /* CH3 spot-lock switch raw pulse, microseconds */
    uint16_t ch4_us;           /* CH4 mode switch raw pulse, microseconds */
    int32_t lat_e7;            /* latitude, degrees * 1e7 (negative for S) */
    int32_t lon_e7;            /* longitude, degrees * 1e7 (negative for W) */
    uint8_t sats;              /* satellites used in the fix */
    uint16_t speed_cms;        /* ground speed, cm/s */
    uint8_t imu_calib;         /* compass calibration status, 0..3 */
    bool gps_fix;              /* GPS has a usable fix (packed into flags) */
    bool imu_ok;               /* fresh IMU heading is flowing (packed) */
    bool rc_valid;             /* debounced RC validity (packed) */
    bool gps_fresh;            /* GPS freshness window open (packed) */
    bool link_fresh;           /* app/comms link fresh (packed) */
    bool goto_owns;            /* SRC_GOTO owns the target this cycle (packed) */
    bool arrived;              /* within the deadband (packed) */
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
 * One CH3 entry attempt that did NOT open a session (a successful entry is
 * already covered by the header+sample pair written for it). Captures which of
 * the entry gates (armed / sticks neutral / GPS fresh / GPS fix) held at the
 * moment of the CH3 rising edge, so a rejected press is diagnosable after the
 * fact instead of leaving no trace.
 */
typedef struct {
    uint32_t attempt_seq;    /* monotonic attempt id (loop_state-owned) */
    uint32_t t_ms;           /* uptime clock at the attempt, ms */
    uint8_t sm_state;        /* control state (sm_state enum) at the attempt */
    bool ok;                 /* true: this attempt did enter HOLD */
    bool armed;              /* control state was ARMED at the attempt */
    bool sticks_neutral;     /* both sticks were near neutral at the attempt */
    bool gps_fresh;          /* GPS freshness window was open at the attempt */
    bool gps_fix;            /* GPS had a usable fix at the attempt */
    uint16_t ch1_us;         /* steering raw pulse at the attempt, us */
    uint16_t ch2_us;         /* throttle raw pulse at the attempt, us */
    uint16_t ch3_us;         /* CH3 raw pulse at the attempt, us */
} blackbox_attempt;

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
 * Serialise a rejected/accepted entry attempt into a BLACKBOX_RECORD_SIZE
 * buffer.
 *
 * @param attempt  Source attempt (must be non-NULL).
 * @param out      Destination buffer (must be non-NULL).
 * @param out_len  Capacity of out; must be >= BLACKBOX_RECORD_SIZE.
 * @return BLACKBOX_REC_OK, or ERR_ARG / ERR_LENGTH.
 */
blackbox_record_result blackbox_record_encode_attempt(
    const blackbox_attempt *attempt, uint8_t *out, size_t out_len);

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

/**
 * Validate framing and decode an attempt. Rejects with ERR_TYPE if the slot is
 * a header or sample. *out is written only on BLACKBOX_REC_OK.
 */
blackbox_record_result blackbox_record_decode_attempt(
    const uint8_t *buf, size_t len, blackbox_attempt *out);

#ifdef __cplusplus
}
#endif
