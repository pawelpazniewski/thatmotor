#pragma once

#include "esp_err.h"
#include "settings_model.h"
#include "settings_validate.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NVS persistence HAL for the settings blob (Unit 8). Thin adapter over the NVS
 * flash API on the dedicated `appcfg` partition: it serialises params to a
 * versioned + CRC32-stamped blob (blob_codec) and runs the Unit 4 validation on
 * every read, so a corrupt/empty/version-mismatched store can never surface a
 * value outside the sanity window. The pure logic (blob_codec, settings_validate)
 * lives elsewhere; this file only touches flash.
 */

/* Dedicated config partition (partitions.csv) and the blob's namespace/key. */
#define NVS_STORE_PARTITION "appcfg"
#define NVS_STORE_NAMESPACE "settings"
#define NVS_STORE_KEY "params"

/**
 * Load and validate persisted settings. Opens (init-recovering the partition if
 * NO_FREE_PAGES / NEW_VERSION_FOUND), reads the blob, checks length + CRC +
 * schema, then runs settings_validate. On any read/decode failure the result is
 * conservative defaults; *result reports the provenance/telemetry flags (R16).
 *
 * The NEW_VERSION_FOUND recovery is treated as an alert (logged), NOT as
 * corruption: it means a newer NVS format, not a bad blob.
 *
 * @param out     Destination for the effective params (must be non-NULL).
 * @param result  Destination for provenance/telemetry flags (must be non-NULL).
 * @return ESP_OK once out and result are populated (defaults on a read miss);
 *         a hard esp_err_t only on an unrecoverable NVS init failure.
 */
esp_err_t nvs_store_load(settings_params *out,
                         settings_validation_result *result);

/**
 * Serialise params to a versioned + CRC32 blob and commit it to NVS. The caller
 * owns the DISARMED gate (R17) and the debounce timing; this only writes.
 *
 * @param params  Params snapshot to persist (must be non-NULL).
 * @return ESP_OK on a committed write, otherwise the failing esp_err_t.
 */
esp_err_t nvs_store_commit(const settings_params *params);

#ifdef __cplusplus
}
#endif
