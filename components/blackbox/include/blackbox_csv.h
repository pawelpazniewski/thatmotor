#pragma once

#include <stddef.h>

#include "blackbox_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure (framework-agnostic) CSV formatter for a spot-lock blackbox dump. Turns a
 * decoded session header + sample pair into one flat, denormalised CSV line: the
 * session id and the active session settings are repeated on every sample row so
 * an offline parser (Claude) can group and analyse without a join. No ESP-IDF
 * dependency (no esp_ or driver includes), so the column contract is host-tested.
 *
 * The header line and the row column order are a STABLE CONTRACT: the parser on
 * the analysis side keys off them. Changing either is a schema change.
 *
 * Column order (header == row):
 *   session_id, t_ms, substate, sm_state, source, end_reason, arm_reason, err_m,
 *   bearing_deg10, heading_deg10, servo_us, esc_us, ch1_us, ch2_us, ch3_us,
 *   ch4_us, lat_e7, lon_e7, sats, speed_cms, imu_calib, gps_fix, imu_ok,
 *   rc_valid, gps_fresh, link_fresh, goto_owns, arrived, target_lat_e7,
 *   target_lon_e7, deadband_m, max_throttle_pct, throttle_gain, servo_gain
 *
 * Rejected CH3 entry attempts (blackbox_attempt) are a different row shape --
 * far fewer, unrelated fields -- so they get their own header/row pair
 * (blackbox_csv_attempt_header/_row) and their own dedicated section in the
 * dump, rather than being force-fit into the sample columns above.
 */

/* Number of CSV columns emitted (header fields == row fields). */
#define BLACKBOX_CSV_COLUMN_COUNT 34U

/* Safe minimum buffer size for one header or data line (excluding newline). The
 * header names line is the longest; a fully-populated 34-column row fits well
 * under this. Grows with the column set -- re-check when adding columns. */
#define BLACKBOX_CSV_LINE_MAX 512U

/* Column order (header == row) for blackbox_csv_attempt_row:
 *   attempt_seq, t_ms, sm_state, ok, armed, sticks_neutral, gps_fresh, gps_fix,
 *   ch1_us, ch2_us, ch3_us
 */
#define BLACKBOX_CSV_ATTEMPT_COLUMN_COUNT 11U

/**
 * Write the fixed CSV header line (no trailing newline) into out.
 *
 * @param out      Destination buffer (must be non-NULL).
 * @param out_len  Capacity of out; must be >= the header length + 1.
 * @return Number of characters written (excluding the NUL), or 0 if out is NULL
 *         or too small.
 */
size_t blackbox_csv_header(char *out, size_t out_len);

/**
 * Write one denormalised CSV data row (no trailing newline): the session context
 * (id, target, active settings) followed by the sample fields, in column order.
 * gps_fix / imu_ok are rendered as 0 or 1.
 *
 * @param header   Session context for this row (must be non-NULL).
 * @param sample   Decoded sample for this row (must be non-NULL).
 * @param out      Destination buffer (must be non-NULL).
 * @param out_len  Capacity of out; must be >= the row length + 1.
 * @return Number of characters written (excluding the NUL), or 0 on a NULL
 *         argument or a buffer too small for the full row.
 */
size_t blackbox_csv_row(const blackbox_session_header *header,
                        const blackbox_sample *sample, char *out,
                        size_t out_len);

/**
 * Write the fixed CSV header line for the attempt section (no trailing
 * newline) into out.
 *
 * @param out      Destination buffer (must be non-NULL).
 * @param out_len  Capacity of out; must be >= the header length + 1.
 * @return Number of characters written (excluding the NUL), or 0 if out is
 *         NULL or too small.
 */
size_t blackbox_csv_attempt_header(char *out, size_t out_len);

/**
 * Write one CSV attempt row (no trailing newline). ok / armed /
 * sticks_neutral / gps_fresh / gps_fix are rendered as 0 or 1.
 *
 * @param attempt  Decoded attempt for this row (must be non-NULL).
 * @param out      Destination buffer (must be non-NULL).
 * @param out_len  Capacity of out; must be >= the row length + 1.
 * @return Number of characters written (excluding the NUL), or 0 on a NULL
 *         argument or a buffer too small for the full row.
 */
size_t blackbox_csv_attempt_row(const blackbox_attempt *attempt, char *out,
                                size_t out_len);

#ifdef __cplusplus
}
#endif
