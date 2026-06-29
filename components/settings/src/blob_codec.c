#include "blob_codec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings_model.h"

/* IEEE 802.3 / zlib CRC32: reflected, poly 0xEDB88320, init+final 0xFFFFFFFF. */
#define CRC32_POLY 0xEDB88320U
#define CRC32_INIT 0xFFFFFFFFU
#define CRC32_BITS_PER_BYTE 8U

uint32_t blob_codec_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = CRC32_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < CRC32_BITS_PER_BYTE; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (CRC32_POLY & mask);
        }
    }
    return crc ^ CRC32_INIT;
}

/* Little-endian field cursor writers. */
static void put_u16(uint8_t *buf, size_t *pos, uint16_t value)
{
    buf[(*pos)++] = (uint8_t)(value & 0xFFU);
    buf[(*pos)++] = (uint8_t)((value >> 8) & 0xFFU);
}

static void put_bool(uint8_t *buf, size_t *pos, bool value)
{
    buf[(*pos)++] = value ? 1U : 0U;
}

/* Signed 16-bit: store the two's-complement bit pattern as LE u16. */
static void put_i16(uint8_t *buf, size_t *pos, int16_t value)
{
    put_u16(buf, pos, (uint16_t)value);
}

/* Little-endian field cursor readers. */
static uint16_t get_u16(const uint8_t *buf, size_t *pos)
{
    uint16_t lo = buf[(*pos)++];
    uint16_t hi = buf[(*pos)++];
    return (uint16_t)(lo | (hi << 8));
}

static bool get_bool(const uint8_t *buf, size_t *pos)
{
    return buf[(*pos)++] != 0U;
}

/* Signed 16-bit: read the LE u16 bit pattern back as two's-complement int16. */
static int16_t get_i16(const uint8_t *buf, size_t *pos)
{
    return (int16_t)get_u16(buf, pos);
}

/* Serialise every settings_params field into buf in declared order. The stored
 * schema_version is always the current one, not whatever the caller passed. */
static void serialize_fields(const settings_params *p, uint8_t *buf, size_t *pos)
{
    put_u16(buf, pos, SETTINGS_SCHEMA_VERSION);

    put_u16(buf, pos, p->rc_min_us);
    put_u16(buf, pos, p->rc_mid_us);
    put_u16(buf, pos, p->rc_max_us);

    put_u16(buf, pos, p->servo_slew_us_per_cycle);
    put_u16(buf, pos, p->servo_min_us);
    put_u16(buf, pos, p->servo_max_us);
    put_u16(buf, pos, p->steer_deadband_us);
    put_bool(buf, pos, p->servo_reverse);
    put_i16(buf, pos, p->servo_trim_us);

    put_u16(buf, pos, p->esc_ramp_up_us_per_cycle);
    put_u16(buf, pos, p->esc_ramp_down_us_per_cycle);
    put_u16(buf, pos, p->throttle_deadband_us);
    put_u16(buf, pos, p->max_throttle_fwd_pct);
    put_u16(buf, pos, p->max_throttle_rev_pct);
    put_bool(buf, pos, p->throttle_reverse);

    put_u16(buf, pos, p->esc_neutral_us);
    put_u16(buf, pos, p->esc_neutral_band_us);
    put_u16(buf, pos, p->esc_forward_min_us);
    put_u16(buf, pos, p->esc_forward_max_us);
    put_u16(buf, pos, p->esc_reverse_min_us);
    put_u16(buf, pos, p->esc_reverse_max_us);

    put_u16(buf, pos, p->failsafe_timeout_ms);
    put_u16(buf, pos, p->reverse_neutral_dwell_ms);

    put_bool(buf, pos, p->ch4_mode_switch_enabled);
    put_u16(buf, pos, p->ch4_switch_threshold_us);

    put_u16(buf, pos, p->deploy_servo_us);
    put_u16(buf, pos, p->click_window_ms);

    put_u16(buf, pos, p->spot_lock_deadband_m);
    put_u16(buf, pos, p->spot_lock_max_throttle_pct);
    put_u16(buf, pos, p->spot_lock_throttle_gain);
    put_u16(buf, pos, p->spot_lock_servo_gain);
}

/* Deserialise every settings_params field from buf in declared order. */
static void deserialize_fields(const uint8_t *buf, size_t *pos, settings_params *p)
{
    p->schema_version = get_u16(buf, pos);

    p->rc_min_us = get_u16(buf, pos);
    p->rc_mid_us = get_u16(buf, pos);
    p->rc_max_us = get_u16(buf, pos);

    p->servo_slew_us_per_cycle = get_u16(buf, pos);
    p->servo_min_us = get_u16(buf, pos);
    p->servo_max_us = get_u16(buf, pos);
    p->steer_deadband_us = get_u16(buf, pos);
    p->servo_reverse = get_bool(buf, pos);
    p->servo_trim_us = get_i16(buf, pos);

    p->esc_ramp_up_us_per_cycle = get_u16(buf, pos);
    p->esc_ramp_down_us_per_cycle = get_u16(buf, pos);
    p->throttle_deadband_us = get_u16(buf, pos);
    p->max_throttle_fwd_pct = get_u16(buf, pos);
    p->max_throttle_rev_pct = get_u16(buf, pos);
    p->throttle_reverse = get_bool(buf, pos);

    p->esc_neutral_us = get_u16(buf, pos);
    p->esc_neutral_band_us = get_u16(buf, pos);
    p->esc_forward_min_us = get_u16(buf, pos);
    p->esc_forward_max_us = get_u16(buf, pos);
    p->esc_reverse_min_us = get_u16(buf, pos);
    p->esc_reverse_max_us = get_u16(buf, pos);

    p->failsafe_timeout_ms = get_u16(buf, pos);
    p->reverse_neutral_dwell_ms = get_u16(buf, pos);

    p->ch4_mode_switch_enabled = get_bool(buf, pos);
    p->ch4_switch_threshold_us = get_u16(buf, pos);

    p->deploy_servo_us = get_u16(buf, pos);
    p->click_window_ms = get_u16(buf, pos);

    p->spot_lock_deadband_m = get_u16(buf, pos);
    p->spot_lock_max_throttle_pct = get_u16(buf, pos);
    p->spot_lock_throttle_gain = get_u16(buf, pos);
    p->spot_lock_servo_gain = get_u16(buf, pos);
}

blob_codec_result blob_codec_encode(const settings_params *params, uint8_t *out,
                                    size_t out_len)
{
    if (params == NULL || out == NULL) {
        return BLOB_CODEC_ERR_ARG;
    }
    if (out_len < BLOB_CODEC_SIZE) {
        return BLOB_CODEC_ERR_LENGTH;
    }

    size_t pos = 0;
    serialize_fields(params, out, &pos);

    uint32_t crc = blob_codec_crc32(out, BLOB_CODEC_FIELD_BYTES);
    put_u16(out, &pos, (uint16_t)(crc & 0xFFFFU));
    put_u16(out, &pos, (uint16_t)((crc >> 16) & 0xFFFFU));
    return BLOB_CODEC_OK;
}

blob_codec_result blob_codec_decode(const uint8_t *blob, size_t len,
                                    settings_params *out)
{
    if (blob == NULL || out == NULL) {
        return BLOB_CODEC_ERR_ARG;
    }
    if (len != BLOB_CODEC_SIZE) {
        return BLOB_CODEC_ERR_LENGTH;
    }

    uint32_t computed = blob_codec_crc32(blob, BLOB_CODEC_FIELD_BYTES);
    size_t crc_pos = BLOB_CODEC_FIELD_BYTES;
    uint32_t stored = (uint32_t)get_u16(blob, &crc_pos);
    stored |= (uint32_t)get_u16(blob, &crc_pos) << 16;
    if (stored != computed) {
        return BLOB_CODEC_ERR_CRC;
    }

    settings_params decoded;
    size_t pos = 0;
    deserialize_fields(blob, &pos, &decoded);
    if (decoded.schema_version != SETTINGS_SCHEMA_VERSION) {
        return BLOB_CODEC_ERR_SCHEMA;
    }

    *out = decoded;
    return BLOB_CODEC_OK;
}
