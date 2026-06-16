#pragma once

#include <stdbool.h>

#include "blob_codec.h"
#include "settings_model.h"
#include "settings_validate.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) provenance decision for a stored settings blob.
 *
 * The HAL (nvs_store.c) only reads raw bytes from NVS and reports two facts:
 *   - whether a blob was found (and readable) at all (nvs_read_status), and
 *   - if found, the blob_codec_decode outcome over those bytes.
 *
 * resolve_provenance composes those facts into the final params + telemetry
 * flags (R16): empty -> defaults; corrupt (CRC/length) -> defaults + nvs_error;
 * newer schema -> defaults + alert (distinct from corruption); valid -> the Unit
 * 4 settings_validate result (which may itself report MIXED_RECOVERED). A
 * rejected blob can never yield a value outside the sanity window because every
 * non-OK branch produces conservative defaults.
 *
 * No IDF dependencies, so the decision logic is host-testable in isolation.
 */

/** Outcome of reading the raw blob from NVS, mapped from the HAL by the HAL. */
typedef enum {
    NVS_READ_OK = 0,        /* a blob of some bytes was read into the buffer */
    NVS_READ_NOT_FOUND = 1, /* the key is absent: a genuinely empty store */
    NVS_READ_ERROR = 2,     /* the blob exists but could not be read back */
} nvs_read_status;

/**
 * Compose the read status + decode result into final params and provenance.
 *
 * @param read_status   How the raw read went (found / absent / unreadable).
 * @param decode        blob_codec_decode result; only meaningful when
 *                      read_status == NVS_READ_OK (ignored otherwise).
 * @param decoded       The decoded params; only read when read_status ==
 *                      NVS_READ_OK && decode == BLOB_CODEC_OK (may be NULL
 *                      otherwise).
 * @param out           Destination for the resulting (validated) params
 *                      (must be non-NULL).
 * @return              Provenance/telemetry flags for the result.
 */
settings_validation_result resolve_provenance(nvs_read_status read_status,
                                              blob_codec_result decode,
                                              const settings_params *decoded,
                                              settings_params *out);

#ifdef __cplusplus
}
#endif
