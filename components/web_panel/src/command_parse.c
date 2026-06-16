#include "command_parse.h"

#include <string.h>

/* Command keyword -> event mapping table. One row per recognised keyword; the
 * event fields a recognised keyword sets are filled, the rest stay inert. */
typedef struct {
    const char *cmd;
    command_parse_result result;
} command_row;

static const command_row COMMAND_TABLE[] = {
    {"arm", {.ok = true, .arm_request = true}},
    {"disarm", {.ok = true, .disarm_request = true}},
    {"calib_start", {.ok = true, .calib_request = true, .calib_confirm = true}},
    {"calib_next", {.ok = true, .calib_event = CALIB_EVENT_NEXT}},
    {"calib_cancel", {.ok = true, .calib_event = CALIB_EVENT_CANCEL}},
};

#define COMMAND_TABLE_COUNT (sizeof(COMMAND_TABLE) / sizeof(COMMAND_TABLE[0]))

command_parse_result command_parse(const char *cmd)
{
    command_parse_result unknown = {0};
    if (cmd == NULL) {
        return unknown;
    }
    for (size_t i = 0; i < COMMAND_TABLE_COUNT; ++i) {
        if (strcmp(cmd, COMMAND_TABLE[i].cmd) == 0) {
            return COMMAND_TABLE[i].result;
        }
    }
    return unknown;
}
