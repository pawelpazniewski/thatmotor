#include "nvs_store.h"

#include <stddef.h>
#include <stdint.h>

#include "blob_codec.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "settings_model.h"
#include "settings_validate.h"

static const char *TAG = "nvs_store";

/* Init-recover the dedicated config partition. NO_FREE_PAGES (truncated) and
 * NEW_VERSION_FOUND (newer NVS format) both require erase + re-init; the latter
 * is an alert, not corruption, so it is logged distinctly. */
static esp_err_t init_partition(void)
{
    esp_err_t err = nvs_flash_init_partition(NVS_STORE_PARTITION);
    if (err == ESP_OK) {
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "appcfg: newer NVS format found, erasing (alert, not corruption)");
    } else if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGW(TAG, "appcfg: no free pages, erasing and re-initialising");
    } else {
        return err;
    }
    ESP_ERROR_CHECK(nvs_flash_erase_partition(NVS_STORE_PARTITION));
    return nvs_flash_init_partition(NVS_STORE_PARTITION);
}

/* Read the raw blob into buf. Returns ESP_OK with *out_len set on a full read,
 * or the NVS error (NOT_FOUND when absent, INVALID_LENGTH on a size mismatch). */
static esp_err_t read_blob(uint8_t *buf, size_t *out_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(NVS_STORE_PARTITION,
                                            NVS_STORE_NAMESPACE,
                                            NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = BLOB_CODEC_SIZE;
    err = nvs_get_blob(handle, NVS_STORE_KEY, buf, &len);
    nvs_close(handle);
    if (err == ESP_OK) {
        *out_len = len;
    }
    return err;
}

/* Decode + validate the read blob into out/result. has_stored is true only when
 * the blob length+CRC+schema all pass, so the Unit 4 validation always runs and
 * a corrupt store can never yield a value outside the sanity window. */
static void resolve_params(const uint8_t *buf, size_t len, esp_err_t read_err,
                           settings_params *out,
                           settings_validation_result *result)
{
    settings_params decoded;
    bool has_stored = false;
    if (read_err == ESP_OK) {
        blob_codec_result decode = blob_codec_decode(buf, len, &decoded);
        has_stored = (decode == BLOB_CODEC_OK);
        if (!has_stored) {
            ESP_LOGW(TAG, "stored blob rejected (code %d) -> defaults", decode);
        }
    } else if (read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "blob read error 0x%x -> defaults", read_err);
    }
    *result = settings_validate(has_stored ? &decoded : NULL, has_stored, out);
}

esp_err_t nvs_store_load(settings_params *out,
                         settings_validation_result *result)
{
    if (out == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = init_partition();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t buf[BLOB_CODEC_SIZE];
    size_t len = 0;
    esp_err_t read_err = read_blob(buf, &len);
    resolve_params(buf, len, read_err, out, result);
    return ESP_OK;
}

esp_err_t nvs_store_commit(const settings_params *params)
{
    if (params == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[BLOB_CODEC_SIZE];
    if (blob_codec_encode(params, buf, sizeof(buf)) != BLOB_CODEC_OK) {
        return ESP_FAIL;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(NVS_STORE_PARTITION,
                                            NVS_STORE_NAMESPACE,
                                            NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, NVS_STORE_KEY, buf, sizeof(buf));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
