#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure fixed-capacity set of WebSocket client file descriptors (Unit 1).
 *
 * Backs the multi-client telemetry broadcast: a small, framework-agnostic list
 * of connected WS fds. No esp_* dependencies -> host-testable with oracle power.
 * A free slot is encoded as -1. Membership is by fd value (a valid WS fd is
 * always >= 0), so negative fds are never stored.
 *
 * NOT thread-safe by itself: callers must serialise mutations. In the telemetry
 * module the invariant is that all mutations happen on the httpd task.
 */

/* Max simultaneous telemetry clients (panel + app + spare). Kept in sync with
 * http_server's max_open_sockets budget (see http_server.c comment). */
#define WS_TELEMETRY_MAX_CLIENTS 4

/* Result of an add: distinguishes a fresh insert from an idempotent duplicate
 * and from a rejected insert because the set is full. */
typedef enum {
    WS_CLIENT_ADDED = 0,
    WS_CLIENT_ALREADY_PRESENT,
    WS_CLIENT_FULL,
} ws_client_add_result;

/* Fixed-capacity fd set. Treat as opaque; use the functions below. */
typedef struct {
    int fds[WS_TELEMETRY_MAX_CLIENTS];
} ws_client_set;

/**
 * Initialise the set to empty (all slots free).
 */
void ws_client_set_init(ws_client_set *set);

/**
 * Add fd to the set.
 *
 * @return WS_CLIENT_ADDED on a fresh insert, WS_CLIENT_ALREADY_PRESENT if fd was
 *         already a member (idempotent, set unchanged), WS_CLIENT_FULL if there
 *         is no free slot (set unchanged).
 */
ws_client_add_result ws_client_set_add(ws_client_set *set, int fd);

/**
 * Remove fd from the set.
 *
 * @return true if fd was present and removed, false if fd was not a member.
 */
bool ws_client_set_remove(ws_client_set *set, int fd);

/**
 * @return true if fd is currently a member.
 */
bool ws_client_set_contains(const ws_client_set *set, int fd);

/**
 * @return the number of members currently in the set (0..WS_TELEMETRY_MAX_CLIENTS).
 */
size_t ws_client_set_count(const ws_client_set *set);

/**
 * Iteration accessor: return the index-th member fd in [0, count).
 *
 * Members are returned with no holes: index 0..count-1 always yield valid fds
 * regardless of which internal slots are free. Out-of-range index returns -1.
 */
int ws_client_set_at(const ws_client_set *set, size_t index);

#ifdef __cplusplus
}
#endif
