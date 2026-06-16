#include "nvs_provenance.h"

#include <stdbool.h>
#include <stddef.h>

#include "blob_codec.h"
#include "settings_model.h"
#include "settings_validate.h"

/* Produce conservative defaults with an explicit nvs_error flag. settings_validate
 * always reports nvs_error=false (it cannot tell empty from corrupt), so the NVS
 * layer owns that distinction and stamps it here. */
static settings_validation_result defaults_with_error(settings_params *out,
                                                      bool nvs_error)
{
    settings_validation_result result = settings_validate(NULL, false, out);
    result.nvs_error = nvs_error;
    return result;
}

/* A blob was found and decoded. OK -> run Unit 4 validation on it (may yield
 * MIXED_RECOVERED). Corrupt (CRC/length) -> defaults + nvs_error. A newer schema
 * is an alert, not corruption, so it falls back to defaults WITHOUT nvs_error. */
static settings_validation_result resolve_decoded(blob_codec_result decode,
                                                  const settings_params *decoded,
                                                  settings_params *out)
{
    if (decode == BLOB_CODEC_OK) {
        return settings_validate(decoded, true, out);
    }
    bool is_corruption =
        (decode == BLOB_CODEC_ERR_CRC || decode == BLOB_CODEC_ERR_LENGTH);
    return defaults_with_error(out, is_corruption);
}

settings_validation_result resolve_provenance(nvs_read_status read_status,
                                              blob_codec_result decode,
                                              const settings_params *decoded,
                                              settings_params *out)
{
    switch (read_status) {
    case NVS_READ_OK:
        return resolve_decoded(decode, decoded, out);
    case NVS_READ_ERROR:
        /* The blob exists but the read itself failed: unreadable -> error. */
        return defaults_with_error(out, true);
    case NVS_READ_NOT_FOUND:
    default:
        /* Genuinely empty store: silent defaults, no error. */
        return defaults_with_error(out, false);
    }
}
