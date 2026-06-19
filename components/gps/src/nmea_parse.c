#include "nmea_parse.h"

#include <stddef.h>
#include <string.h>

/* Maximum number of comma-separated fields we index in a sentence. GGA uses 15;
 * RMC/VTG fewer. A small fixed cap keeps the parser allocation-free. */
#define NMEA_MAX_FIELDS 24

/* Scale factor: degrees -> degrees * 1e7 (the stored coordinate domain). */
#define NMEA_DEG_SCALE 10000000

/* Knots -> cm/s: 1 knot = 1.852 km/h = 51.444 cm/s. Stored as a scaled rational
 * (numerator/denominator) so the conversion stays integer-only. */
#define NMEA_KNOT_CMS_NUM 514444
#define NMEA_KNOT_CMS_DEN 10000

/* km/h -> cm/s: 1 km/h = 100000 cm / 3600 s. Integer-only. */
#define NMEA_KMH_CMS_NUM 100000
#define NMEA_KMH_CMS_DEN 3600

/* Split @p line at commas into @p fields (pointers into @p work, which is a
 * mutable copy of the sentence body). Returns the number of fields found. */
static int split_fields(char *work, const char **fields, int max_fields)
{
    int count = 0;
    fields[count++] = work;
    for (char *p = work; *p != '\0'; ++p) {
        if (*p != ',') {
            continue;
        }
        *p = '\0';
        if (count >= max_fields) {
            break;
        }
        fields[count++] = p + 1;
    }
    return count;
}

/* Parse a non-negative integer from [start, end). Returns false on empty input
 * or any non-digit character. */
static bool parse_uint(const char *s, uint32_t *out)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    uint32_t value = 0;
    for (const char *p = s; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + (uint32_t)(*p - '0');
    }
    *out = value;
    return true;
}

/* Convert a single hex nibble to its value, or -1 if not a hex digit. */
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/* Validate the NMEA checksum and copy the sentence body (between '$' and '*')
 * into @p body. The checksum is the XOR of every char between '$' and '*'.
 * Returns false on a missing '$'/'*', a malformed checksum, or a mismatch. */
static bool checksum_ok(const char *line, char *body, size_t body_size)
{
    if (line == NULL || line[0] != '$') {
        return false;
    }
    uint8_t sum = 0;
    size_t i = 1;
    for (; line[i] != '\0' && line[i] != '*'; ++i) {
        sum ^= (uint8_t)line[i];
    }
    if (line[i] != '*') {
        return false;
    }
    int high = hex_nibble(line[i + 1]);
    int low = (high < 0) ? -1 : hex_nibble(line[i + 2]);
    if (high < 0 || low < 0) {
        return false;
    }
    if ((uint8_t)((high << 4) | low) != sum) {
        return false;
    }
    size_t body_len = i - 1; /* chars between '$' and '*' */
    if (body_len >= body_size) {
        return false;
    }
    memcpy(body, line + 1, body_len);
    body[body_len] = '\0';
    return true;
}

/* Parse an NMEA ddmm.mmmm / dddmm.mmmm coordinate plus a N/S/E/W hemisphere into
 * degrees * 1e7. @p deg_digits is 2 for latitude, 3 for longitude. */
static bool parse_coord(const char *value, const char *hemi, int deg_digits,
                        int32_t *out)
{
    if (value == NULL || value[0] == '\0' || hemi == NULL ||
        hemi[0] == '\0') {
        return false;
    }
    const char *dot = strchr(value, '.');
    if (dot == NULL || (dot - value) < deg_digits + 2) {
        return false;
    }
    char deg_buf[4] = {0};
    memcpy(deg_buf, value, (size_t)deg_digits);
    uint32_t degrees = 0;
    if (!parse_uint(deg_buf, &degrees)) {
        return false;
    }
    /* Minutes = the rest of the integer part + fraction. Parse as a fixed-point
     * value scaled to 1e5 (5 fractional digits) without floating point. */
    const char *min_int = value + deg_digits;
    uint32_t min_whole = 0;
    char min_buf[3] = {min_int[0], min_int[1], '\0'};
    if (!parse_uint(min_buf, &min_whole)) {
        return false;
    }
    uint32_t frac = 0;
    uint32_t frac_scale = 1;
    for (const char *p = dot + 1; *p != '\0' && frac_scale <= 100000; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        frac = frac * 10 + (uint32_t)(*p - '0');
        frac_scale *= 10;
    }
    /* minutes_e5 = (min_whole + frac/frac_scale) * 1e5 */
    uint64_t minutes_e5 =
        (uint64_t)min_whole * 100000 + (uint64_t)frac * (100000 / frac_scale);
    /* degrees_e7 = degrees*1e7 + minutes_e5 / 60 * (1e7 / 1e5) */
    uint64_t result = (uint64_t)degrees * NMEA_DEG_SCALE + minutes_e5 * 100 / 60;
    if (result > 1800000000ULL) {
        return false;
    }
    char h = hemi[0];
    int32_t signed_result = (int32_t)result;
    if (h == 'S' || h == 'W') {
        signed_result = -signed_result;
    } else if (h != 'N' && h != 'E') {
        return false;
    }
    *out = signed_result;
    return true;
}

/* Parse a fixed-point decimal (e.g. "22.4") scaled by @p scale into an integer.
 * "22.4" with scale 1000 -> 22400. Returns false on a malformed field. */
static bool parse_scaled(const char *s, uint32_t scale, uint32_t *out)
{
    if (s == NULL || s[0] == '\0') {
        return false;
    }
    uint32_t whole = 0;
    const char *p = s;
    for (; *p != '\0' && *p != '.'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        whole = whole * 10 + (uint32_t)(*p - '0');
    }
    uint32_t value = whole * scale;
    if (*p == '.') {
        uint32_t step = scale;
        for (++p; *p != '\0' && step > 1; ++p) {
            if (*p < '0' || *p > '9') {
                return false;
            }
            step /= 10;
            value += (uint32_t)(*p - '0') * step;
        }
    }
    *out = value;
    return true;
}

/* Match a sentence type ignoring the 2-char talker id: body "GPGGA" matches
 * type "GGA" via its 3-char suffix at offset 2. */
static bool is_type(const char *body, const char *type)
{
    return strlen(body) >= 5 && strncmp(body + 2, type, 3) == 0;
}

/* GGA: field[6]=fix quality, [7]=sats, [2..3]=lat, [4..5]=lon. */
static bool parse_gga(const char **f, int n, gps_state *st)
{
    if (n < 8) {
        return false;
    }
    uint32_t quality = 0;
    if (!parse_uint(f[6], &quality)) {
        return false;
    }
    gps_state next = *st;
    next.fix = (quality > 0);
    if (!next.fix) {
        *st = next;
        return true;
    }
    uint32_t sats = 0;
    if (!parse_uint(f[7], &sats) || sats > 255) {
        return false;
    }
    if (!parse_coord(f[2], f[3], 2, &next.lat_e7) ||
        !parse_coord(f[4], f[5], 3, &next.lon_e7)) {
        return false;
    }
    next.sats = (uint8_t)sats;
    *st = next;
    return true;
}

/* Apply a knots speed + course pair into @p st. */
static bool apply_speed_course(const char *speed_field, const char *course_field,
                               uint32_t speed_num, uint32_t speed_den,
                               gps_state *st)
{
    uint32_t speed_scaled = 0;
    if (!parse_scaled(speed_field, 1000, &speed_scaled)) {
        return false;
    }
    /* speed_field is value*1000; convert to cm/s: value * num/den. */
    uint64_t cms = (uint64_t)speed_scaled * speed_num / speed_den / 1000;
    st->speed_cms = (cms > 65535) ? 65535 : (uint16_t)cms;
    uint32_t course = 0;
    if (parse_scaled(course_field, 1, &course)) {
        st->course_deg = (course > 360) ? 360 : (uint16_t)course;
    }
    return true;
}

/* RMC: field[2]=status A/V, [7]=speed (knots), [8]=course. */
static bool parse_rmc(const char **f, int n, gps_state *st)
{
    if (n < 9) {
        return false;
    }
    if (f[2] == NULL || f[2][0] != 'A') {
        return true; /* void fix: valid sentence, no speed update */
    }
    return apply_speed_course(f[7], f[8], NMEA_KNOT_CMS_NUM, NMEA_KNOT_CMS_DEN,
                              st);
}

/* VTG: field[1]=course (true), [7]=speed (km/h). */
static bool parse_vtg(const char **f, int n, gps_state *st)
{
    if (n < 8) {
        return false;
    }
    return apply_speed_course(f[7], f[1], NMEA_KMH_CMS_NUM, NMEA_KMH_CMS_DEN,
                              st);
}

bool nmea_parse_line(const char *line, gps_state *st)
{
    if (line == NULL || st == NULL) {
        return false;
    }
    char body[83]; /* NMEA sentences are <= 82 chars including framing */
    if (!checksum_ok(line, body, sizeof(body))) {
        return false;
    }
    const char *fields[NMEA_MAX_FIELDS];
    int n = split_fields(body, fields, NMEA_MAX_FIELDS);
    if (is_type(body, "GGA")) {
        return parse_gga(fields, n, st);
    }
    if (is_type(body, "RMC")) {
        return parse_rmc(fields, n, st);
    }
    if (is_type(body, "VTG")) {
        return parse_vtg(fields, n, st);
    }
    return false;
}
