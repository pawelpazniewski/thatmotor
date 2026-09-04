#include "telemetry_json.h"

#include <stdio.h>

int telemetry_json_format(const control_loop_snapshot *s, char *buf, size_t n)
{
    return snprintf(buf, n,
        "{\"fw_version\":\"%s\","
        "\"state\":%d,\"arm_reason\":%u,\"rc_valid\":%s,\"ch1_us\":%u,"
        "\"ch2_us\":%u,\"ch4_us\":%u,\"ch3_us\":%u,\"ch1_period_us\":%u,"
        "\"ch2_period_us\":%u,"
        "\"ch1_valid\":%s,\"ch2_valid\":%s,\"servo_us\":%u,\"esc_us\":%u,"
        "\"servo_trim_us\":%d,"
        "\"source\":%d,\"settings_valid\":%s,\"calibrated\":%s,"
        "\"defaults_used\":%s,\"nvs_error\":%s,"
        "\"gps_fix\":%s,\"gps_sats\":%u,\"gps_lat_e7\":%d,\"gps_lon_e7\":%d,"
        "\"gps_speed_cms\":%u,"
        "\"imu_ok\":%s,\"imu_heading_deg10\":%u,\"imu_calib\":%u,"
        "\"spot_lock_state\":%u,\"spot_lock_err_m\":%u,"
        "\"spot_lock_bearing_deg10\":%u,"
        "\"goto_state\":%u,\"goto_target_lat_e7\":%d,\"goto_target_lon_e7\":%d,"
        "\"goto_err_m\":%u,\"goto_bearing_deg10\":%u,\"goto_arrived\":%s,"
        "\"app_link_fresh\":%s}",
        s->fw_version,
        (int)s->state, (unsigned)s->arm_reason, s->rc_valid ? "true" : "false",
        (unsigned)s->ch1_us, (unsigned)s->ch2_us, (unsigned)s->ch4_us,
        (unsigned)s->ch3_us,
        (unsigned)s->ch1_period_us, (unsigned)s->ch2_period_us,
        s->ch1_valid ? "true" : "false", s->ch2_valid ? "true" : "false",
        (unsigned)s->servo_us, (unsigned)s->esc_us, (int)s->servo_trim_us,
        (int)s->source,
        s->settings_valid ? "true" : "false",
        s->calibrated ? "true" : "false",
        s->defaults_used ? "true" : "false",
        s->nvs_error ? "true" : "false",
        s->gps_fix ? "true" : "false", (unsigned)s->gps_sats,
        (int)s->gps_lat_e7, (int)s->gps_lon_e7, (unsigned)s->gps_speed_cms,
        s->imu_ok ? "true" : "false", (unsigned)s->imu_heading_deg10,
        (unsigned)s->imu_calib,
        (unsigned)s->spot_lock_state, (unsigned)s->spot_lock_err_m,
        (unsigned)s->spot_lock_bearing_deg10,
        (unsigned)s->goto_state, (int)s->goto_target_lat_e7,
        (int)s->goto_target_lon_e7, (unsigned)s->goto_err_m,
        (unsigned)s->goto_bearing_deg10, s->goto_arrived ? "true" : "false",
        s->app_link_fresh ? "true" : "false");
}
