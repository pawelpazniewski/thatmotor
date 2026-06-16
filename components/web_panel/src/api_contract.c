#include "api_contract.h"

#include <string.h>

const char *api_error_code_str(api_error_code code)
{
    switch (code) {
    case API_ERR_VALIDATION_FAILED:
        return "VALIDATION_FAILED";
    case API_ERR_NOT_DISARMED:
        return API_CODE_NOT_DISARMED;
    case API_ERR_BAD_REQUEST:
        return "BAD_REQUEST";
    case API_ERR_INTERNAL:
        return "INTERNAL_ERROR";
    case API_OK:
    default:
        return "OK";
    }
}

const char *api_error_default_message(api_error_code code)
{
    switch (code) {
    case API_ERR_VALIDATION_FAILED:
        return "One or more parameters failed validation";
    case API_ERR_NOT_DISARMED:
        return "Settings can only be changed while DISARMED";
    case API_ERR_BAD_REQUEST:
        return "Malformed request body";
    case API_ERR_INTERNAL:
    default:
        return "Internal error";
    }
}

/* Append src to dst at *pos, JSON-escaping the minimal set of control chars and
 * quotes/backslashes. Never overflows cap; advances *pos by the bytes written.
 * Returns false if the buffer filled before src was fully written. */
static bool append_escaped(char *dst, size_t cap, size_t *pos, const char *src)
{
    for (const char *p = src; *p != '\0'; ++p) {
        const char *esc = NULL;
        char c = *p;
        if (c == '"') {
            esc = "\\\"";
        } else if (c == '\\') {
            esc = "\\\\";
        } else if (c == '\n') {
            esc = "\\n";
        } else if (c == '\t') {
            esc = "\\t";
        }
        if (esc != NULL) {
            size_t len = strlen(esc);
            if (*pos + len >= cap) {
                return false;
            }
            memcpy(dst + *pos, esc, len);
            *pos += len;
            continue;
        }
        if (*pos + 1 >= cap) {
            return false;
        }
        dst[*pos] = c;
        *pos += 1;
    }
    return true;
}

/* Append a raw (already-formed) literal to dst at *pos without escaping. */
static bool append_raw(char *dst, size_t cap, size_t *pos, const char *src)
{
    size_t len = strlen(src);
    if (*pos + len >= cap) {
        return false;
    }
    memcpy(dst + *pos, src, len);
    *pos += len;
    return true;
}

/* Finalise a built buffer: NUL-terminate and report the length, or fail (0). */
static size_t finalise(char *out, size_t out_size, size_t pos, bool ok)
{
    if (out_size >= 1) {
        out[ok && pos < out_size ? pos : 0] = '\0';
    }
    return ok ? pos : 0U;
}

size_t api_build_success(const char *data_json, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) {
        return 0U;
    }
    size_t pos = 0;
    bool ok = append_raw(out, out_size, &pos, "{\"data\":");
    ok = ok && append_raw(out, out_size, &pos,
                          data_json != NULL ? data_json : "null");
    ok = ok && append_raw(out, out_size, &pos, ",\"error\":null}");
    return finalise(out, out_size, pos, ok);
}

size_t api_build_error(api_error_code code, const char *message, char *out,
                       size_t out_size)
{
    if (out == NULL || out_size == 0U) {
        return 0U;
    }
    if (code == API_OK) {
        code = API_ERR_INTERNAL;
    }
    const char *msg = message != NULL ? message : api_error_default_message(code);
    size_t pos = 0;
    bool ok = append_raw(out, out_size, &pos,
                        "{\"data\":null,\"error\":{\"code\":\"");
    ok = ok && append_escaped(out, out_size, &pos, api_error_code_str(code));
    ok = ok && append_raw(out, out_size, &pos, "\",\"message\":\"");
    ok = ok && append_escaped(out, out_size, &pos, msg);
    ok = ok && append_raw(out, out_size, &pos, "\"}}");
    return finalise(out, out_size, pos, ok);
}
