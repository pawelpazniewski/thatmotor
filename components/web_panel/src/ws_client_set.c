#include "ws_client_set.h"

/* Free-slot sentinel. A valid WS fd is always >= 0, so -1 is unambiguous. */
#define WS_CLIENT_FREE_SLOT (-1)

void ws_client_set_init(ws_client_set *set)
{
    for (size_t i = 0; i < WS_TELEMETRY_MAX_CLIENTS; ++i) {
        set->fds[i] = WS_CLIENT_FREE_SLOT;
    }
}

ws_client_add_result ws_client_set_add(ws_client_set *set, int fd)
{
    int free_slot = WS_CLIENT_FREE_SLOT;
    for (size_t i = 0; i < WS_TELEMETRY_MAX_CLIENTS; ++i) {
        if (set->fds[i] == fd) {
            return WS_CLIENT_ALREADY_PRESENT;
        }
        if (set->fds[i] == WS_CLIENT_FREE_SLOT && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (free_slot < 0) {
        return WS_CLIENT_FULL;
    }
    set->fds[free_slot] = fd;
    return WS_CLIENT_ADDED;
}

bool ws_client_set_remove(ws_client_set *set, int fd)
{
    for (size_t i = 0; i < WS_TELEMETRY_MAX_CLIENTS; ++i) {
        if (set->fds[i] == fd) {
            set->fds[i] = WS_CLIENT_FREE_SLOT;
            return true;
        }
    }
    return false;
}

bool ws_client_set_contains(const ws_client_set *set, int fd)
{
    for (size_t i = 0; i < WS_TELEMETRY_MAX_CLIENTS; ++i) {
        if (set->fds[i] == fd) {
            return true;
        }
    }
    return false;
}

size_t ws_client_set_count(const ws_client_set *set)
{
    size_t count = 0;
    for (size_t i = 0; i < WS_TELEMETRY_MAX_CLIENTS; ++i) {
        if (set->fds[i] != WS_CLIENT_FREE_SLOT) {
            ++count;
        }
    }
    return count;
}

int ws_client_set_at(const ws_client_set *set, size_t index)
{
    size_t seen = 0;
    for (size_t i = 0; i < WS_TELEMETRY_MAX_CLIENTS; ++i) {
        if (set->fds[i] == WS_CLIENT_FREE_SLOT) {
            continue;
        }
        if (seen == index) {
            return set->fds[i];
        }
        ++seen;
    }
    return WS_CLIENT_FREE_SLOT;
}
