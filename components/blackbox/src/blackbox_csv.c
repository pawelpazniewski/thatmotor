#include "blackbox_csv.h"

#include <stdint.h>
#include <stdio.h>

/* The header string and the row column order are one contract; keep them in sync
 * with BLACKBOX_CSV_COLUMN_COUNT and the row format below. */
static const char CSV_HEADER[] =
    "session_id,t_ms,substate,sm_state,source,end_reason,arm_reason,err_m,"
    "bearing_deg10,heading_deg10,servo_us,esc_us,ch1_us,ch2_us,ch3_us,ch4_us,"
    "lat_e7,lon_e7,sats,speed_cms,imu_calib,gps_fix,imu_ok,rc_valid,gps_fresh,"
    "link_fresh,goto_owns,arrived,target_lat_e7,target_lon_e7,deadband_m,"
    "max_throttle_pct,throttle_gain,servo_gain";

/* snprintf returns the length it WOULD have written; a value >= capacity means
 * the line was truncated, which we treat as a hard failure (return 0) rather
 * than emitting a partial, unparseable row. */
static size_t written_or_zero(int printed, size_t out_len)
{
    if (printed < 0 || (size_t)printed >= out_len) {
        return 0U;
    }
    return (size_t)printed;
}

size_t blackbox_csv_header(char *out, size_t out_len)
{
    if (out == NULL) {
        return 0U;
    }
    int printed = snprintf(out, out_len, "%s", CSV_HEADER);
    return written_or_zero(printed, out_len);
}

size_t blackbox_csv_row(const blackbox_session_header *header,
                        const blackbox_sample *sample, char *out, size_t out_len)
{
    if (header == NULL || sample == NULL || out == NULL) {
        return 0U;
    }
    int printed = snprintf(
        out, out_len,
        "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,"
        "%u,%u,%u,%u,%d,%d,%u,%u,%u,%u",
        (unsigned)header->session_seq, (unsigned)sample->t_ms,
        (unsigned)sample->substate, (unsigned)sample->sm_state,
        (unsigned)sample->source, (unsigned)sample->end_reason,
        (unsigned)sample->arm_reason, (unsigned)sample->err_m,
        (unsigned)sample->bearing_deg10, (unsigned)sample->heading_deg10,
        (unsigned)sample->servo_us, (unsigned)sample->esc_us,
        (unsigned)sample->ch1_us, (unsigned)sample->ch2_us,
        (unsigned)sample->ch3_us, (unsigned)sample->ch4_us, (int)sample->lat_e7,
        (int)sample->lon_e7, (unsigned)sample->sats, (unsigned)sample->speed_cms,
        (unsigned)sample->imu_calib, (unsigned)(sample->gps_fix ? 1U : 0U),
        (unsigned)(sample->imu_ok ? 1U : 0U),
        (unsigned)(sample->rc_valid ? 1U : 0U),
        (unsigned)(sample->gps_fresh ? 1U : 0U),
        (unsigned)(sample->link_fresh ? 1U : 0U),
        (unsigned)(sample->goto_owns ? 1U : 0U),
        (unsigned)(sample->arrived ? 1U : 0U), (int)header->target_lat_e7,
        (int)header->target_lon_e7, (unsigned)header->deadband_m,
        (unsigned)header->max_throttle_pct, (unsigned)header->throttle_gain,
        (unsigned)header->servo_gain);
    return written_or_zero(printed, out_len);
}
