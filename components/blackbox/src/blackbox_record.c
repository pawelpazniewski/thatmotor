#include "blackbox_record.h"

#include <string.h>

#include "blackbox_region.h"

/* IEEE 802.3 / zlib CRC32: reflected, poly 0xEDB88320, init+final 0xFFFFFFFF. */
#define CRC32_POLY 0xEDB88320U
#define CRC32_INIT 0xFFFFFFFFU
#define CRC32_BITS_PER_BYTE 8U

/* Framing byte offsets within a record. */
#define REC_MAGIC_LO 0U
#define REC_SCHEMA 2U
#define REC_TYPE 3U
#define REC_PAYLOAD 4U

/* The trailing CRC32 covers every byte before it (framing + payload + pad). */
#define REC_CRC_OFFSET (BLACKBOX_RECORD_SIZE - 4U)

/* Erased NOR flash reads back as 0xFF. */
#define FLASH_ERASED_BYTE 0xFFU

uint32_t blackbox_record_crc32(const uint8_t *data, size_t len)
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

/* ---- little-endian field cursor writers ---- */

static void put_u8(uint8_t *buf, size_t *pos, uint8_t value)
{
    buf[(*pos)++] = value;
}

static void put_u16(uint8_t *buf, size_t *pos, uint16_t value)
{
    buf[(*pos)++] = (uint8_t)(value & 0xFFU);
    buf[(*pos)++] = (uint8_t)((value >> 8) & 0xFFU);
}

static void put_u32(uint8_t *buf, size_t *pos, uint32_t value)
{
    buf[(*pos)++] = (uint8_t)(value & 0xFFU);
    buf[(*pos)++] = (uint8_t)((value >> 8) & 0xFFU);
    buf[(*pos)++] = (uint8_t)((value >> 16) & 0xFFU);
    buf[(*pos)++] = (uint8_t)((value >> 24) & 0xFFU);
}

/* Signed 32-bit: store the two's-complement bit pattern as LE u32. */
static void put_i32(uint8_t *buf, size_t *pos, int32_t value)
{
    put_u32(buf, pos, (uint32_t)value);
}

/* ---- little-endian field cursor readers ---- */

static uint8_t get_u8(const uint8_t *buf, size_t *pos)
{
    return buf[(*pos)++];
}

static uint16_t get_u16(const uint8_t *buf, size_t *pos)
{
    uint16_t lo = buf[(*pos)++];
    uint16_t hi = buf[(*pos)++];
    return (uint16_t)(lo | (hi << 8));
}

static uint32_t get_u32(const uint8_t *buf, size_t *pos)
{
    uint32_t b0 = buf[(*pos)++];
    uint32_t b1 = buf[(*pos)++];
    uint32_t b2 = buf[(*pos)++];
    uint32_t b3 = buf[(*pos)++];
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

/* Signed 32-bit: read the LE u32 bit pattern back as two's-complement int32. */
static int32_t get_i32(const uint8_t *buf, size_t *pos)
{
    return (int32_t)get_u32(buf, pos);
}

/* ---- framing helpers ---- */

/* Zero the whole slot, stamp magic/schema/type, and return the payload cursor. */
static size_t begin_record(uint8_t *out, blackbox_record_type type)
{
    memset(out, 0, BLACKBOX_RECORD_SIZE);
    size_t pos = 0;
    put_u16(out, &pos, BLACKBOX_RECORD_MAGIC);
    put_u8(out, &pos, BLACKBOX_RECORD_SCHEMA);
    put_u8(out, &pos, (uint8_t)type);
    return pos; /* == REC_PAYLOAD */
}

/* Stamp the trailing CRC over every preceding byte (framing + payload + pad). */
static void finish_record(uint8_t *out)
{
    uint32_t crc = blackbox_record_crc32(out, REC_CRC_OFFSET);
    size_t pos = REC_CRC_OFFSET;
    put_u32(out, &pos, crc);
}

static bool is_erased(const uint8_t *buf)
{
    for (size_t i = 0; i < BLACKBOX_RECORD_SIZE; i++) {
        if (buf[i] != FLASH_ERASED_BYTE) {
            return false;
        }
    }
    return true;
}

/* ---- payload writers ---- */

static void write_sample(const blackbox_sample *s, uint8_t *out, size_t *pos)
{
    put_u32(out, pos, s->t_ms);
    put_u8(out, pos, s->substate);
    put_u16(out, pos, s->err_m);
    put_u16(out, pos, s->bearing_deg10);
    put_u16(out, pos, s->heading_deg10);
    put_u16(out, pos, s->servo_us);
    put_u16(out, pos, s->esc_us);
    put_u16(out, pos, s->ch1_us);
    put_u16(out, pos, s->ch2_us);
    put_i32(out, pos, s->lat_e7);
    put_i32(out, pos, s->lon_e7);
    put_u8(out, pos, s->sats);
    put_u16(out, pos, s->speed_cms);
    uint8_t flags = 0;
    flags |= s->gps_fix ? BLACKBOX_FLAG_GPS_FIX : 0U;
    flags |= s->imu_ok ? BLACKBOX_FLAG_IMU_OK : 0U;
    put_u8(out, pos, flags);
}

static void write_header(const blackbox_session_header *h, uint8_t *out,
                         size_t *pos)
{
    put_u32(out, pos, h->session_seq);
    put_i32(out, pos, h->target_lat_e7);
    put_i32(out, pos, h->target_lon_e7);
    put_u16(out, pos, h->deadband_m);
    put_u16(out, pos, h->max_throttle_pct);
    put_u16(out, pos, h->throttle_gain);
    put_u16(out, pos, h->servo_gain);
    put_u32(out, pos, h->start_ms);
}

/* ---- payload readers ---- */

static void read_sample(const uint8_t *buf, size_t *pos, blackbox_sample *s)
{
    s->t_ms = get_u32(buf, pos);
    s->substate = get_u8(buf, pos);
    s->err_m = get_u16(buf, pos);
    s->bearing_deg10 = get_u16(buf, pos);
    s->heading_deg10 = get_u16(buf, pos);
    s->servo_us = get_u16(buf, pos);
    s->esc_us = get_u16(buf, pos);
    s->ch1_us = get_u16(buf, pos);
    s->ch2_us = get_u16(buf, pos);
    s->lat_e7 = get_i32(buf, pos);
    s->lon_e7 = get_i32(buf, pos);
    s->sats = get_u8(buf, pos);
    s->speed_cms = get_u16(buf, pos);
    uint8_t flags = get_u8(buf, pos);
    s->gps_fix = (flags & BLACKBOX_FLAG_GPS_FIX) != 0U;
    s->imu_ok = (flags & BLACKBOX_FLAG_IMU_OK) != 0U;
}

static void read_header(const uint8_t *buf, size_t *pos,
                        blackbox_session_header *h)
{
    h->session_seq = get_u32(buf, pos);
    h->target_lat_e7 = get_i32(buf, pos);
    h->target_lon_e7 = get_i32(buf, pos);
    h->deadband_m = get_u16(buf, pos);
    h->max_throttle_pct = get_u16(buf, pos);
    h->throttle_gain = get_u16(buf, pos);
    h->servo_gain = get_u16(buf, pos);
    h->start_ms = get_u32(buf, pos);
}

/* ---- public encode ---- */

blackbox_record_result blackbox_record_encode_sample(
    const blackbox_sample *sample, uint8_t *out, size_t out_len)
{
    if (sample == NULL || out == NULL) {
        return BLACKBOX_REC_ERR_ARG;
    }
    if (out_len < BLACKBOX_RECORD_SIZE) {
        return BLACKBOX_REC_ERR_LENGTH;
    }
    size_t pos = begin_record(out, BLACKBOX_TYPE_SAMPLE);
    write_sample(sample, out, &pos);
    finish_record(out);
    return BLACKBOX_REC_OK;
}

blackbox_record_result blackbox_record_encode_header(
    const blackbox_session_header *header, uint8_t *out, size_t out_len)
{
    if (header == NULL || out == NULL) {
        return BLACKBOX_REC_ERR_ARG;
    }
    if (out_len < BLACKBOX_RECORD_SIZE) {
        return BLACKBOX_REC_ERR_LENGTH;
    }
    size_t pos = begin_record(out, BLACKBOX_TYPE_HEADER);
    write_header(header, out, &pos);
    finish_record(out);
    return BLACKBOX_REC_OK;
}

/* ---- public decode ---- */

/* Shared framing gate: length, erased, magic, CRC, schema. On OK the caller
 * still owns the type check. */
static blackbox_record_result validate_framing(const uint8_t *buf, size_t len,
                                               blackbox_record_type *out_type)
{
    if (buf == NULL || out_type == NULL) {
        return BLACKBOX_REC_ERR_ARG;
    }
    if (len != BLACKBOX_RECORD_SIZE) {
        return BLACKBOX_REC_ERR_LENGTH;
    }
    if (is_erased(buf)) {
        return BLACKBOX_REC_EMPTY;
    }
    size_t pos = REC_MAGIC_LO;
    if (get_u16(buf, &pos) != BLACKBOX_RECORD_MAGIC) {
        return BLACKBOX_REC_ERR_MAGIC;
    }
    uint32_t computed = blackbox_record_crc32(buf, REC_CRC_OFFSET);
    size_t crc_pos = REC_CRC_OFFSET;
    if (get_u32(buf, &crc_pos) != computed) {
        return BLACKBOX_REC_ERR_CRC;
    }
    if (buf[REC_SCHEMA] != BLACKBOX_RECORD_SCHEMA) {
        return BLACKBOX_REC_ERR_SCHEMA;
    }
    *out_type = (blackbox_record_type)buf[REC_TYPE];
    return BLACKBOX_REC_OK;
}

blackbox_record_result blackbox_record_classify(const uint8_t *buf, size_t len,
                                                blackbox_record_type *out_type)
{
    return validate_framing(buf, len, out_type);
}

blackbox_record_result blackbox_record_decode_sample(const uint8_t *buf,
                                                     size_t len,
                                                     blackbox_sample *out)
{
    if (out == NULL) {
        return BLACKBOX_REC_ERR_ARG;
    }
    blackbox_record_type type;
    blackbox_record_result framing = validate_framing(buf, len, &type);
    if (framing != BLACKBOX_REC_OK) {
        return framing;
    }
    if (type != BLACKBOX_TYPE_SAMPLE) {
        return BLACKBOX_REC_ERR_TYPE;
    }
    size_t pos = REC_PAYLOAD;
    read_sample(buf, &pos, out);
    return BLACKBOX_REC_OK;
}

blackbox_record_result blackbox_record_decode_header(
    const uint8_t *buf, size_t len, blackbox_session_header *out)
{
    if (out == NULL) {
        return BLACKBOX_REC_ERR_ARG;
    }
    blackbox_record_type type;
    blackbox_record_result framing = validate_framing(buf, len, &type);
    if (framing != BLACKBOX_REC_OK) {
        return framing;
    }
    if (type != BLACKBOX_TYPE_HEADER) {
        return BLACKBOX_REC_ERR_TYPE;
    }
    size_t pos = REC_PAYLOAD;
    read_header(buf, &pos, out);
    return BLACKBOX_REC_OK;
}
