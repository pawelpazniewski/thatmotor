#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) serialisation of settings_params to/from a
 * versioned NVS blob with an application-level CRC32. No IDF dependencies, so
 * the whole codec is host-testable.
 *
 * Blob layout (deterministic, little-endian, no struct padding / ABI reliance):
 *
 *   [ schema_version : u16 ]   first, so a layout change is detected up front
 *   [ ...settings fields... ]  every settings_params field, in declared order
 *   [ crc32 : u32 ]            IEEE 802.3 CRC32 over every preceding byte
 *
 * The CRC is computed over the schema_version + payload bytes only (not over
 * itself). On decode: length must match exactly, the trailing CRC must match
 * the recomputed CRC, and the schema_version must equal SETTINGS_SCHEMA_VERSION.
 * Any mismatch -> rejected (the caller loads conservative defaults instead;
 * reload-defaults over migration in this safety-critical path).
 */

/* Encoded blob size: 21 u16 fields (2 bytes each) + 2 bool fields (1 byte each)
 * + u32 CRC. Kept as a compile-time constant so callers size buffers exactly. */
#define BLOB_CODEC_FIELD_BYTES 44U
#define BLOB_CODEC_CRC_BYTES 4U
#define BLOB_CODEC_SIZE (BLOB_CODEC_FIELD_BYTES + BLOB_CODEC_CRC_BYTES)

/** Decode outcome. Distinguishes the failure modes for telemetry/logging. */
typedef enum {
    BLOB_CODEC_OK = 0,
    BLOB_CODEC_ERR_LENGTH = 1,       /* blob length != BLOB_CODEC_SIZE */
    BLOB_CODEC_ERR_CRC = 2,          /* trailing CRC32 mismatch (corrupt) */
    BLOB_CODEC_ERR_SCHEMA = 3,       /* schema_version != current (->defaults) */
    BLOB_CODEC_ERR_ARG = 4,          /* NULL pointer argument */
} blob_codec_result;

/**
 * Compute the IEEE 802.3 (zlib/PKZIP) CRC32 of a byte range. Reflected, poly
 * 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF. Deterministic and shared
 * by encode/decode so the HAL never needs a separate CRC implementation.
 *
 * @param data  Bytes to checksum (may be NULL only when len == 0).
 * @param len   Number of bytes.
 * @return CRC32 of the range.
 */
uint32_t blob_codec_crc32(const uint8_t *data, size_t len);

/**
 * Serialise params into a BLOB_CODEC_SIZE buffer (versioned + CRC-stamped).
 * The serialised schema_version is always SETTINGS_SCHEMA_VERSION regardless of
 * params->schema_version, so the stored blob is self-describing.
 *
 * @param params  Source params (must be non-NULL).
 * @param out     Destination buffer (must be non-NULL).
 * @param out_len Capacity of out; must be >= BLOB_CODEC_SIZE.
 * @return BLOB_CODEC_OK, or BLOB_CODEC_ERR_ARG / BLOB_CODEC_ERR_LENGTH.
 */
blob_codec_result blob_codec_encode(const settings_params *params, uint8_t *out,
                                    size_t out_len);

/**
 * Validate and deserialise a stored blob into params. Checks length, then CRC,
 * then schema_version, in that order; *out is written only on BLOB_CODEC_OK.
 *
 * @param blob  Stored bytes (must be non-NULL).
 * @param len   Number of stored bytes.
 * @param out   Destination params (must be non-NULL).
 * @return BLOB_CODEC_OK on a fully valid blob, else the rejecting reason.
 */
blob_codec_result blob_codec_decode(const uint8_t *blob, size_t len,
                                    settings_params *out);

#ifdef __cplusplus
}
#endif
