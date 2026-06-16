#pragma once

#include <stdbool.h>

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Where the effective settings came from / their trust level. Discriminated
 * source state instead of a pile of booleans.
 *
 * SETTINGS_SOURCE_DEFAULTS        No usable stored blob: pure conservative
 *                                 defaults (empty/corrupt/version-mismatch NVS).
 * SETTINGS_SOURCE_NVS             Stored blob loaded and every field valid.
 * SETTINGS_SOURCE_MIXED_RECOVERED Stored blob loaded but one or more fields
 *                                 were out of range / failed a cross-field
 *                                 invariant and were replaced by defaults.
 */
typedef enum {
    SETTINGS_SOURCE_DEFAULTS = 0,
    SETTINGS_SOURCE_NVS = 1,
    SETTINGS_SOURCE_MIXED_RECOVERED = 2,
} settings_source;

/**
 * Outcome flags produced by settings_validate, surfaced to telemetry (R16).
 *
 * source        Discriminated provenance of the resulting params.
 * settings_valid  True when source == NVS (every field passed as stored).
 * calibrated      True only when the params represent a real, fully-valid
 *                 stored calibration (source == NVS). Defaults and recovered
 *                 mixes are reported as !calibrated (UNCALIBRATED).
 * defaults_used   True when at least one field fell back to a default
 *                 (source == DEFAULTS or MIXED_RECOVERED).
 * nvs_error       True when the caller flagged the stored blob unreadable
 *                 (CRC/length/version) and validation ran on no input.
 */
typedef struct {
    settings_source source;
    bool settings_valid;
    bool calibrated;
    bool defaults_used;
    bool nvs_error;
} settings_validation_result;

/**
 * Fill *out with the conservative built-in defaults. Defaults are guaranteed
 * to satisfy every per-field and cross-field invariant in settings_validate.
 *
 * @param out  Destination (must be non-NULL).
 */
void settings_load_defaults(settings_params *out);

/**
 * Validate stored settings against per-field ranges and cross-field
 * invariants, repairing out-of-range fields in place by falling back to the
 * corresponding default, and report provenance flags.
 *
 * @param stored      The candidate params read from NVS. If has_stored is
 *                    false this is ignored and pure defaults are produced.
 * @param has_stored  False when NVS was empty/corrupt/version-mismatch
 *                    (no usable blob); true when a blob was decoded.
 * @param out         Destination for the resulting (possibly repaired) params
 *                    (must be non-NULL).
 * @return            Provenance/telemetry flags for the result.
 */
settings_validation_result settings_validate(const settings_params *stored,
                                             bool has_stored,
                                             settings_params *out);

#ifdef __cplusplus
}
#endif
